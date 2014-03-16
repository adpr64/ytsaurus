#pragma once

#include "public.h"

#include <ytlib/chunk_client/public.h>
#include <ytlib/chunk_client/fetcher_base.h>

#include <ytlib/node_tracker_client/public.h>

namespace NYT {
namespace NVersionedTableClient {

////////////////////////////////////////////////////////////////////////////////

//! Fetches samples for a bunch of table chunks by requesting
//! them directly from data nodes.
class TChunkSplitsFetcher
    : public NChunkClient::TFetcherBase
{
public:
    TChunkSplitsFetcher(
        NChunkClient::TFetcherConfigPtr config,
        i64 chunkSliceSize,
        const TKeyColumns& keyColumns,
        NNodeTrackerClient::TNodeDirectoryPtr nodeDirectory,
        IInvokerPtr invoker,
        NLog::TTaggedLogger& logger);


    virtual TAsyncError Fetch() override;
    const std::vector<NChunkClient::TRefCountedChunkSpecPtr>& GetChunkSplits() const;

private:
    i64 ChunkSliceSize_;
    TKeyColumns KeyColumns_;

    //! All samples fetched so far.
    std::vector<NChunkClient::TRefCountedChunkSpecPtr> ChunkSplits_;

    virtual TFuture<void> FetchFromNode(
        const NNodeTrackerClient::TNodeId& nodeId,
        std::vector<int>&& chunkIndexes) override;

    void DoFetchFromNode(
        const NNodeTrackerClient::TNodeId& nodeId,
        const std::vector<int>& chunkIndexes);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NVersionedTableClient
} // namespace NYT
