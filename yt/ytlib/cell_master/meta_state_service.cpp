#include "stdafx.h"
#include "meta_state_service.h"
#include "bootstrap.h"
#include "meta_state_facade.h"

namespace NYT {
namespace NCellMaster {

using namespace NMetaState;

////////////////////////////////////////////////////////////////////////////////

TMetaStateServiceBase::TMetaStateServiceBase(
    TBootstrap* bootstrap,
    const Stroka& serviceName,
    const Stroka& loggingCategory)
    : NRpc::TServiceBase(
        bootstrap->GetMetaStateFacade()->GetWrappedInvoker(),
        serviceName,
        loggingCategory)
    , Bootstrap(bootstrap)
{
    YCHECK(bootstrap);
}

void TMetaStateServiceBase::ValidateLeaderStatus()
{
    auto status = Bootstrap->GetMetaStateFacade()->GetManager()->GetStateStatus();
    if (status == NMetaState::EPeerStatus::Following) {
        ythrow NRpc::TServiceException(TError(
            NRpc::EErrorCode::Unavailable,
            "Not a leader"));
    }
    YCHECK(status == NMetaState::EPeerStatus::Leading);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NCellMaster
} // namespace NYT
