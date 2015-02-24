#pragma once

#include "public.h"
#include "config.h"

#include <core/misc/nullable.h>

#include <core/ytree/permission.h>

#include <core/actions/future.h>

#include <server/cell_node/public.h>

namespace NYT {
namespace NTabletNode {

////////////////////////////////////////////////////////////////////////////////

//! A simple RAII guard for setting the authenticated user.
/*!
 *  \see #TSecurityManager::SetAuthenticatedUser
 *  \see #TSecurityManager::ResetAuthenticatedUser
 */
class TAuthenticatedUserGuard
    : private TNonCopyable
{
public:
    TAuthenticatedUserGuard(TSecurityManagerPtr securityManager, const TNullable<Stroka>& maybeUser);
    ~TAuthenticatedUserGuard();

private:
    const TSecurityManagerPtr SecurityManager_;
    const bool IsNull_;

};

////////////////////////////////////////////////////////////////////////////////

class TSecurityManager
    : public TRefCounted
{
public:
    TSecurityManager(
        TSecurityManagerConfigPtr config,
        NCellNode::TBootstrap* bootstrap);
    ~TSecurityManager();

    void SetAuthenticatedUser(const Stroka& user);
    void ResetAuthenticatedUser();
    TNullable<Stroka> GetAuthenticatedUser();

    TFuture<void> CheckPermission(
        TTabletSnapshotPtr tabletSnapshot,
        NYTree::EPermission permission);

    void ValidatePermission(
        TTabletSnapshotPtr tabletSnapshot,
        NYTree::EPermission permission);

private:
    class TImpl;
    const TIntrusivePtr<TImpl> Impl_;

};

DEFINE_REFCOUNTED_TYPE(TSecurityManager)

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletNode
} // namespace NYT
