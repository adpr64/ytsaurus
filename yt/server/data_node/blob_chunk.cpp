#include "stdafx.h"
#include "blob_chunk.h"
#include "private.h"
#include "location.h"
#include "blob_reader_cache.h"
#include "chunk_cache.h"
#include "block_store.h"

#include <core/profiling/scoped_timer.h>

#include <core/concurrency/thread_affinity.h>

#include <ytlib/chunk_client/file_reader.h>
#include <ytlib/chunk_client/file_writer.h>
#include <ytlib/chunk_client/chunk_meta_extensions.h>

#include <server/misc/memory_usage_tracker.h>

#include <server/cell_node/bootstrap.h>
#include <server/cell_node/config.h>

namespace NYT {
namespace NDataNode {

using namespace NConcurrency;
using namespace NCellNode;
using namespace NNodeTrackerClient;
using namespace NChunkClient;
using namespace NChunkClient::NProto;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = DataNodeLogger;

static NProfiling::TSimpleCounter DiskBlobReadByteCounter("/blob_block_read_bytes");

////////////////////////////////////////////////////////////////////////////////

TBlobChunkBase::TBlobChunkBase(
    TBootstrap* bootstrap,
    TLocationPtr location,
    const TChunkDescriptor& descriptor,
    const TChunkMeta* meta)
    : TChunkBase(
        bootstrap,
        location,
        descriptor.Id)
{
    Info_.set_disk_space(descriptor.DiskSpace);
    if (meta) {
        SetCachedMeta(*meta);
    }
}

TBlobChunkBase::~TBlobChunkBase()
{
    auto cachedMeta = GetCachedMeta();
    if (cachedMeta) {
        auto* tracker = Bootstrap_->GetMemoryUsageTracker();
        tracker->Release(EMemoryCategory::ChunkMeta, cachedMeta->SpaceUsed());
    }
}

TChunkInfo TBlobChunkBase::GetInfo() const
{
    return Info_;
}

bool TBlobChunkBase::IsActive() const
{
    return false;
}

TFuture<TRefCountedChunkMetaPtr> TBlobChunkBase::ReadMeta(
    i64 priority,
    const TNullable<std::vector<int>>& extensionTags)
{
    VERIFY_THREAD_AFFINITY_ANY();

    auto cachedMeta = GetCachedMeta();
    if (cachedMeta) {
        LOG_TRACE("Meta cache hit (ChunkId: %v)", Id_);
        return MakeFuture(FilterMeta(cachedMeta, extensionTags));
    }

    LOG_DEBUG("Meta cache miss (ChunkId: %v)", Id_);

    auto readGuard = TChunkReadGuard::TryAcquire(this);
    if (!readGuard) {
        return MakeFuture<TRefCountedChunkMetaPtr>(TError("Cannot read meta of chunk %v: chunk is scheduled for removal",
            Id_));
    }

    auto promise = NewPromise<TRefCountedChunkMetaPtr>();
    auto callback = BIND(
        &TBlobChunkBase::DoReadMeta,
        MakeStrong(this),
        Passed(std::move(readGuard)),
        promise);
    Location_
        ->GetMetaReadInvoker()
        ->Invoke(callback, priority);

    return promise.ToFuture().Apply(BIND([=] (const TRefCountedChunkMetaPtr& cachedMeta) {
        return FilterMeta(cachedMeta, extensionTags);
    }));
}

void TBlobChunkBase::DoReadMeta(
    TChunkReadGuard /*readGuard*/,
    TPromise<TRefCountedChunkMetaPtr> promise)
{
    const auto& Profiler = Location_->GetProfiler();
    LOG_DEBUG("Started reading chunk meta (LocationId: %v, ChunkId: %v)",
        Location_->GetId(),
        Id_);

    NChunkClient::TFileReaderPtr reader;
    PROFILE_TIMING ("/meta_read_time") {
        auto readerCache = Bootstrap_->GetBlobReaderCache();
        try {
            reader = readerCache->GetReader(this);
        } catch (const std::exception& ex) {
            promise.Set(TError(ex));
            return;
        }
    }

    LOG_DEBUG("Finished reading chunk meta (LocationId: %v, ChunkId: %v)",
        Location_->GetId(),
        Id_);

    auto cachedMeta = SetCachedMeta(reader->GetMeta());
    promise.Set(cachedMeta);
}

TFuture<std::vector<TSharedRef>> TBlobChunkBase::ReadBlocks(
    int firstBlockIndex,
    int blockCount,
    i64 priority)
{
    VERIFY_THREAD_AFFINITY_ANY();
    YCHECK(firstBlockIndex >= 0);
    YCHECK(blockCount >= 0);

    i64 pendingSize;
    AdjustReadRange(firstBlockIndex, &blockCount, &pendingSize);

    TPendingReadSizeGuard pendingReadSizeGuard;
    if (pendingSize >= 0) {
        auto blockStore = Bootstrap_->GetBlockStore();
        pendingReadSizeGuard = blockStore->IncreasePendingReadSize(pendingSize);
    }

    auto promise = NewPromise<std::vector<TSharedRef>>();

    auto callback = BIND(
        &TBlobChunkBase::DoReadBlocks,
        MakeStrong(this),
        firstBlockIndex,
        blockCount,
        Passed(std::move(pendingReadSizeGuard)),
        promise);

    Location_
        ->GetDataReadInvoker()
        ->Invoke(callback, priority);

    return promise;
}

void TBlobChunkBase::DoReadBlocks(
    int firstBlockIndex,
    int blockCount,
    TPendingReadSizeGuard pendingReadSizeGuard,
    TPromise<std::vector<TSharedRef>> promise)
{
    auto blockStore = Bootstrap_->GetBlockStore();
    auto readerCache = Bootstrap_->GetBlobReaderCache();

    try {
        auto reader = readerCache->GetReader(this);

        if (!pendingReadSizeGuard) {
            SetCachedMeta(reader->GetMeta());
            
            i64 pendingSize;
            AdjustReadRange(firstBlockIndex, &blockCount, &pendingSize);
            YCHECK(pendingSize >= 0);

            pendingReadSizeGuard = blockStore->IncreasePendingReadSize(pendingSize);
        }

        std::vector<TSharedRef> blocks;

        LOG_DEBUG("Started reading blob chunk blocks (BlockIds: %v:%v-%v, LocationId: %v)",
            Id_,
            firstBlockIndex,
            firstBlockIndex + blockCount - 1,
            Location_->GetId());
            
        NProfiling::TScopedTimer timer;

        // NB: The reader is synchronous.
        auto blocksOrError = reader->ReadBlocks(firstBlockIndex, blockCount).Get();

        auto readTime = timer.GetElapsed();

        LOG_DEBUG("Finished reading blob chunk blocks (BlockIds: %v:%v-%v, LocationId: %v)",
            Id_,
            firstBlockIndex,
            firstBlockIndex + blockCount - 1,
            Location_->GetId());

        if (!blocksOrError.IsOK()) {
            auto error = TError(
                NChunkClient::EErrorCode::IOError,
                "Error reading blob chunk %v",
                Id_)
                << TError(blocksOrError);
            Location_->Disable(error);
            THROW_ERROR error;
        }

        auto& locationProfiler = Location_->GetProfiler();
        i64 pendingSize = pendingReadSizeGuard.GetSize();
        locationProfiler.Enqueue("/blob_block_read_size", pendingSize);
        locationProfiler.Enqueue("/blob_block_read_time", readTime.MicroSeconds());
        locationProfiler.Enqueue("/blob_block_read_throughput", pendingSize * 1000000 / (1 + readTime.MicroSeconds()));
        DataNodeProfiler.Increment(DiskBlobReadByteCounter, pendingSize);

        promise.Set(blocksOrError.Value());
    } catch (const std::exception& ex) {
        promise.Set(TError(ex));
    }
}

TRefCountedChunkMetaPtr TBlobChunkBase::GetCachedMeta()
{
    VERIFY_THREAD_AFFINITY_ANY();

    TReaderGuard guard(CachedMetaLock_);
    return CachedMeta_;
}

TRefCountedChunkMetaPtr TBlobChunkBase::SetCachedMeta(const NChunkClient::NProto::TChunkMeta& meta)
{
    VERIFY_THREAD_AFFINITY_ANY();

    TWriterGuard guard(CachedMetaLock_);

    CachedBlocksExt_ = GetProtoExtension<TBlocksExt>(meta.extensions());
    CachedMeta_ = New<TRefCountedChunkMeta>(meta);

    auto* tracker = Bootstrap_->GetMemoryUsageTracker();
    tracker->Acquire(EMemoryCategory::ChunkMeta, CachedMeta_->SpaceUsed());

    return CachedMeta_;
}

void TBlobChunkBase::AdjustReadRange(
    int firstBlockIndex,
    int* blockCount,
    i64* dataSize)
{
    VERIFY_THREAD_AFFINITY_ANY();

    auto cachedMeta = GetCachedMeta();
    if (!cachedMeta) {
        *dataSize = -1;
        return;
    }

    auto config = Bootstrap_->GetConfig()->DataNode;
    *blockCount = std::min(*blockCount, config->MaxBlocksPerRead);

    // TODO(babenko): CachedBlocksExt_ is accessed without guard;
    // we might change this if it becomes evictable.
    *dataSize = 0;
    int blockIndex = firstBlockIndex;
    while (
        blockIndex < firstBlockIndex + *blockCount &&
        blockIndex < CachedBlocksExt_.blocks_size() &&
        *dataSize <= config->MaxBytesPerRead)
    {
        const auto& blockInfo = CachedBlocksExt_.blocks(blockIndex);
        *dataSize += blockInfo.size();
        ++blockIndex;
    }

    *blockCount = blockIndex - firstBlockIndex;
}

void TBlobChunkBase::SyncRemove(bool force)
{
    auto readerCache = Bootstrap_->GetBlobReaderCache();
    readerCache->EvictReader(this);

    if (force) {
        Location_->RemoveChunkFiles(Id_);
    } else {
        Location_->MoveChunkFilesToTrash(Id_);
    }
}

TFuture<void> TBlobChunkBase::AsyncRemove()
{
    return BIND(&TBlobChunkBase::SyncRemove, MakeStrong(this), false)
        .AsyncVia(Location_->GetWritePoolInvoker())
        .Run();
}

////////////////////////////////////////////////////////////////////////////////

TStoredBlobChunk::TStoredBlobChunk(
    TBootstrap* bootstrap,
    TLocationPtr location,
    const TChunkDescriptor& descriptor,
    const TChunkMeta* meta)
    : TBlobChunkBase(
        bootstrap,
        location,
        descriptor,
        meta)
{ }

////////////////////////////////////////////////////////////////////////////////

TCachedBlobChunk::TCachedBlobChunk(
    TBootstrap* bootstrap,
    TLocationPtr location,
    const TChunkDescriptor& descriptor,
    const TChunkMeta* meta,
    TClosure destroyed)
    : TBlobChunkBase(
        bootstrap,
        location,
        descriptor,
        meta)
    , TAsyncCacheValueBase<TChunkId, TCachedBlobChunk>(GetId())
    , Destroyed_(destroyed)
{ }

TCachedBlobChunk::~TCachedBlobChunk()
{
    Destroyed_.Run();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NDataNode
} // namespace NYT
