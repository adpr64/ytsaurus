#include "partition_chunk_reader.h"
#include "private.h"

#include "chunk_meta_extensions.h"
#include "columnar_chunk_meta.h"
#include "schemaless_multi_chunk_reader.h"

#include <yt/yt/ytlib/chunk_client/config.h>
#include <yt/yt/ytlib/chunk_client/dispatcher.h>
#include <yt/yt/ytlib/chunk_client/data_source.h>
#include <yt/yt/ytlib/chunk_client/helpers.h>
#include <yt/yt/ytlib/chunk_client/parallel_reader_memory_manager.h>
#include <yt/yt/ytlib/chunk_client/reader_factory.h>
#include <yt/yt/ytlib/chunk_client/chunk_reader_statistics.h>

#include <yt/yt/client/node_tracker_client/node_directory.h>

#include <yt/yt/client/table_client/schema.h>
#include <yt/yt/client/table_client/name_table.h>

#include <yt/yt/core/concurrency/scheduler.h>

#include <yt/yt/core/ytree/yson_serializable.h>

namespace NYT::NTableClient {

using namespace NChunkClient;
using namespace NChunkClient::NProto;
using namespace NConcurrency;
using namespace NRpc;
using namespace NApi;
using namespace NNodeTrackerClient;
using namespace NYTree;

using NChunkClient::TDataSliceDescriptor;
using NChunkClient::TChunkReaderStatistics;

using NYT::FromProto;

////////////////////////////////////////////////////////////////////////////////

TPartitionChunkReader::TPartitionChunkReader(
    TBlockFetcherConfigPtr config,
    IChunkReaderPtr underlyingReader,
    TNameTablePtr nameTable,
    IBlockCachePtr blockCache,
    const TClientChunkReadOptions& chunkReadOptions,
    int partitionTag,
    TChunkReaderMemoryManagerPtr memoryManager)
    : TChunkReaderBase(
        std::move(config),
        std::move(underlyingReader),
        std::move(blockCache),
        chunkReadOptions,
        std::move(memoryManager))
    , NameTable_(nameTable)
    , PartitionTag_(partitionTag)
{
    SetReadyEvent(BIND(&TPartitionChunkReader::InitializeBlockSequence, MakeStrong(this))
        .AsyncVia(NChunkClient::TDispatcher::Get()->GetReaderInvoker())
        .Run());
}

TFuture<void> TPartitionChunkReader::InitializeBlockSequence()
{
    const std::vector<int> extensionTags = {
        TProtoExtensionTag<TMiscExt>::Value,
        TProtoExtensionTag<NProto::TBlockMetaExt>::Value,
        TProtoExtensionTag<NProto::TTableSchemaExt>::Value,
        TProtoExtensionTag<NProto::TNameTableExt>::Value,
    };

    ChunkMeta_ = WaitFor(UnderlyingReader_->GetMeta(
        BlockReadOptions_,
        PartitionTag_,
        extensionTags))
        .ValueOrThrow();

    YT_VERIFY(FromProto<ETableChunkFormat>(ChunkMeta_->format()) == ETableChunkFormat::SchemalessHorizontal);

    TNameTablePtr chunkNameTable;
    auto nameTableExt = GetProtoExtension<NProto::TNameTableExt>(ChunkMeta_->extensions());
    try {
        FromProto(&chunkNameTable, nameTableExt);
    } catch (const std::exception& ex) {
        THROW_ERROR_EXCEPTION(
            EErrorCode::CorruptedNameTable,
            "Failed to deserialize name table for partition chunk reader")
            << TErrorAttribute("chunk_id", UnderlyingReader_->GetChunkId())
            << ex;
    }

    InitNameTable(chunkNameTable);

    BlockMetaExt_ = GetProtoExtension<NProto::TBlockMetaExt>(ChunkMeta_->extensions());
    std::vector<TBlockFetcher::TBlockInfo> blocks;
    for (auto& blockMeta : BlockMetaExt_.blocks()) {
        TBlockFetcher::TBlockInfo blockInfo;
        blockInfo.Index = blockMeta.block_index();
        blockInfo.UncompressedDataSize = blockMeta.uncompressed_size();
        blockInfo.Priority = blocks.size();
        blocks.push_back(blockInfo);
    }

    return DoOpen(blocks, GetProtoExtension<TMiscExt>(ChunkMeta_->extensions()));
}

void TPartitionChunkReader::InitFirstBlock()
{
    YT_VERIFY(CurrentBlock_ && CurrentBlock_.IsSet());
    auto schema = GetTableSchema(*ChunkMeta_);
    BlockReader_ = new THorizontalBlockReader(
        CurrentBlock_.Get().ValueOrThrow().Data,
        BlockMetaExt_.blocks(CurrentBlockIndex_),
        schema,
        IdMapping_,
        schema->ToComparator(),
        schema->ToComparator());

    BlockReaders_.emplace_back(BlockReader_);
}

void TPartitionChunkReader::InitNextBlock()
{
    ++CurrentBlockIndex_;
    InitFirstBlock();
}

void TPartitionChunkReader::InitNameTable(TNameTablePtr chunkNameTable)
{
    IdMapping_.reserve(chunkNameTable->GetSize());

    try {
        for (int chunkNameId = 0; chunkNameId < chunkNameTable->GetSize(); ++chunkNameId) {
            auto name = chunkNameTable->GetName(chunkNameId);
            auto id = NameTable_->GetIdOrRegisterName(name);
            IdMapping_.push_back({chunkNameId, id});
        }
    } catch (const std::exception& ex) {
        THROW_ERROR_EXCEPTION("Failed to add column to name table for partition chunk reader") << ex;
    }
}

////////////////////////////////////////////////////////////////////////////////

TPartitionMultiChunkReader::TPartitionMultiChunkReader(IMultiReaderManagerPtr multiReaderManager)
    : MultiReaderManager_(std::move(multiReaderManager))
{
    MultiReaderManager_->SubscribeReaderSwitched(BIND(
        &TPartitionMultiChunkReader::OnReaderSwitched,
        MakeWeak(this)));
}

void TPartitionMultiChunkReader::OnReaderSwitched()
{
    CurrentReader_ = dynamic_cast<TPartitionChunkReader*>(MultiReaderManager_->GetCurrentSession().Reader.Get());
    YT_VERIFY(CurrentReader_);
}

TPartitionMultiChunkReaderPtr CreatePartitionMultiChunkReader(
    TMultiChunkReaderConfigPtr config,
    TMultiChunkReaderOptionsPtr options,
    NNative::IClientPtr client,
    IBlockCachePtr blockCache,
    TNodeDirectoryPtr nodeDirectory,
    const TDataSourceDirectoryPtr& dataSourceDirectory,
    const std::vector<TDataSliceDescriptor>& dataSliceDescriptors,
    TNameTablePtr nameTable,
    int partitionTag,
    const TClientChunkReadOptions& chunkReadOptions,
    TTrafficMeterPtr trafficMeter,
    IThroughputThrottlerPtr bandwidthThrottler,
    IThroughputThrottlerPtr rpsThrottler,
    IMultiReaderMemoryManagerPtr multiReaderMemoryManager)
{
    if (!multiReaderMemoryManager) {
        multiReaderMemoryManager = CreateParallelReaderMemoryManager(
            TParallelReaderMemoryManagerOptions{
                .TotalReservedMemorySize = config->MaxBufferSize,
                .MaxInitialReaderReservedMemory = config->WindowSize
            },
            NChunkClient::TDispatcher::Get()->GetReaderMemoryManagerInvoker());
    }

    std::vector<IReaderFactoryPtr> factories;
    for (const auto& dataSliceDescriptor : dataSliceDescriptors) {
        const auto& dataSource = dataSourceDirectory->DataSources()[dataSliceDescriptor.GetDataSourceIndex()];
        switch (dataSource.GetType()) {
            case EDataSourceType::UnversionedTable: {
                const auto& chunkSpec = dataSliceDescriptor.GetSingleChunk();

                auto memoryEstimate = GetChunkReaderMemoryEstimate(chunkSpec, config);

                auto createReader = BIND([=] () -> IReaderBasePtr {
                    auto remoteReader = CreateRemoteReader(
                        chunkSpec,
                        config,
                        options,
                        client,
                        nodeDirectory,
                        /* localDescriptor */ {},
                        /* partitionTag */ std::nullopt,
                        blockCache,
                        trafficMeter,
                        bandwidthThrottler,
                        rpsThrottler);

                    YT_VERIFY(!chunkSpec.has_lower_limit());
                    YT_VERIFY(!chunkSpec.has_upper_limit());

                    TBlockFetcherConfigPtr sequentialReaderConfig = config;

                    return New<TPartitionChunkReader>(
                        sequentialReaderConfig,
                        remoteReader,
                        nameTable,
                        blockCache,
                        chunkReadOptions,
                        partitionTag,
                        multiReaderMemoryManager->CreateChunkReaderMemoryManager(memoryEstimate));
                });

                auto canCreateReader = BIND([=] {
                    return multiReaderMemoryManager->GetFreeMemorySize() >= memoryEstimate;
                });

                factories.push_back(CreateReaderFactory(
                    createReader,
                    canCreateReader,
                    dataSliceDescriptor));
                break;
            }

            default:
                YT_ABORT();
        }
    }

    auto reader = New<TPartitionMultiChunkReader>(CreateParallelMultiReaderManager(
        std::move(config),
        std::move(options),
        factories,
        std::move(multiReaderMemoryManager)));

    reader->Open();
    return reader;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableClient

