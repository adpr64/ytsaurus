#include "throughput_throttler.h"
#include "periodic_executor.h"
#include "config.h"

#include <yt/core/concurrency/thread_affinity.h>

#include <yt/core/misc/singleton.h>

#include <yt/core/profiling/profiler.h>

#include <queue>

namespace NYT {
namespace NConcurrency {

////////////////////////////////////////////////////////////////////////////////

class TReconfigurableThroughputThrottler
    : public IReconfigurableThroughputThrottler
{
public:
    TReconfigurableThroughputThrottler(
        TThroughputThrottlerConfigPtr config,
        const NLogging::TLogger& logger,
        const NProfiling::TProfiler& profiler)
        : Logger(logger)
        , Profiler(profiler)
        , ValueCounter_("/value")
    {
        Reconfigure(config);
    }

    virtual TFuture<void> Throttle(i64 count) override
    {
        VERIFY_THREAD_AFFINITY_ANY();
        YCHECK(count >= 0);

        Profiler.Increment(ValueCounter_, count);

        if (count == 0) {
            return VoidFuture;
        }

        TGuard<TSpinLock> guard(SpinLock_);

        if (!Limit_) {
            return VoidFuture;
        }

        if (Available_ > 0) {
            // Execute immediately.
            Available_ -= count;
            return VoidFuture;
        }

        // Enqueue request to be executed later.
        LOG_DEBUG("Started waiting for throttler (Count: %v)", count);
        TRequest request{count, NewPromise<void>()};
        Requests_.push(request);
        QueueTotalCount_ += count;
        return request.Promise;
    }

    virtual bool TryAcquire(i64 count) override
    {
        VERIFY_THREAD_AFFINITY_ANY();
        YCHECK(count >= 0);

        if (Limit_) {
            TGuard<TSpinLock> guard(SpinLock_);
            if (Available_ < count) {
                return false;
            }
            Available_ -= count;
        }

        Profiler.Increment(ValueCounter_, count);
        return true;
    }

    virtual void Acquire(i64 count) override
    {
        VERIFY_THREAD_AFFINITY_ANY();
        YCHECK(count >= 0);

        if (Limit_) {
            TGuard<TSpinLock> guard(SpinLock_);
            Available_ -= count;
        }

        Profiler.Increment(ValueCounter_, count);
    }

    virtual bool IsOverdraft() const override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        TGuard<TSpinLock> guard(SpinLock_);
        return Available_ < 0;
    }

    virtual void Reconfigure(TThroughputThrottlerConfigPtr config) override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        TGuard<TSpinLock> guard(SpinLock_);

        Limit_ = config->Limit;
        if (Limit_) {
            ThroughputPerPeriod_ = static_cast<i64>(config->Period.SecondsFloat() * (*Limit_));
            Available_ = ThroughputPerPeriod_;
            PeriodicExecutor_ = New<TPeriodicExecutor>(
                GetSyncInvoker(),
                BIND(&TReconfigurableThroughputThrottler::OnTick, MakeWeak(this)),
                config->Period);
            PeriodicExecutor_->Start();
        } else {
            ThroughputPerPeriod_ = -1;
            Available_ = -1;
        }

        ProcessRequests(std::move(guard));
    }

    virtual i64 GetQueueTotalCount() const override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return QueueTotalCount_;
    }

private:
    const NLogging::TLogger Logger;
    const NProfiling::TProfiler Profiler;

    NProfiling::TAggregateCounter ValueCounter_;

    //! Protects the section immediately following it.
    TSpinLock SpinLock_;
    i64 Available_ = -1;
    TNullable<i64> Limit_;
    i64 ThroughputPerPeriod_ = -1;
    TPeriodicExecutorPtr PeriodicExecutor_;

    struct TRequest
    {
        i64 Count;
        TPromise<void> Promise;
    };

    std::atomic<i64> QueueTotalCount_ = {0};
    std::queue<TRequest> Requests_;


    void OnTick()
    {
        VERIFY_THREAD_AFFINITY_ANY();

        TGuard<TSpinLock> guard(SpinLock_);

        Available_ += ThroughputPerPeriod_;
        if (Available_ > ThroughputPerPeriod_) {
            Available_ = ThroughputPerPeriod_;
        }

        ProcessRequests(std::move(guard));
    }

    void ProcessRequests(TGuard<TSpinLock> guard)
    {
        std::vector<TPromise<void>> readyList;
        std::vector<TPromise<void>> canceledList;

        while (!Requests_.empty() && (!Limit_ || Available_ > 0)) {
            auto& request = Requests_.front();
            LOG_DEBUG("Finished waiting for throttler (Count: %v)", request.Count);
            if (request.Promise.IsCanceled()) {
                canceledList.push_back(std::move(request.Promise));
            } else {
                if (Limit_) {
                    Available_ -= request.Count;
                }
                readyList.push_back(std::move(request.Promise));
            }
            QueueTotalCount_ -= request.Count;
            Requests_.pop();
        }

        guard.Release();

        for (auto& promise : readyList) {
            promise.Set();
        }

        for (auto& promise : canceledList) {
            promise.Set(TError(NYT::EErrorCode::Canceled, "Throttled request canceled"));
        }
    }

};

IReconfigurableThroughputThrottlerPtr CreateReconfigurableThroughputThrottler(
    TThroughputThrottlerConfigPtr config,
    const NLogging::TLogger& logger,
    const NProfiling::TProfiler& profiler)
{
    return New<TReconfigurableThroughputThrottler>(
        config,
        logger,
        profiler);
}

////////////////////////////////////////////////////////////////////////////////

class TUnlimitedThroughtputThrottler
    : public IThroughputThrottler
{
public:
    virtual TFuture<void> Throttle(i64 count) override
    {
        VERIFY_THREAD_AFFINITY_ANY();
        YCHECK(count >= 0);

        return VoidFuture;
    }

    virtual bool TryAcquire(i64 count) override
    {
        VERIFY_THREAD_AFFINITY_ANY();
        YCHECK(count >= 0);

        return true;
    }

    virtual void Acquire(i64 count) override
    {
        VERIFY_THREAD_AFFINITY_ANY();
        YCHECK(count >= 0);
    }

    virtual bool IsOverdraft() const override
    {
        VERIFY_THREAD_AFFINITY_ANY();
        return false;
    }

    virtual i64 GetQueueTotalCount() const override
    {
        VERIFY_THREAD_AFFINITY_ANY();
        return 0;
    }
};

IThroughputThrottlerPtr GetUnlimitedThrottler()
{
    return RefCountedSingleton<TUnlimitedThroughtputThrottler>();
}

////////////////////////////////////////////////////////////////////////////////

class TCombinedThroughtputThrottler
    : public IThroughputThrottler
{
public:
    explicit TCombinedThroughtputThrottler(const std::vector<IThroughputThrottlerPtr>& throttlers)
        : Throttlers_(throttlers)
    { }

    virtual TFuture<void> Throttle(i64 count) override
    {
        VERIFY_THREAD_AFFINITY_ANY();
        YCHECK(count >= 0);

        QueueTotalCount_ += count;

        std::vector<TFuture<void>> asyncResults;
        for (const auto& throttler : Throttlers_) {
            asyncResults.push_back(throttler->Throttle(count));
        }

        return Combine(asyncResults).Apply(BIND([weakThis = MakeWeak(this), count] {
            if (auto this_ = weakThis.Lock()) {
                this_->QueueTotalCount_ -= count;
            }
        }));
    }

    virtual bool TryAcquire(i64 /*count*/) override
    {
        Y_UNREACHABLE();
    }

    virtual void Acquire(i64 count) override
    {
        VERIFY_THREAD_AFFINITY_ANY();
        YCHECK(count >= 0);

        for (const auto& throttler : Throttlers_) {
            throttler->Acquire(count);
        }
    }

    virtual bool IsOverdraft() const override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        for (const auto& throttler : Throttlers_) {
            if (throttler->IsOverdraft()) {
                return true;
            }
        }
        return false;
    }

    virtual i64 GetQueueTotalCount() const override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return QueueTotalCount_;
    }

private:
    const std::vector<IThroughputThrottlerPtr> Throttlers_;

    std::atomic<i64> QueueTotalCount_ = {0};
};

IThroughputThrottlerPtr CreateCombinedThrottler(
    const std::vector<IThroughputThrottlerPtr>& throttler)
{
    return New<TCombinedThroughtputThrottler>(throttler);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NConcurrency
} // namespace NYT
