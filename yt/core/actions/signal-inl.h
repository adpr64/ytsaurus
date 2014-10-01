#include <core/misc/ref.h>

#ifndef SIGNAL_INL_H_
#error "Direct inclusion of this file is not allowed, include signal.h"
#endif
#undef SIGNAL_INL_H_

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

template <class... TArgs>
void TCallbackList<void(TArgs...)>::Subscribe(const TCallback& callback)
{
    TGuard<TSpinLock> guard(SpinLock_);
    Callbacks_.push_back(callback);
}

template <class... TArgs>
bool TCallbackList<void(TArgs...)>::Unsubscribe(const TCallback& callback)
{
    TGuard<TSpinLock> guard(SpinLock_);
    for (auto it = Callbacks_.begin(); it != Callbacks_.end(); ++it) {
        if (*it == callback) {
            Callbacks_.erase(it);
            return true;
        }
    }
    return false;
}

template <class... TArgs>
void TCallbackList<void(TArgs...)>::Clear()
{
    TGuard<TSpinLock> guard(SpinLock_);
    Callbacks_.clear();
}

template <class... TArgs>
void TCallbackList<void(TArgs...)>::Fire(const TArgs&... args) const
{
    TGuard<TSpinLock> guard(SpinLock_);

    if (Callbacks_.empty())
        return;

    std::vector<TCallback> callbacks(Callbacks_);
    guard.Release();

    for (const auto& callback : callbacks) {
        callback.Run(args...);
    }
}

template <class... TArgs>
void TCallbackList<void(TArgs...)>::FireAndClear(const TArgs&... args) const
{
    TGuard<TSpinLock> guard(SpinLock_);

    if (Callbacks_.empty())
        return;

    std::vector<TCallback> callbacks;
    swap(callbacks, Callbacks_);
    guard.Release();

    for (const auto& callback : callbacks) {
        callback.Run(args...);
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
