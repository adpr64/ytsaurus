#pragma once

#include "common.h"
#include "monitoring_manager.h"

#include "../ytree/ypath_service.h"

namespace NYT {
namespace NMonitoring {

////////////////////////////////////////////////////////////////////////////////

NYTree::TYPathServiceProvider::TPtr CreateMonitoringProvider(
    TMonitoringManager* monitoringManager);

////////////////////////////////////////////////////////////////////////////////

} // namespace NMonitoring
} // namespace NYT
