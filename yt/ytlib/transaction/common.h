#pragma once

#include "../misc/common.h"
#include "../misc/ptr.h"
#include "../misc/guid.h"

#include "../logging/log.h"

namespace NYT {
namespace NTransaction {

////////////////////////////////////////////////////////////////////////////////

extern NLog::TLogger TransactionLogger;

////////////////////////////////////////////////////////////////////////////////

typedef TGuid TTransactionId;

////////////////////////////////////////////////////////////////////////////////

} // namespace NTransaction
} // namespace NYT

