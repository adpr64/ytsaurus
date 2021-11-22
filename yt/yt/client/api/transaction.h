#pragma once

#include "client.h"

#include <yt/yt/client/table_client/unversioned_row.h>
#include <yt/yt/client/table_client/versioned_row.h>

#include <yt/yt/client/tablet_client/public.h>

#include <yt/yt/core/actions/signal.h>

namespace NYT::NApi {

////////////////////////////////////////////////////////////////////////////////

struct TTransactionFlushResult
{
    std::vector<NElection::TCellId> ParticipantCellIds;
};

//! Either a write or delete.
struct TRowModification
{
    //! Discriminates between writes and deletes.
    ERowModificationType Type;
    //! Either a row (for write; versioned or unversioned) or a key (for delete; always unversioned).
    NTableClient::TTypeErasedRow Row;
    //! Locks.
    NTableClient::TLockMask Locks;
};

struct TModifyRowsOptions
{
    //! If this happens to be a modification of a replicated table,
    //! controls if at least one sync replica is required.
    bool RequireSyncReplica = true;

    //! For chaos replicated tables indicates if it is necessary to explore other replicas.
    bool TopmostTransaction = true;

    //! For chaos replicas pass replication card to ensure that all data is sent using same meta info.
    NChaosClient::TReplicationCardPtr ReplicationCard;

    //! For writes to replicas, this is the id of the replica at the upstream cluster.
    NTabletClient::TTableReplicaId UpstreamReplicaId;

    //! ModifyRows requests are sent asynchronously. Sequential numbering is
    //! required to restore their order. (This parameter is only used by RPC proxy client.)
    std::optional<i64> SequenceNumber;
};

////////////////////////////////////////////////////////////////////////////////

//! Represents a client-controlled transaction.
/*
 *  Transactions are created by calling IClientBase::Transaction.
 *
 *  For some table operations (e.g. #WriteRows), the transaction instance
 *  buffers all modifications and flushes them during #Commit. This, in
 *  particular, explains why these methods return |void|.
 *
 *  Thread affinity: any
 */
struct ITransaction
    : public virtual IClientBase
{
    virtual IClientPtr GetClient() const = 0;
    virtual NTransactionClient::ETransactionType GetType() const = 0;
    virtual NTransactionClient::TTransactionId GetId() const = 0;
    virtual NTransactionClient::TTimestamp GetStartTimestamp() const = 0;
    virtual NTransactionClient::EAtomicity GetAtomicity() const = 0;
    virtual NTransactionClient::EDurability GetDurability() const = 0;
    virtual TDuration GetTimeout() const = 0;

    virtual TFuture<void> Ping(const NApi::TTransactionPingOptions& options = {}) = 0;
    virtual TFuture<TTransactionCommitResult> Commit(const TTransactionCommitOptions& options = TTransactionCommitOptions()) = 0;
    virtual TFuture<void> Abort(const TTransactionAbortOptions& options = TTransactionAbortOptions()) = 0;
    virtual void Detach() = 0;
    virtual TFuture<TTransactionFlushResult> Flush() = 0;
    virtual void RegisterAlienTransaction(const ITransactionPtr& transaction) = 0;

    using TCommittedHandlerSignature = void();
    using TCommittedHandler = TCallback<TCommittedHandlerSignature>;
    DECLARE_INTERFACE_SIGNAL(TCommittedHandlerSignature, Committed);

    using TAbortedHandlerSignature = void(const TError& error);
    using TAbortedHandler = TCallback<TAbortedHandlerSignature>;
    DECLARE_INTERFACE_SIGNAL(TAbortedHandlerSignature, Aborted);

    // Tables

    void WriteRows(
        const NYPath::TYPath& path,
        NTableClient::TNameTablePtr nameTable,
        TSharedRange<NTableClient::TUnversionedRow> rows,
        const TModifyRowsOptions& options = TModifyRowsOptions());

    void WriteRows(
        const NYPath::TYPath& path,
        NTableClient::TNameTablePtr nameTable,
        TSharedRange<NTableClient::TVersionedRow> rows,
        const TModifyRowsOptions& options = TModifyRowsOptions());

    void DeleteRows(
        const NYPath::TYPath& path,
        NTableClient::TNameTablePtr nameTable,
        TSharedRange<NTableClient::TLegacyKey> keys,
        const TModifyRowsOptions& options = TModifyRowsOptions());

    void LockRows(
        const NYPath::TYPath& path,
        NTableClient::TNameTablePtr nameTable,
        TSharedRange<NTableClient::TLegacyKey> keys,
        NTableClient::TLockMask lockMask);

    void LockRows(
        const NYPath::TYPath& path,
        NTableClient::TNameTablePtr nameTable,
        TSharedRange<NTableClient::TLegacyKey> keys,
        NTableClient::ELockType lockType = NTableClient::ELockType::SharedStrong);

    void LockRows(
        const NYPath::TYPath& path,
        NTableClient::TNameTablePtr nameTable,
        TSharedRange<NTableClient::TLegacyKey> keys,
        const std::vector<TString>& locks,
        NTableClient::ELockType lockType = NTableClient::ELockType::SharedStrong);

    virtual void ModifyRows(
        const NYPath::TYPath& path,
        NTableClient::TNameTablePtr nameTable,
        TSharedRange<TRowModification> modifications,
        const TModifyRowsOptions& options = TModifyRowsOptions()) = 0;
};

DEFINE_REFCOUNTED_TYPE(ITransaction)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi

