#pragma once

#include <library/cpp/yt/small_containers/compact_vector.h>

#include "public.h"

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

//! A helper structure for maintaining a monotonic counter and
//! estimating its average rate over a set of configured time windows
//! using EMA (exponential moving average) technique.
struct TEmaCounter
{
    //! Current value of the counter.
    i64 Count = 0;
    //! Last update time.
    std::optional<TInstant> Timestamp;
    //! Rate (measured in units per second) calculated based on the last update,
    //! i.e. #Count delta divided by the time delta measured in seconds
    //! according to the last update.
    double ImmediateRate = 0.0;

    //! A typical number of configured time windows.
    static constexpr int TypicalWindowCount = 2;
    using TWindowDurations = TCompactVector<TDuration, TypicalWindowCount>;
    using TWindowRates = TCompactVector<double, TypicalWindowCount>;

    //! Durations of configured time windows.
    TWindowDurations WindowDurations;
    //! Estimates of a rate over corresponding time windows.
    TWindowRates WindowRates;

    explicit TEmaCounter(TWindowDurations windowDurations);

    //! Set new value of counter, optionally providing a current timestamp.
    void Update(i64 newCount, TInstant newTimestamp = TInstant::Now());
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
