#include "lease_tracker.h"
#include "private.h"
#include "config.h"
#include "decorated_automaton.h"

#include <yt/ytlib/election/cell_manager.h>
#include <yt/ytlib/election/config.h>

#include <yt/core/concurrency/scheduler.h>

namespace NYT::NHydra {

using namespace NElection;
using namespace NConcurrency;

////////////////////////////////////////////////////////////////////////////////

bool TLeaderLease::IsValid() const
{
    return NProfiling::GetCpuInstant() < Deadline_.load();
}

void TLeaderLease::SetDeadline(NProfiling::TCpuInstant deadline)
{
    YT_VERIFY(Deadline_.load() < deadline);
    Deadline_ = deadline;
}

void TLeaderLease::Invalidate()
{
    Deadline_ = 0;
}

////////////////////////////////////////////////////////////////////////////////

// Also pings non-voting peers.
class TLeaseTracker::TFollowerPinger
    : public TRefCounted
{
public:
    explicit TFollowerPinger(TLeaseTrackerPtr owner)
        : Owner_(std::move(owner))
        , Logger(Owner_->Logger)
    { }

    TFuture<void> Run()
    {
        VERIFY_THREAD_AFFINITY(Owner_->ControlThread);

        for (TPeerId id = 0; id < Owner_->EpochContext_->CellManager->GetTotalPeerCount(); ++id) {
            if (id == Owner_->EpochContext_->CellManager->GetSelfPeerId()) {
                OnSuccess();
            } else {
                SendPing(id);
            }
        }

        AllSucceeded(AsyncResults_).Subscribe(
            BIND(&TFollowerPinger::OnComplete, MakeStrong(this))
                .Via(Owner_->EpochContext_->EpochControlInvoker));

        return Promise_;
    }

private:
    const TLeaseTrackerPtr Owner_;
    const NLogging::TLogger Logger;

    int ActiveCount_ = 0;
    std::vector<TFuture<void>> AsyncResults_;
    std::vector<TError> PingErrors_;

    const TPromise<void> Promise_ = NewPromise<void>();


    void SendPing(TPeerId followerId)
    {
        auto channel = Owner_->EpochContext_->CellManager->GetPeerChannel(followerId);
        if (!channel) {
            return;
        }

        const auto& decoratedAutomaton = Owner_->DecoratedAutomaton_;
        const auto& epochContext = Owner_->EpochContext_;

        auto pingVersion = decoratedAutomaton->GetPingVersion();
        auto committedVersion = decoratedAutomaton->GetAutomatonVersion();

        YT_LOG_DEBUG("Sending ping to follower (FollowerId: %v, PingVersion: %v, CommittedVersion: %v, EpochId: %v)",
            followerId,
            pingVersion,
            committedVersion,
            epochContext->EpochId);

        THydraServiceProxy proxy(channel);
        auto req = proxy.PingFollower();
        req->SetTimeout(Owner_->Config_->LeaderLeaseTimeout);
        ToProto(req->mutable_epoch_id(), epochContext->EpochId);
        req->set_ping_revision(pingVersion.ToRevision());
        req->set_committed_revision(committedVersion.ToRevision());
        for (auto peerId : Owner_->AlivePeers_) {
            req->add_alive_peers(peerId);
        }

        bool voting = Owner_->EpochContext_->CellManager->GetPeerConfig(followerId).Voting;
        AsyncResults_.push_back(req->Invoke().Apply(
            BIND(&TFollowerPinger::OnResponse, MakeStrong(this), followerId, voting)
                .Via(epochContext->EpochControlInvoker)));
    }

    void OnResponse(
        TPeerId followerId,
        bool voting,
        const THydraServiceProxy::TErrorOrRspPingFollowerPtr& rspOrError)
    {
        VERIFY_THREAD_AFFINITY(Owner_->ControlThread);

        if (!rspOrError.IsOK()) {
            PingErrors_.push_back(rspOrError);
            YT_LOG_WARNING(rspOrError, "Error pinging follower (PeerId: %v)",
                followerId);
            return;
        }

        const auto& rsp = rspOrError.Value();
        auto state = EPeerState(rsp->state());
        YT_LOG_DEBUG("Follower ping succeeded (PeerId: %v, State: %v)",
            followerId,
            state);

        if (voting) {
            if (state == EPeerState::Following) {
                OnSuccess();
            } else {
                PingErrors_.push_back(TError("Follower %v is in %Qlv state",
                    followerId,
                    state));
            }
        }
    }

    void OnComplete(const TError&)
    {
        VERIFY_THREAD_AFFINITY(Owner_->ControlThread);

        if (!Promise_.IsSet()) {
            auto error = TError("Could not acquire quorum")
                << PingErrors_;
            Promise_.Set(error);
        }
    }

    void OnSuccess()
    {
        ++ActiveCount_;
        if (ActiveCount_ == Owner_->EpochContext_->CellManager->GetQuorumPeerCount()) {
            Promise_.Set();
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

TLeaseTracker::TLeaseTracker(
    TDistributedHydraManagerConfigPtr config,
    TDecoratedAutomatonPtr decoratedAutomaton,
    TEpochContext* epochContext,
    TLeaderLeasePtr lease,
    std::vector<TCallback<TFuture<void>()>> customLeaseCheckers,
    NLogging::TLogger logger)
    : Config_(std::move(config))
    , DecoratedAutomaton_(std::move(decoratedAutomaton))
    , EpochContext_(epochContext)
    , Lease_(std::move(lease))
    , CustomLeaseCheckers_(std::move(customLeaseCheckers))
    , Logger(std::move(logger))
    , LeaseCheckExecutor_(New<TPeriodicExecutor>(
        EpochContext_->EpochControlInvoker,
        BIND(&TLeaseTracker::OnLeaseCheck, MakeWeak(this)),
        Config_->LeaderLeaseCheckPeriod))
{
    YT_VERIFY(Config_);
    YT_VERIFY(DecoratedAutomaton_);
    YT_VERIFY(EpochContext_);
    YT_VERIFY(Lease_);
    VERIFY_INVOKER_THREAD_AFFINITY(EpochContext_->EpochControlInvoker, ControlThread);
}

void TLeaseTracker::Start()
{
    VERIFY_THREAD_AFFINITY(ControlThread);

    AlivePeers_.clear();
    for (TPeerId id = 0; id < EpochContext_->CellManager->GetTotalPeerCount(); ++id) {
        AlivePeers_.insert(id);
    }

    LeaseCheckExecutor_->Start();
}

void TLeaseTracker::SetAlivePeers(const TPeerIdSet& alivePeers)
{
    VERIFY_THREAD_AFFINITY(ControlThread);

    AlivePeers_ = alivePeers;
}

TFuture<void> TLeaseTracker::GetLeaseAcquired()
{
    VERIFY_THREAD_AFFINITY(ControlThread);

    return LeaseAcquired_;
}

TFuture<void> TLeaseTracker::GetLeaseLost()
{
    VERIFY_THREAD_AFFINITY(ControlThread);

    return LeaseLost_;
}

void TLeaseTracker::OnLeaseCheck()
{
    VERIFY_THREAD_AFFINITY(ControlThread);

    YT_LOG_DEBUG("Starting leader lease check");

    auto startTime = NProfiling::GetCpuInstant();
    auto asyncResult = FireLeaseCheck();
    auto result = WaitFor(asyncResult);

    if (result.IsOK()) {
        Lease_->SetDeadline(startTime + NProfiling::DurationToCpuDuration(Config_->LeaderLeaseTimeout));
        YT_LOG_DEBUG("Leader lease check succeeded");
        if (!LeaseAcquired_.IsSet()) {
            LeaseAcquired_.Set();
        }
    } else {
        YT_LOG_DEBUG(result, "Leader lease check failed");
        if (Lease_->IsValid() && LeaseAcquired_.IsSet() && !LeaseLost_.IsSet()) {
            Lease_->Invalidate();
            LeaseLost_.Set(result);
        }
    }
}

TFuture<void> TLeaseTracker::FireLeaseCheck()
{
    VERIFY_THREAD_AFFINITY(ControlThread);

    std::vector<TFuture<void>> asyncResults;

    auto pinger = New<TFollowerPinger>(this);
    asyncResults.push_back(pinger->Run());

    for (auto callback : CustomLeaseCheckers_) {
        asyncResults.push_back(callback.Run());
    }

    return AllSucceeded(asyncResults);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NHydra
