#pragma once

#include "public.h"

#include <core/logging/log.h>

namespace NYT {
namespace NQueryClient {

////////////////////////////////////////////////////////////////////////////////

extern NLog::TLogger QueryClientLogger;

NLog::TLogger BuildLogger(const TConstQueryPtr& query);

////////////////////////////////////////////////////////////////////////////////

} // namespace NQueryClient
} // namespace NYT

