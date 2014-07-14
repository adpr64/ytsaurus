#include "stdafx.h"
#include "sequential_reader.h"
#include "config.h"
#include "private.h"
#include "dispatcher.h"

#include <core/misc/string.h>

#include <core/compression/codec.h>

namespace NYT {
namespace NChunkClient {

using namespace NChunkClient::NProto;

///////////////////////////////////////////////////////////////////////////////

TSequentialReader::TSequentialReader(
    TSequentialReaderConfigPtr config,
    std::vector<TBlockInfo> blockInfos,
    IReaderPtr chunkReader,
    NCompression::ECodec codecId)
    : UncompressedDataSize_(0)
    , CompressedDataSize_(0)
    , BlockInfos_(std::move(blockInfos))
    , Config_(config)
    , ChunkReader_(chunkReader)
    , AsyncSemaphore_(config->WindowSize)
    , Codec_(NCompression::GetCodec(codecId))
    , Logger(ChunkClientLogger)
{
    VERIFY_INVOKER_AFFINITY(TDispatcher::Get()->GetReaderInvoker(), ReaderThread);

    Logger.AddTag("ChunkId: %v", ChunkReader_->GetChunkId());

    YCHECK(ChunkReader_);

    std::vector<int> blockIndexes;
    for (const auto& info : BlockInfos_) {
        blockIndexes.push_back(info.Index);
    }

    LOG_DEBUG("Creating sequential reader (Blocks: [%v])",
        JoinToString(blockIndexes));

    BlockWindow.reserve(BlockInfos_.size());
    for (int i = 0; i < BlockInfos_.size(); ++i) {
        BlockWindow.push_back(NewPromise<TSharedRef>());
    }

    TDispatcher::Get()->GetReaderInvoker()->Invoke(BIND(
        &TSequentialReader::FetchNextGroup,
        MakeWeak(this)));
}

bool TSequentialReader::HasNext() const
{
    // No thread affinity - can be called from
    // ContinueNextRow of NTableClient::TChunkReader.
    return NextSequenceIndex < BlockWindow.size();
}

TSharedRef TSequentialReader::GetBlock()
{
    // No thread affinity - can be called from
    // ContinueNextRow of NTableClient::TChunkReader.
    YCHECK(!State.HasRunningOperation());
    YCHECK(NextSequenceIndex > 0);
    YCHECK(BlockWindow[NextSequenceIndex - 1].IsSet());

    return BlockWindow[NextSequenceIndex - 1].Get();
}

TAsyncError TSequentialReader::AsyncNextBlock()
{
    // No thread affinity - can be called from
    // ContinueNextRow of NTableClient::TChunkReader.

    YCHECK(HasNext());
    YCHECK(!State.HasRunningOperation());

    if (NextSequenceIndex > 0) {
        AsyncSemaphore_.Release(BlockWindow[NextSequenceIndex - 1].Get().Size());
        BlockWindow[NextSequenceIndex - 1].Reset();
    }

    State.StartOperation();

    auto this_ = MakeStrong(this);
    BlockWindow[NextSequenceIndex].Subscribe(
        BIND([=] (TSharedRef) {
            this_->State.FinishOperation();
        }));

    ++NextSequenceIndex;

    return State.GetOperationError();
}

void TSequentialReader::OnGotBlocks(
    int firstSequenceIndex,
    IReader::TReadBlocksResult readResult)
{
    VERIFY_THREAD_AFFINITY(ReaderThread);

    if (!State.IsActive())
        return;

    if (!readResult.IsOK()) {
        State.Fail(readResult);
        return;
    }

    const auto& blocks = readResult.Value();

    LOG_DEBUG("Got block group (FirstIndex: %v, BlockCount: %v)",
        firstSequenceIndex,
        blocks.size());

    TDispatcher::Get()->GetCompressionInvoker()->Invoke(BIND(
        &TSequentialReader::DecompressBlocks,
        MakeWeak(this),
        firstSequenceIndex,
        readResult));
}

void TSequentialReader::DecompressBlocks(
    int blockIndex,
    const IReader::TReadBlocksResult& readResult)
{
    const auto& readBlocks = readResult.Value();
    for (int i = 0; i < readBlocks.size(); ++i, ++blockIndex) {
        const auto& block = readBlocks[i];
        const auto& blockInfo = BlockInfos_[blockIndex];

        LOG_DEBUG("Started decompressing block (Block: %v)",
            blockInfo.Index);

        auto data = Codec_->Decompress(block);
        BlockWindow[blockIndex].Set(data);

        UncompressedDataSize_ += data.Size();
        CompressedDataSize_ += block.Size();

        i64 delta = data.Size() - BlockInfos_[blockIndex].Size;

        if (delta > 0) {
            AsyncSemaphore_.Acquire(delta);
        } else {
            AsyncSemaphore_.Release(-delta);
        }

        LOG_DEBUG("Finished decompressing block (BlockIndex: %v, CompressedSize: %v, UncompressedSize: %v)", 
            blockInfo.Index,
            block.Size(),
            data.Size());
    }
}

void TSequentialReader::FetchNextGroup()
{
    VERIFY_THREAD_AFFINITY(ReaderThread);

    // ToDo(psushin): maybe use SmallVector here?
    auto firstUnfetched = NextUnfetchedIndex;
    std::vector<int> blockIndexes;
    i64 groupSize = 0;
    while (NextUnfetchedIndex < BlockInfos_.size()) {
        auto& blockInfo = BlockInfos_[NextUnfetchedIndex];

        if (!blockIndexes.empty() && groupSize + blockInfo.Size > Config_->GroupSize) {
            // Do not exceed group size if possible.
            break;
        }

        blockIndexes.push_back(blockInfo.Index);
        groupSize += blockInfo.Size;
        ++NextUnfetchedIndex;
    }

    if (!groupSize) {
        FetchingCompleteEvent.Set();
        return;
    }

    LOG_DEBUG("Requesting block group (FirstIndex: %v, BlockCount: %v, GroupSize: %v)",
        firstUnfetched,
        blockIndexes.size(),
        groupSize);

    AsyncSemaphore_.GetReadyEvent().Subscribe(
        BIND(&TSequentialReader::RequestBlocks,
            MakeWeak(this),
            firstUnfetched,
            blockIndexes,
            groupSize)
        .Via(TDispatcher::Get()->GetReaderInvoker()));
}

void TSequentialReader::RequestBlocks(
    int firstIndex,
    const std::vector<int>& blockIndexes,
    i64 groupSize)
{
    AsyncSemaphore_.Acquire(groupSize);
    ChunkReader_->ReadBlocks(blockIndexes).Subscribe(
        BIND(&TSequentialReader::OnGotBlocks,
            MakeWeak(this),
            firstIndex)
        .Via(TDispatcher::Get()->GetReaderInvoker()));

    TDispatcher::Get()->GetReaderInvoker()->Invoke(BIND(
        &TSequentialReader::FetchNextGroup,
        MakeWeak(this)));
}

TFuture<void> TSequentialReader::GetFetchingCompleteEvent()
{
    return FetchingCompleteEvent;
}

///////////////////////////////////////////////////////////////////////////////

} // namespace NChunkClient
} // namespace NYT
