#include "stdafx.h"
#include "chunk_balancer.h"
#include "holder_lease_tracker.h"
#include "chunk_placement.h"
#include "holder.h"
#include "job.h"
#include "chunk.h"
#include "job_list.h"

#include <ytlib/misc/foreach.h>
#include <ytlib/misc/serialize.h>
#include <ytlib/misc/string.h>
#include <ytlib/cell_master/bootstrap.h>
#include <ytlib/cell_master/config.h>
#include <ytlib/chunk_server/chunk_manager.h>
#include <ytlib/profiling/profiler.h>
#include <ytlib/profiling/timing.h>

namespace NYT {
namespace NChunkServer {

using namespace NCellMaster;
using namespace NProto;
using namespace NProfiling;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger Logger("ChunkServer");
static NProfiling::TProfiler Profiler("/chunk_server");

////////////////////////////////////////////////////////////////////////////////

TChunkBalancer::TChunkBalancer(
    TChunkManagerConfigPtr config,
    TBootstrap* bootstrap,
    TChunkPlacementPtr chunkPlacement,
    THolderLeaseTrackerPtr holderLeaseTracker)
    : Config(config)
    , Bootstrap(bootstrap)
    , ChunkPlacement(chunkPlacement)
    , HolderLeaseTracker(holderLeaseTracker)
    , ChunkRefreshDelay(DurationToCpuDuration(config->ChunkRefreshDelay))
{
    YASSERT(config);
    YASSERT(bootstrap);
    YASSERT(chunkPlacement);
    YASSERT(holderLeaseTracker);

    ScheduleNextRefresh();
}

void TChunkBalancer::ScheduleJobs(
    THolder* holder,
    const std::vector<TJobInfo>& runningJobs,
    std::vector<TJobStartInfo>* jobsToStart,
    std::vector<TJobStopInfo>* jobsToStop)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    int replicationJobCount;
    int removalJobCount;
    ProcessExistingJobs(
        holder,
        runningJobs,
        jobsToStop,
        &replicationJobCount,
        &removalJobCount);

    if (IsEnabled()) {
        ScheduleNewJobs(
            holder,
            Max(0, Config->Balancer->MaxReplicationFanOut - replicationJobCount),
            Max(0, Config->Balancer->MaxRemovalJobsPerHolder - removalJobCount),
            jobsToStart);
    }
}

void TChunkBalancer::OnHolderRegistered(const THolder* holder)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    YCHECK(HolderInfoMap.insert(MakePair(holder->GetId(), THolderInfo())).second);

    FOREACH (const auto* chunk, holder->StoredChunks()) {
        ScheduleChunkRefresh(chunk->GetId());
    }
}

void TChunkBalancer::OnHolderUnregistered(const THolder* holder)
{
    VERIFY_THREAD_AFFINITY(StateThread);

    YCHECK(HolderInfoMap.erase(holder->GetId()) == 1);
}

void TChunkBalancer::OnChunkRemoved(const TChunk* chunk)
{
    auto chunkId = chunk->GetId();
    LostChunkIds_.erase(chunkId);
    UnderreplicatedChunkIds_.erase(chunkId);
    OverreplicatedChunkIds_.erase(chunkId);
}

void TChunkBalancer::ScheduleChunkRemoval(const THolder* holder, const TChunkId& chunkId)
{
    auto* holderInfo = GetHolderInfo(holder->GetId());
    holderInfo->ChunksToReplicate.erase(chunkId);
    holderInfo->ChunksToRemove.insert(chunkId);
}

void TChunkBalancer::ProcessExistingJobs(
    const THolder* holder,
    const std::vector<TJobInfo>& runningJobs,
    std::vector<TJobStopInfo>* jobsToStop,
    int* replicationJobCount,
    int* removalJobCount)
{
    *replicationJobCount = 0;
    *removalJobCount = 0;

    yhash_set<TJobId> runningJobIds;

    auto chunkManager = Bootstrap->GetChunkManager();
    FOREACH (const auto& jobInfo, runningJobs) {
        auto jobId = TJobId::FromProto(jobInfo.job_id());
        runningJobIds.insert(jobId);
        const auto* job = chunkManager->FindJob(jobId);

        if (!job) {
            LOG_WARNING("Stopping unknown or obsolete job %s on %s",
                ~jobId.ToString(),
                ~holder->GetAddress());
            TJobStopInfo stopInfo;
            *stopInfo.mutable_job_id() = jobId.ToProto();
            jobsToStop->push_back(stopInfo);
            continue;
        }

        auto jobState = EJobState(jobInfo.state());
        switch (jobState) {
            case EJobState::Running:
                switch (job->GetType()) {
                    case EJobType::Replicate:
                        ++*replicationJobCount;
                        break;

                    case EJobType::Remove:
                        ++*removalJobCount;
                        break;

                    default:
                        YUNREACHABLE();
                }
                LOG_INFO("Job %s is running on %s",
                    ~jobId.ToString(),
                    ~holder->GetAddress());

                if (TInstant::Now() - job->GetStartTime() > Config->Balancer->JobTimeout) {
                    TJobStopInfo stopInfo;
                    *stopInfo.mutable_job_id() = jobId.ToProto();
                    jobsToStop->push_back(stopInfo);

                    LOG_WARNING("Job %s has timed out on %s after %s",
                        ~jobId.ToString(),
                        ~holder->GetAddress(),
                        ~ToString(TInstant::Now() - job->GetStartTime()));
                }
                break;

            case EJobState::Completed:
            case EJobState::Failed: {
                TJobStopInfo stopInfo;
                *stopInfo.mutable_job_id() = jobId.ToProto();
                jobsToStop->push_back(stopInfo);

                ScheduleChunkRefresh(job->GetChunkId());

                LOG_INFO("Job %s has %s on %s",
                    ~jobId.ToString(),
                    jobState == EJobState::Completed ? "completed" : "failed",
                    ~holder->GetAddress());
                break;
            }

            default:
                YUNREACHABLE();
        }
    }

    // Check for missing jobs
    FOREACH (auto job, holder->Jobs()) {
        auto jobId = job->GetId();
        if (runningJobIds.find(jobId) == runningJobIds.end()) {
            TJobStopInfo stopInfo;
            *stopInfo.mutable_job_id() = jobId.ToProto();
            jobsToStop->push_back(stopInfo);

            LOG_WARNING("Job %s is missing on %s",
                ~jobId.ToString(),
                ~holder->GetAddress());
        }
    }
}

bool TChunkBalancer::IsRefreshScheduled(const TChunkId& chunkId)
{
    return RefreshSet.find(chunkId) != RefreshSet.end();
}

TChunkBalancer::EScheduleFlags TChunkBalancer::ScheduleReplicationJob(
    THolder* sourceHolder,
    const TChunkId& chunkId,
    std::vector<TJobStartInfo>* jobsToStart)
{
    auto chunkManager = Bootstrap->GetChunkManager();
    const auto* chunk = chunkManager->FindChunk(chunkId);
    if (!chunk) {
        LOG_TRACE("Chunk %s we're about to replicate is missing on %s",
            ~chunkId.ToString(),
            ~sourceHolder->GetAddress());
        return EScheduleFlags::Purged;
    }

    if (IsRefreshScheduled(chunkId)) {
        LOG_TRACE("Chunk %s we're about to replicate is scheduled for another refresh",
            ~chunkId.ToString());
        return EScheduleFlags::Purged;
    }

    int replicationFactor;
    int storedCount;
    int cachedCount;
    int plusCount;
    int minusCount;
    GetReplicaStatistics(
        chunk,
        &replicationFactor,
        &storedCount,
        &cachedCount,
        &plusCount,
        &minusCount);

    int replicasNeeded = replicationFactor - (storedCount + plusCount);
    if (replicasNeeded <= 0) {
        LOG_TRACE("Chunk %s we're about to replicate has enough replicas",
            ~chunkId.ToString());
        return EScheduleFlags::Purged;
    }

    auto targets = ChunkPlacement->GetReplicationTargets(chunk, replicasNeeded);
    if (targets.empty()) {
        LOG_TRACE("No suitable target holders for replication of chunk %s",
            ~chunkId.ToString());
        return EScheduleFlags::None;
    }

    std::vector<Stroka> targetAddresses;
    FOREACH (auto* holder, targets) {
        targetAddresses.push_back(holder->GetAddress());
        ChunkPlacement->OnSessionHinted(holder);
    }

    auto jobId = TJobId::Create();
    TJobStartInfo startInfo;
    *startInfo.mutable_job_id() = jobId.ToProto();
    startInfo.set_type(EJobType::Replicate);
    *startInfo.mutable_chunk_id() = chunkId.ToProto();
    ToProto(startInfo.mutable_target_addresses(), targetAddresses);
    jobsToStart->push_back(startInfo);

    LOG_DEBUG("Job %s is scheduled on %s: replicate chunk %s to [%s]",
        ~jobId.ToString(),
        ~sourceHolder->GetAddress(),
        ~chunkId.ToString(),
        ~JoinToString(targetAddresses));

    return
        targetAddresses.size() == replicasNeeded
        // TODO: flagged enums
        ? (EScheduleFlags) (EScheduleFlags::Purged | EScheduleFlags::Scheduled)
        : (EScheduleFlags) EScheduleFlags::Scheduled;
}

TChunkBalancer::EScheduleFlags TChunkBalancer::ScheduleBalancingJob(
    THolder* sourceHolder,
    const TChunkId& chunkId,
    std::vector<TJobStartInfo>* jobsToStart)
{
    auto chunkManager = Bootstrap->GetChunkManager();
    auto* chunk = chunkManager->GetChunk(chunkId);

    if (IsRefreshScheduled(chunkId)) {
        LOG_DEBUG("Chunk %s we're about to balance is scheduled for another refresh",
            ~chunkId.ToString());
        return EScheduleFlags::None;
    }

    double maxFillCoeff =
        ChunkPlacement->GetFillCoeff(sourceHolder) -
        Config->Balancer->MinBalancingFillCoeffDiff;
    auto targetHolder = ChunkPlacement->GetBalancingTarget(chunk, maxFillCoeff);
    if (targetHolder == NULL) {
        LOG_DEBUG("No suitable target holders for balancing of chunk %s",
            ~chunkId.ToString());
        return EScheduleFlags::None;
    }

    ChunkPlacement->OnSessionHinted(targetHolder);
    
    auto jobId = TJobId::Create();
    TJobStartInfo startInfo;
    *startInfo.mutable_job_id() = jobId.ToProto();
    startInfo.set_type(EJobType::Replicate);
    *startInfo.mutable_chunk_id() = chunkId.ToProto();
    startInfo.add_target_addresses(targetHolder->GetAddress());
    jobsToStart->push_back(startInfo);

    LOG_DEBUG("Job %s is scheduled on %s: balance chunk %s to [%s]",
        ~jobId.ToString(),
        ~sourceHolder->GetAddress(),
        ~chunkId.ToString(),
        ~targetHolder->GetAddress());

    // TODO: flagged enums
    return (EScheduleFlags) (EScheduleFlags::Purged | EScheduleFlags::Scheduled);
}

TChunkBalancer::EScheduleFlags TChunkBalancer::ScheduleRemovalJob(
    THolder* holder,
    const TChunkId& chunkId,
    std::vector<TJobStartInfo>* jobsToStart)
{
    if (IsRefreshScheduled(chunkId)) {
        LOG_DEBUG("Chunk %s we're about to remove is scheduled for another refresh",
            ~chunkId.ToString());
        return EScheduleFlags::None;
    }
    
    auto jobId = TJobId::Create();
    TJobStartInfo startInfo;
    *startInfo.mutable_job_id() = jobId.ToProto();
    startInfo.set_type(EJobType::Remove);
    *startInfo.mutable_chunk_id() = chunkId.ToProto();
    jobsToStart->push_back(startInfo);

    LOG_DEBUG("Job %s is scheduled on %s: chunk %s will be removed",
        ~jobId.ToString(),
        ~holder->GetAddress(),
        ~chunkId.ToString());

    // TODO: flagged enums
    return (EScheduleFlags) (EScheduleFlags::Purged | EScheduleFlags::Scheduled);
}

void TChunkBalancer::ScheduleNewJobs(
    THolder* holder,
    int maxReplicationJobsToStart,
    int maxRemovalJobsToStart,
    std::vector<TJobStartInfo>* jobsToStart)
{
    auto* holderInfo = FindHolderInfo(holder->GetId());
    if (!holderInfo)
        return;

    // Schedule replication jobs.
    if (maxReplicationJobsToStart > 0) {
        auto& chunksToReplicate = holderInfo->ChunksToReplicate;
        auto it = chunksToReplicate.begin();
        while (it != chunksToReplicate.end()) {
            auto jt = it;
            ++jt;
            const auto& chunkId = *it;
            if (maxReplicationJobsToStart == 0) {
                break;
            }
            auto flags = ScheduleReplicationJob(holder, chunkId, jobsToStart);
            if (flags & EScheduleFlags::Scheduled) {
                --maxReplicationJobsToStart;
            }
            if (flags & EScheduleFlags::Purged) {
                chunksToReplicate.erase(it);
            }
            it = jt;
        }
    }

    // Schedule balancing jobs.
    if (maxReplicationJobsToStart > 0 &&
        ChunkPlacement->GetFillCoeff(holder) > Config->Balancer->MinBalancingFillCoeff)
    {
        auto chunksToBalance = ChunkPlacement->GetBalancingChunks(holder, maxReplicationJobsToStart);
        if (!chunksToBalance.empty()) {
            LOG_DEBUG("Balancing chunks [%s] on %s",
                ~JoinToString(chunksToBalance),
                ~holder->GetAddress());

            FOREACH (const auto& chunkId, chunksToBalance) {
                if (maxReplicationJobsToStart == 0) {
                    break;
                }
                auto flags = ScheduleBalancingJob(holder, chunkId, jobsToStart);
                if (flags & EScheduleFlags::Scheduled) {
                    --maxReplicationJobsToStart;
                }
            }
        }
    }

    // Schedule removal jobs.
    if (maxRemovalJobsToStart > 0) {
        auto& chunksToRemove = holderInfo->ChunksToRemove;
        auto it = chunksToRemove.begin();
        while (it != chunksToRemove.end()) {
            const auto& chunkId = *it;
            auto jt = it;
            ++jt;
            if (maxRemovalJobsToStart == 0) {
                break;
            }
            auto flags = ScheduleRemovalJob(holder, chunkId, jobsToStart);
            if (flags & EScheduleFlags::Scheduled) {
                --maxReplicationJobsToStart;
            }
            if (flags & EScheduleFlags::Purged) {
                chunksToRemove.erase(it);
            }
            it = jt;
        }
    }
}

void TChunkBalancer::GetReplicaStatistics(
    const TChunk* chunk,
    int* replicationFactor,
    int* storedCount,
    int* cachedCount,
    int* plusCount,
    int* minusCount)
{
    *replicationFactor = GetReplicationFactor(chunk);
    *storedCount = static_cast<int>(chunk->StoredLocations().size());
    *cachedCount = !~chunk->CachedLocations() ? 0 : static_cast<int>(chunk->CachedLocations()->size());
    *plusCount = 0;
    *minusCount = 0;

    if (*storedCount == 0) {
        return;
    }

    auto chunkManager = Bootstrap->GetChunkManager();
    const auto* jobList = chunkManager->FindJobList(chunk->GetId());
    if (jobList) {
        yhash_set<Stroka> storedAddresses(*storedCount);
        FOREACH (auto holderId, chunk->StoredLocations()) {
            const auto& holder = chunkManager->GetHolder(holderId);
            storedAddresses.insert(holder->GetAddress());
        }

        FOREACH (auto& job, jobList->Jobs()) {
            switch (job->GetType()) {
                case EJobType::Replicate: {
                    FOREACH (const auto& address, job->TargetAddresses()) {
                        if (storedAddresses.find(address) == storedAddresses.end()) {
                            ++*plusCount;
                        }
                    }
                    break;
                }

                case EJobType::Remove:
                    if (storedAddresses.find(job->GetAddress()) != storedAddresses.end()) {
                        ++*minusCount;
                    }
                    break;

                default:
                    YUNREACHABLE();
                }
        }
    }
}

int TChunkBalancer::GetReplicationFactor(const TChunk* chunk)
{
    return chunk->GetReplicationFactor();
}

void TChunkBalancer::Refresh(const TChunk* chunk)
{
    int replicationFactor;
    int storedCount;
    int cachedCount;
    int plusCount;
    int minusCount;
    GetReplicaStatistics(
        chunk,
        &replicationFactor,
        &storedCount,
        &cachedCount,
        &plusCount,
        &minusCount);

    auto replicaCountStr = Sprintf("%d+%d+%d-%d",
        storedCount,
        cachedCount,
        plusCount,
        minusCount);

    FOREACH (auto holderId, chunk->StoredLocations()) {
        auto* holderInfo = FindHolderInfo(holderId);
        if (holderInfo) {
            holderInfo->ChunksToReplicate.erase(chunk->GetId());
            holderInfo->ChunksToRemove.erase(chunk->GetId());
        }
    }
    auto chunkId = chunk->GetId();
    LostChunkIds_.erase(chunkId);
    OverreplicatedChunkIds_.erase(chunkId);
    UnderreplicatedChunkIds_.erase(chunkId);

    auto chunkManager = Bootstrap->GetChunkManager();
    if (storedCount == 0) {
        LostChunkIds_.insert(chunkId);

        LOG_TRACE("Chunk %s is lost: %d replicas needed but only %s exist",
            ~chunk->GetId().ToString(),
            replicationFactor,
            ~replicaCountStr);
    } else if (storedCount - minusCount > replicationFactor) {
        OverreplicatedChunkIds_.insert(chunkId);

        // NB: Never start removal jobs if new replicas are on the way, hence the check plusCount > 0.
        if (plusCount > 0) {
            LOG_WARNING("Chunk %s is over-replicated: %s replicas exist but only %d needed, waiting for pending replications to complete",
                ~chunk->GetId().ToString(),
                ~replicaCountStr,
                replicationFactor);
            return;
        }

        auto holders = ChunkPlacement->GetRemovalTargets(chunk, storedCount - minusCount - replicationFactor);
        FOREACH (auto* holder, holders) {
            auto* holderInfo = GetHolderInfo(holder->GetId());
            holderInfo->ChunksToRemove.insert(chunk->GetId());
        }

        std::vector<Stroka> holderAddresses;
        FOREACH (auto holder, holders) {
            holderAddresses.push_back(holder->GetAddress());
        }

        LOG_DEBUG("Chunk %s is over-replicated: %s replicas exist but only %d needed, removal is scheduled on [%s]",
            ~chunk->GetId().ToString(),
            ~replicaCountStr,
            replicationFactor,
            ~JoinToString(holderAddresses));
    } else if (storedCount + plusCount < replicationFactor) {
        UnderreplicatedChunkIds_.insert(chunkId);

        // NB: Never start replication jobs when removal jobs are in progress, hence the check minusCount > 0.
        if (minusCount > 0) {
            LOG_WARNING("Chunk %s is under-replicated: %s replicas exist but %d needed, waiting for pending removals to complete",
                ~chunk->GetId().ToString(),
                ~replicaCountStr,
                replicationFactor);
            return;
        }

        auto* holder = ChunkPlacement->GetReplicationSource(chunk);
        auto* holderInfo = GetHolderInfo(holder->GetId());

        holderInfo->ChunksToReplicate.insert(chunk->GetId());

        LOG_DEBUG("Chunk %s is under-replicated: %s replicas exist but %d needed, replication is scheduled on %s",
            ~chunk->GetId().ToString(),
            ~replicaCountStr,
            replicationFactor,
            ~holder->GetAddress());
    } else {
        LOG_TRACE("Chunk %s is OK: %s replicas exist and %d needed",
            ~chunk->GetId().ToString(),
            ~replicaCountStr,
            replicationFactor);
    }
 }

void TChunkBalancer::ScheduleChunkRefresh(const TChunkId& chunkId)
{
    if (RefreshSet.find(chunkId) != RefreshSet.end())
        return;

    TRefreshEntry entry;
    entry.ChunkId = chunkId;
    entry.When = GetCpuInstant() + ChunkRefreshDelay;
    RefreshList.push_back(entry);
    RefreshSet.insert(chunkId);
}

void TChunkBalancer::RefreshAllChunks()
{
    auto chunkManager = Bootstrap->GetChunkManager();
    FOREACH (auto* chunk, chunkManager->GetChunks()) {

        Refresh(chunk);
    }
}

void TChunkBalancer::ScheduleNextRefresh()
{
    auto context = Bootstrap->GetMetaStateManager()->GetEpochContext();
    if (!context)
        return;
    TDelayedInvoker::Submit(
        BIND(&TChunkBalancer::OnRefresh, MakeStrong(this))
        .Via(
            Bootstrap->GetStateInvoker(EStateThreadQueue::ChunkRefresh),
            context),
        Config->ChunkRefreshQuantum);
}

void TChunkBalancer::OnRefresh()
{
    VERIFY_THREAD_AFFINITY(StateThread);

    PROFILE_TIMING ("/incremental_chunk_refresh_time") {
        auto chunkManager = Bootstrap->GetChunkManager();
        auto now = GetCpuInstant();
        for (int i = 0; i < Config->MaxChunksPerRefresh; ++i) {
            if (RefreshList.empty())
                break;

            const auto& entry = RefreshList.front();
            if (entry.When > now)
                break;

            auto* chunk = chunkManager->FindChunk(entry.ChunkId);
            if (chunk) {
                Refresh(chunk);
            }

            YCHECK(RefreshSet.erase(entry.ChunkId) == 1);
            RefreshList.pop_front();
        }
    }

    ScheduleNextRefresh();
}

TChunkBalancer::THolderInfo* TChunkBalancer::FindHolderInfo(THolderId holderId)
{
    auto it = HolderInfoMap.find(holderId);
    return it == HolderInfoMap.end() ? NULL : &it->second;
}

TChunkBalancer::THolderInfo* TChunkBalancer::GetHolderInfo(THolderId holderId)
{
    auto it = HolderInfoMap.find(holderId);
    YASSERT(it != HolderInfoMap.end());
    return &it->second;
}

bool TChunkBalancer::IsEnabled()
{
    // This method also logs state changes.

    auto config = Config->Balancer;
    if (config->MinOnlineHolderCount) {
        int needOnline = config->MinOnlineHolderCount.Get();
        int gotOnline = HolderLeaseTracker->GetOnlineHolderCount();
        if (gotOnline < needOnline) {
            if (!LastEnabled || LastEnabled.Get()) {
                LOG_INFO("Chunk balancer disabled: too few online nodes, needed >= %d but got %d",
                    needOnline,
                    gotOnline);
                LastEnabled = false;
            }
            return false;
        }
    }

    if (config->MaxLostChunkFraction)
    {
        auto chunkManager = Bootstrap->GetChunkManager();
        double needFraction = config->MaxLostChunkFraction.Get();
        double gotFraction = (double) chunkManager->LostChunkIds().size() / chunkManager->GetChunkCount();
        if (gotFraction > needFraction) {
            if (!LastEnabled || LastEnabled.Get()) {
                LOG_INFO("Chunk balancer disabled: too many lost chunks, needed <= %lf but got %lf",
                    needFraction,
                    gotFraction);
                LastEnabled = false;
            }
            return false;
        }
    }

    if (!LastEnabled || !LastEnabled.Get()) {
        LOG_INFO("Chunk balancer enabled");
        LastEnabled = true;
    }

    return true;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
