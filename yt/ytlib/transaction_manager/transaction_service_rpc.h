#pragma once

#include "common.h"
#include "transaction_service_rpc.pb.h"

#include "../rpc/service.h"
#include "../rpc/client.h"

namespace NYT {
namespace NTransaction {

////////////////////////////////////////////////////////////////////////////////

class TTransactionServiceProxy
    : public NRpc::TProxyBase
{
public:
    typedef TIntrusivePtr<TTransactionServiceProxy> TPtr;

    RPC_DECLARE_PROXY(TransactionService,
        ((NoSuchTransaction)(1))
    );

    TTransactionServiceProxy(NRpc::IChannel::TPtr channel)
        : TProxyBase(channel, GetServiceName())
    { }

    RPC_PROXY_METHOD(NProto, StartTransaction);
    RPC_PROXY_METHOD(NProto, CommitTransaction);
    RPC_PROXY_METHOD(NProto, AbortTransaction);
    RPC_PROXY_METHOD(NProto, RenewTransactionLease);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NTransaction
} // namespace NYT
