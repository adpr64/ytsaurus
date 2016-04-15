#pragma once

#include "public.h"

#include <yt/ytlib/api/public.h>

#include <yt/ytlib/table_client/public.h>

#include <yt/ytlib/chunk_client/public.h>

#include <yt/core/actions/signal.h>
#include <yt/core/actions/future.h>

#include <yt/core/misc/range.h>

#include <yt/core/yson/public.h>

namespace NYT {
namespace NTabletNode {

////////////////////////////////////////////////////////////////////////////////

struct IStore
    : public virtual TRefCounted
{
    virtual TStoreId GetId() const = 0;
    virtual TTablet* GetTablet() const = 0;

    virtual i64 GetUncompressedDataSize() const = 0;
    virtual i64 GetRowCount() const = 0;

    virtual EStoreType GetType() const = 0;

    virtual EStoreState GetStoreState() const = 0;
    virtual void SetStoreState(EStoreState state) = 0;

    //! Returns the number of bytes currently used by the store.
    virtual i64 GetMemoryUsage() const = 0;
    //! Raised whenever the store memory usage is changed.
    DECLARE_INTERFACE_SIGNAL(void(i64 delta), MemoryUsageUpdated);

    //! Serializes the synchronous part of the state.
    virtual void Save(TSaveContext& context) const = 0;
    //! Deserializes the synchronous part of the state.
    virtual void Load(TLoadContext& context) = 0;

    //! Serializes the asynchronous part of the state.
    virtual TCallback<void(TSaveContext&)> AsyncSave() = 0;
    //! Deserializes the asynchronous part of the state.
    virtual void AsyncLoad(TLoadContext& context) = 0;

    virtual void BuildOrchidYson(NYson::IYsonConsumer* consumer) = 0;

    virtual bool IsDynamic() const = 0;
    virtual IDynamicStorePtr AsDynamic() = 0;

    virtual bool IsChunk() const = 0;
    virtual IChunkStorePtr AsChunk() = 0;

    virtual bool IsSorted() const = 0;
    virtual ISortedStorePtr AsSorted() = 0;
    virtual TSortedDynamicStorePtr AsSortedDynamic() = 0;
    virtual TSortedChunkStorePtr AsSortedChunk() = 0;

    virtual bool IsOrdered() const = 0;
    virtual IOrderedStorePtr AsOrdered() = 0;
    virtual TOrderedDynamicStorePtr AsOrderedDynamic() = 0;
    virtual TOrderedChunkStorePtr AsOrderedChunk() = 0;
};

DEFINE_REFCOUNTED_TYPE(IStore)

////////////////////////////////////////////////////////////////////////////////

struct IDynamicStore
    : public virtual IStore
{
    virtual i64 GetValueCount() const = 0;
    virtual i64 GetLockCount() const = 0;

    virtual i64 GetPoolSize() const = 0;
    virtual i64 GetPoolCapacity() const = 0;

    virtual EStoreFlushState GetFlushState() const = 0;
    virtual void SetFlushState(EStoreFlushState state) = 0;
};

DEFINE_REFCOUNTED_TYPE(IDynamicStore)

////////////////////////////////////////////////////////////////////////////////

struct IChunkStore
    : public virtual IStore
{
    virtual void SetBackingStore(IDynamicStorePtr store) = 0;
    virtual bool HasBackingStore() const = 0;
    virtual IDynamicStorePtr GetBackingStore() = 0;

    virtual EStorePreloadState GetPreloadState() const = 0;
    virtual void SetPreloadState(EStorePreloadState state) = 0;

    virtual TFuture<void> GetPreloadFuture() const = 0;
    virtual void SetPreloadFuture(TFuture<void> future) = 0;

    virtual NChunkClient::IChunkReaderPtr GetChunkReader() = 0;

    virtual EInMemoryMode GetInMemoryMode() const = 0;
    virtual void SetInMemoryMode(EInMemoryMode mode) = 0;

    virtual void Preload(TInMemoryChunkDataPtr chunkData) = 0;

    virtual EStoreCompactionState GetCompactionState() const = 0;
    virtual void SetCompactionState(EStoreCompactionState state) = 0;
};

DEFINE_REFCOUNTED_TYPE(IChunkStore)

////////////////////////////////////////////////////////////////////////////////

struct ISortedStore
    : public virtual IStore
{
    virtual TPartition* GetPartition() const = 0;
    virtual void SetPartition(TPartition* partition) = 0;

    //! Returns the minimum key in the store, inclusive.
    virtual TOwningKey GetMinKey() const = 0;

    //! Returns the maximum key in the store, inclusive.
    virtual TOwningKey GetMaxKey() const = 0;

    //! Returns the minimum timestamp of changes recorded in the store.
    virtual TTimestamp GetMinTimestamp() const = 0;

    //! Returns the maximum timestamp of changes recorded in the store.
    virtual TTimestamp GetMaxTimestamp() const = 0;

    //! Creates a reader for the range from |lowerKey| (inclusive) to |upperKey| (exclusive).
    /*!
    *  If no matching row is found then |nullptr| might be returned.
    *
    *  The reader will be providing values filtered by |timestamp| and columns
    *  filtered by |columnFilter|.
    *
    *  This call is typically synchronous and fast but may occasionally yield.
    *
    *  Thread affinity: any
    */
    virtual NTableClient::IVersionedReaderPtr CreateReader(
        TOwningKey lowerKey,
        TOwningKey upperKey,
        TTimestamp timestamp,
        const TColumnFilter& columnFilter,
        const TWorkloadDescriptor& workloadDescriptor) = 0;

    //! Creates a reader for the set of |keys|.
    /*!
    *  If no matching row is found then |nullptr| might be returned.
    *
    *  The reader will be providing values filtered by |timestamp| and columns
    *  filtered by |columnFilter|.
    *
    *  This call is typically synchronous and fast but may occasionally yield.
    *
    *  Thread affinity: any
    */
    virtual NTableClient::IVersionedReaderPtr CreateReader(
        const TSharedRange<TKey>& keys,
        TTimestamp timestamp,
        const TColumnFilter& columnFilter,
        const TWorkloadDescriptor& workloadDescriptor) = 0;

    //! Checks that #transaction attempting to take locks indicated by #lockMask
    //! has no conflicts within the store. Throws on failure.
    /*!
     *  Thread affinity: any
     */
    virtual void CheckRowLocks(
        TUnversionedRow row,
        TTransaction* transaction,
        ui32 lockMask) = 0;
};

DEFINE_REFCOUNTED_TYPE(ISortedStore)

////////////////////////////////////////////////////////////////////////////////

struct IOrderedStore
    : public virtual IStore
{
    //! Creates a reader for the range from |lowerRowIndex| (inclusive) to |upperRowIndex| (exclusive).
    /*!
    *  If no matching row is found then |nullptr| might be returned.
    *
    *  This call is typically synchronous and fast but may occasionally yield.
    *
    *  Thread affinity: any
    */
    virtual NTableClient::ISchemafulReaderPtr CreateReader(
        i64 lowerRowIndex,
        i64 upperRowIndex,
        const NTableClient::TTableSchema& schema,
        const TWorkloadDescriptor& workloadDescriptor) = 0;
};

DEFINE_REFCOUNTED_TYPE(IOrderedStore)

////////////////////////////////////////////////////////////////////////////////

class TStoreIdFormatter
{
public:
    void operator()(TStringBuilder* builder, const IStorePtr& store) const;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletNode
} // namespace NYT
