#pragma once

#include "public.h"

#include <yt/ytlib/hive/proto/transaction_supervisor_service.pb.h>

#include <yt/ytlib/transaction_client/public.h>

#include <yt/core/misc/optional.h>

#include <yt/core/ytree/public.h>

namespace NYT::NHiveServer {

////////////////////////////////////////////////////////////////////////////////

struct ITransactionManager
    : public virtual TRefCounted
{
    virtual TFuture<void> GetReadyToPrepareTransactionCommit(
        const std::vector<TTransactionId>& prerequisiteTransactionIds,
        const std::vector<TCellId>& cellIdsToSyncWith) = 0;

    virtual void PrepareTransactionCommit(
        TTransactionId transactionId,
        bool persistent,
        TTimestamp prepareTimestamp,
        const std::vector<TTransactionId>& prerequisiteTransactionIds) = 0;

    virtual void PrepareTransactionAbort(
        TTransactionId transactionId,
        bool force) = 0;

    //! Once #PrepareTransactionCommit succeeded, #CommitTransaction cannot throw.
    virtual void CommitTransaction(
        TTransactionId transactionId,
        TTimestamp commitTimestamp) = 0;

    virtual void AbortTransaction(
        TTransactionId transactionId,
        bool force) = 0;

    virtual void PingTransaction(
        TTransactionId transactionId,
        bool pingAncestors) = 0;
};

DEFINE_REFCOUNTED_TYPE(ITransactionManager)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NHiveServer
