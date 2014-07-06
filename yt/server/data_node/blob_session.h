#pragma once

#include "public.h"
#include "session_detail.h"

#include <core/concurrency/thread_affinity.h>
#include <core/concurrency/throughput_throttler.h>

#include <ytlib/chunk_client/data_node_service_proxy.h>

#include <core/logging/tagged_logger.h>

#include <core/profiling/profiler.h>

#include <server/cell_node/public.h>

namespace NYT {
namespace NDataNode {

////////////////////////////////////////////////////////////////////////////////

class TBlobSession
    : public TSessionBase
{
public:
    TBlobSession(
        TDataNodeConfigPtr config,
        NCellNode::TBootstrap* bootstrap,
        const TChunkId& chunkId,
        const TSessionOptions& options,
        TLocationPtr location);

    NChunkClient::NProto::TChunkInfo GetChunkInfo() const override;

private:
    DECLARE_ENUM(ESlotState,
        (Empty)
        (Received)
        (Written)
    );

    struct TSlot
    {
        ESlotState State = ESlotState::Empty;
        TSharedRef Block;
        TPromise<void> WrittenPromise = NewPromise();
    };

    TError Error_;
    std::vector<TSlot> Window_;
    int WindowStartIndex_ = 0;
    int WriteIndex_ = 0;
    i64 Size_ = 0;
    int BlockCount_ = 0;
    NChunkClient::TFileWriterPtr Writer_;


    virtual void DoStart() override;
    void DoOpenWriter();
   
    virtual TAsyncError DoPutBlocks(
        int startBlockIndex,
        const std::vector<TSharedRef>& blocks,
        bool enableCaching) override;

    virtual TAsyncError DoSendBlocks(
        int startBlockIndex,
        int blockCount,
        const NNodeTrackerClient::TNodeDescriptor& target) override;

    virtual TAsyncError DoFlushBlocks(int blockIndex) override;

    virtual void DoCancel() override;

    virtual TFuture<TErrorOr<IChunkPtr>> DoFinish(
        const NChunkClient::NProto::TChunkMeta& chunkMeta,
        const TNullable<int>& blockCount) override;

    bool IsInWindow(int blockIndex);
    void ValidateBlockIsInWindow(int blockIndex);
    TSlot& GetSlot(int blockIndex);
    void ReleaseBlocks(int flushedBlockIndex);
    TSharedRef GetBlock(int blockIndex);
    void MarkAllSlotsWritten();

    TAsyncError AbortWriter();
    TError DoAbortWriter();
    TError OnWriterAborted(TError error);

    TAsyncError CloseWriter(const NChunkClient::NProto::TChunkMeta& chunkMeta);
    TError DoCloseWriter(const NChunkClient::NProto::TChunkMeta& chunkMeta);
    TErrorOr<IChunkPtr> OnWriterClosed(TError error);

    TError DoWriteBlock(const TSharedRef& block, int blockIndex);
    void OnBlockWritten(int blockIndex, TError error);

    TError OnBlockFlushed(int blockIndex);

    void ReleaseSpace();

    void OnIOError(const TError& error);

};

DEFINE_REFCOUNTED_TYPE(TBlobSession)

////////////////////////////////////////////////////////////////////////////////

} // namespace NDataNode
} // namespace NYT

