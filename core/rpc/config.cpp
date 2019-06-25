#include "config.h"

namespace NYT::NRpc {

using namespace NBus;
using namespace NYTree;
using namespace NConcurrency;

////////////////////////////////////////////////////////////////////////////////

const int TServiceConfig::DefaultAuthenticationQueueSizeLimit = 10000;

////////////////////////////////////////////////////////////////////////////////

const bool TMethodConfig::DefaultHeavy = false;
const int TMethodConfig::DefaultQueueSizeLimit = 10000;
const int TMethodConfig::DefaultConcurrencyLimit = 1000;
const NLogging::ELogLevel TMethodConfig::DefaultLogLevel = NLogging::ELogLevel::Debug;
const TDuration TMethodConfig::DefaultLoggingSuppressionTimeout = TDuration::Zero();
const TThroughputThrottlerConfigPtr TMethodConfig::DefaultLoggingSuppressionFailedRequestThrottler =
    New<TThroughputThrottlerConfig>(1000);

////////////////////////////////////////////////////////////////////////////////

const int TDispatcherConfig::DefaultHeavyPoolSize = 16;
const int TDispatcherConfig::DefaultCompressionPoolSize = 8;

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NRpc
