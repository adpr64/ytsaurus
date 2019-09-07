#pragma once

#include "private.h"

namespace NYT::NControllerAgent {

////////////////////////////////////////////////////////////////////////////////

int GetCurrentSnapshotVersion();
bool ValidateSnapshotVersion(int version);

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(ESnapshotVersion,
    // 19.7 starts here
    ((TransactionMirroring)           (300200))
);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NControllerAgent
