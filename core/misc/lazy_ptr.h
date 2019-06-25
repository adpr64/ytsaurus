#pragma once

#include "common.h"

#include <yt/core/actions/callback.h>

#include <yt/core/tracing/trace_context.h>

#include <util/system/spinlock.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

template <class T>
const TCallback<TIntrusivePtr<T>()>& DefaultRefCountedFactory()
{
    static auto result = BIND([] () -> TIntrusivePtr<T> {
        return New<T>();
    });
    return result;
}

//! Intrusive ptr with lazy creation and double-checked locking.
template <class T, class TLock = TSpinLock>
class TLazyIntrusivePtr
    : public TPointerCommon<TLazyIntrusivePtr<T, TLock>, T>
{
public:
    typedef TCallback<TIntrusivePtr<T>()> TFactory;

    explicit TLazyIntrusivePtr(TFactory factory = DefaultRefCountedFactory<T>())
        : Factory_(std::move(factory))
    { }

    T* Get() const noexcept
    {
        MaybeInitialize();
        return Value_.Get();
    }

    const TIntrusivePtr<T>& Value() const noexcept
    {
        MaybeInitialize();
        return Value_;
    }

    bool HasValue() const noexcept
    {
        return Initialized_.load();
    }

private:
    TLock Lock_;
    TFactory Factory_;
    mutable TIntrusivePtr<T> Value_;
    mutable std::atomic<bool> Initialized_ = {false};

    void MaybeInitialize() const noexcept
    {
        if (!HasValue()) {
            TGuard<TLock> guard(Lock_);
            if (!HasValue()) {
                NTracing::TNullTraceContextGuard guard;
                Value_ = Factory_.Run();
                Initialized_.store(true);
            }
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
