#pragma once

#include "object_service_proxy.h"

#include <ytlib/cell_master/public.h>
#include <ytlib/cell_master/meta_state_service.h>

namespace NYT {
namespace NObjectServer {

////////////////////////////////////////////////////////////////////////////////

class TObjectService
    : public NCellMaster::TMetaStateServiceBase
{
public:
    typedef TIntrusivePtr<TObjectService> TPtr;

    //! Creates an instance.
    TObjectService(NCellMaster::TBootstrap* bootstrap);

private:
    typedef TObjectService TThis;
    class TExecuteSession;

    DECLARE_RPC_SERVICE_METHOD(NProto, Execute);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NObjectServer
} // namespace NYT
