#pragma once

#include "public.h"

#include <yt/ytlib/event_log/event_log.h>

namespace NYT::NScheduler {

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(ELogEventType,
    (SchedulerStarted)
    (MasterConnected)
    (MasterDisconnected)
    (JobStarted)
    (JobCompleted)
    (JobFailed)
    (JobAborted)
    (OperationStarted)
    (OperationCompleted)
    (OperationFailed)
    (OperationAborted)
    (OperationPrepared)
    (OperationMaterialized)
    (OperationBannedInTree)
    (FairShareInfo)
    (ClusterInfo)
    (NodesInfo)
    (PoolsInfo)
    (RuntimeParametersInfo)
);

////////////////////////////////////////////////////////////////////////////////

struct IEventLogHost
{
    virtual ~IEventLogHost() = default;

    virtual NEventLog::TFluentLogEvent LogEventFluently(ELogEventType eventType) = 0;
    virtual NEventLog::TFluentLogEvent LogEventFluently(ELogEventType eventType, TInstant now) = 0;
};

class TEventLogHostBase
    : public virtual IEventLogHost
{
public:
    virtual NEventLog::TFluentLogEvent LogEventFluently(ELogEventType eventType) override;
    virtual NEventLog::TFluentLogEvent LogEventFluently(ELogEventType eventType, TInstant now) override;

protected:
    NEventLog::TFluentLogEvent LogEventFluently(
        ELogEventType eventType,
        NYson::IYsonConsumer* eventLogConsumer,
        const NLogging::TLogger* eventLogger,
        TInstant now);

    virtual NYson::IYsonConsumer* GetEventLogConsumer() = 0;
    virtual const NLogging::TLogger* GetEventLogger() = 0;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NScheduler
