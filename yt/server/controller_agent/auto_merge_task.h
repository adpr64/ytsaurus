#pragma once

#include "private.h"

#include "chunk_pool_adapters.h"
#include "task.h"

#include <yt/server/chunk_pools/unordered_chunk_pool.h>

namespace NYT {
namespace NControllerAgent {

////////////////////////////////////////////////////////////////////////////////

class TAutoMergeChunkPoolAdapter
    : public TChunkPoolInputAdapterBase
{
public:
    //! Used only for persistence.
    TAutoMergeChunkPoolAdapter() = default;

    TAutoMergeChunkPoolAdapter(NChunkPools::IChunkPoolInput* underlyingInput, TAutoMergeTask* task);

    virtual NChunkPools::IChunkPoolInput::TCookie Add(
        NChunkPools::TChunkStripePtr stripe,
        NChunkPools::TChunkStripeKey key) override;

    void Persist(const TPersistenceContext& context);
private:
    DECLARE_DYNAMIC_PHOENIX_TYPE(TAutoMergeChunkPoolAdapter, 0xfb888bac);

    TAutoMergeTask* Task_;
};

////////////////////////////////////////////////////////////////////////////////

class TAutoMergeTask
    : public TTask
{
public:
    friend class TAutoMergeChunkPoolAdapter;

    //! Used only for persistense.
    TAutoMergeTask() = default;

    TAutoMergeTask(
        ITaskHostPtr taskHost,
        int tableIndex,
        int maxChunksPerJob,
        int desiredChunkSize,
        TEdgeDescriptor edgeDescriptor);

    TString GetId() const override;

    TTaskGroupPtr GetGroup() const override;

    TDuration GetLocalityTimeout() const override;

    NScheduler::TExtendedJobResources GetNeededResources(const TJobletPtr& joblet) const override;

    NChunkPools::IChunkPoolInput* GetChunkPoolInput() const override;

    NChunkPools::IChunkPoolOutput* GetChunkPoolOutput() const override;

    i64 GetDesiredChunkSize() const;

    EJobType GetJobType() const override;

    virtual int GetPendingJobCount() const override;

    virtual bool CanScheduleJob(NScheduler::ISchedulingContext* context, const TJobResources& jobLimits) override;

    virtual void OnJobStarted(TJobletPtr joblet) override;
    virtual void OnJobAborted(TJobletPtr joblet, const NScheduler::TAbortedJobSummary& jobSummary) override;
    virtual void OnJobFailed(TJobletPtr joblet, const NScheduler::TFailedJobSummary& jobSummary) override;
    virtual void OnJobCompleted(TJobletPtr joblet, NScheduler::TCompletedJobSummary& jobSummary) override;

    void RegisterTeleportChunk(NChunkClient::TInputChunkPtr chunk);

    virtual void SetupCallbacks() override;

    void Persist(const TPersistenceContext& context);

protected:
    NScheduler::TExtendedJobResources GetMinNeededResourcesHeavy() const override;

    void BuildJobSpec(TJobletPtr joblet, NJobTrackerClient::NProto::TJobSpec* jobSpec) override;

private:
    DECLARE_DYNAMIC_PHOENIX_TYPE(TAutoMergeTask, 0x4ef99f1a);

    std::unique_ptr<NChunkPools::IChunkPool> ChunkPool_;
    std::unique_ptr<TAutoMergeChunkPoolAdapter> ChunkPoolInput_;

    int TableIndex_;
    int CurrentChunkCount_ = 0;
    int MaxChunksPerJob_;
    i64 DesiredChunkSize_;

    bool CanScheduleJob_ = true;

    void UpdateSelf();
};

DEFINE_REFCOUNTED_TYPE(TAutoMergeTask);

////////////////////////////////////////////////////////////////////////////////

} // namespace NControllerAgent
} // namespace NYT

#define AUTO_MERGE_TASK_INL_H
#include "auto_merge_task-inl.h"
#undef AUTO_MERGE_TASK_INL_H