#include "transaction.h"
#include "automaton.h"
#include "sorted_dynamic_store.h"
#include "tablet.h"
#include "tablet_manager.h"
#include "tablet_slot.h"

#include <yt/yt/server/lib/hydra_common/composite_automaton.h>

#include <yt/yt/ytlib/tablet_client/public.h>

#include <yt/yt/client/table_client/versioned_row.h>

#include <yt/yt/client/transaction_client/helpers.h>

#include <yt/yt/client/object_client/helpers.h>

#include <yt/yt/core/misc/compact_vector.h>

namespace NYT::NTabletNode {

using namespace NHiveServer;
using namespace NObjectClient;
using namespace NTableClient;
using namespace NTabletClient;
using namespace NTransactionClient;

////////////////////////////////////////////////////////////////////////////////

TTransactionWriteRecord::TTransactionWriteRecord(
    TTabletId tabletId,
    TSharedRef data,
    int rowCount,
    size_t dataWeight,
    const TSyncReplicaIdList& syncReplicaIds)
    : TabletId(tabletId)
    , Data(std::move(data))
    , RowCount(rowCount)
    , DataWeight(dataWeight)
    , SyncReplicaIds(syncReplicaIds)
{ }

void TTransactionWriteRecord::Save(TSaveContext& context) const
{
    using NYT::Save;
    Save(context, TabletId);
    Save(context, Data);
    Save(context, RowCount);
    Save(context, DataWeight);
    Save(context, SyncReplicaIds);
}

void TTransactionWriteRecord::Load(TLoadContext& context)
{
    using NYT::Load;
    Load(context, TabletId);
    Load(context, Data);
    Load(context, RowCount);
    Load(context, DataWeight);
    Load(context, SyncReplicaIds);
}

i64 TTransactionWriteRecord::GetByteSize() const
{
    return Data.Size();
}

i64 GetWriteLogRowCount(const TTransactionWriteLog& writeLog)
{
    i64 result = 0;
    for (const auto& entry : writeLog) {
        result += entry.RowCount;
    }
    return result;
}

////////////////////////////////////////////////////////////////////////////////

TTransaction::TTransaction(TTransactionId id)
    : TTransactionBase(id)
{ }

void TTransaction::Save(TSaveContext& context) const
{
    TTransactionBase::Save(context);

    using NYT::Save;

    YT_VERIFY(!Transient_);
    Save(context, Foreign_);
    Save(context, Timeout_);
    Save(context, GetPersistentState());
    Save(context, StartTimestamp_);
    Save(context, GetPersistentPrepareTimestamp());
    Save(context, CommitTimestamp_);
    Save(context, PrepareRevision_);
    Save(context, PersistentSignature_);
    Save(context, PersistentGeneration_);
    Save(context, RowsPrepared_);
    Save(context, AuthenticationIdentity_.User);
    Save(context, AuthenticationIdentity_.UserTag);
    Save(context, CommitTimestampClusterTag_);
    Save(context, SerializationForced_);
    Save(context, TabletsToUpdateReplicationProgress_);
}

void TTransaction::Load(TLoadContext& context)
{
    TTransactionBase::Load(context);

    using NYT::Load;

    Transient_ = false;
    Load(context, Foreign_);
    Load(context, Timeout_);
    SetPersistentState(Load<ETransactionState>(context));
    Load(context, StartTimestamp_);
    Load(context, PrepareTimestamp_);
    Load(context, CommitTimestamp_);
    // COMPAT(ifsmirnov)
    if (context.GetVersion() >= ETabletReign::DiscardStoresRevision) {
        Load(context, PrepareRevision_);
    }
    Load(context, PersistentSignature_);
    TransientSignature_ = PersistentSignature_;
    // COMPAT(max42)
    if (context.GetVersion() >= ETabletReign::WriteGenerations) {
        Load(context, PersistentGeneration_);
        TransientGeneration_ = PersistentGeneration_;
    }
    Load(context, RowsPrepared_);
    Load(context, AuthenticationIdentity_.User);
    Load(context, AuthenticationIdentity_.UserTag);
    // COMPAT(savrus)
    if (context.GetVersion() >= ETabletReign::SerializeReplicationProgress) {
        Load(context, CommitTimestampClusterTag_);
        Load(context, SerializationForced_);
        Load(context, TabletsToUpdateReplicationProgress_);
    }
}

TCallback<void(TSaveContext&)> TTransaction::AsyncSave()
{
    return BIND([
        immediateLockedWriteLogSnapshot = ImmediateLockedWriteLog_.MakeSnapshot(),
        immediateLocklessWriteLogSnapshot = ImmediateLocklessWriteLog_.MakeSnapshot(),
        delayedLocklessWriteLogSnapshot = DelayedLocklessWriteLog_.MakeSnapshot()
    ] (TSaveContext& context) {
        using NYT::Save;
        Save(context, immediateLockedWriteLogSnapshot);
        Save(context, immediateLocklessWriteLogSnapshot);
        Save(context, delayedLocklessWriteLogSnapshot);
    });
}

void TTransaction::AsyncLoad(TLoadContext& context)
{
    using NYT::Load;
    Load(context, ImmediateLockedWriteLog_);
    Load(context, ImmediateLocklessWriteLog_);
    Load(context, DelayedLocklessWriteLog_);
}

TFuture<void> TTransaction::GetFinished() const
{
    return Finished_;
}

void TTransaction::SetFinished()
{
    Finished_.Set();
}

void TTransaction::ResetFinished()
{
    Finished_.Set();
    Finished_ = NewPromise<void>();
}

TTimestamp TTransaction::GetPersistentPrepareTimestamp() const
{
    switch (GetTransientState()) {
        case ETransactionState::TransientCommitPrepared:
            return NullTimestamp;
        default:
            return PrepareTimestamp_;
    }
}

TInstant TTransaction::GetStartTime() const
{
    return TimestampToInstant(StartTimestamp_).first;
}

bool TTransaction::IsSerializationNeeded() const
{
    return !DelayedLocklessWriteLog_.Empty() || !TabletsToUpdateReplicationProgress_.empty() || SerializationForced_;
}

TCellTag TTransaction::GetCellTag() const
{
    return CellTagFromId(GetId());
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletNode

