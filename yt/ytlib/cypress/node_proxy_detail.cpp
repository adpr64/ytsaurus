#include "stdafx.h"
#include "node_proxy_detail.h"

namespace NYT {
namespace NCypress {

using namespace NYTree;
using namespace NRpc;

////////////////////////////////////////////////////////////////////////////////

TNodeFactory::TNodeFactory(
    TCypressManager* cypressManager,
    const TTransactionId& transactionId)
    : CypressManager(cypressManager)
    , TransactionId(transactionId)
{
    YASSERT(cypressManager != NULL);
}

IStringNode::TPtr TNodeFactory::CreateString()
{
    return CypressManager->CreateStringNodeProxy(TransactionId);
}

IInt64Node::TPtr TNodeFactory::CreateInt64()
{
    return CypressManager->CreateInt64NodeProxy(TransactionId);
}

IDoubleNode::TPtr TNodeFactory::CreateDouble()
{
    return CypressManager->CreateDoubleNodeProxy(TransactionId);
}

IMapNode::TPtr TNodeFactory::CreateMap()
{
    return CypressManager->CreateMapNodeProxy(TransactionId);
}

IListNode::TPtr TNodeFactory::CreateList()
{
    return CypressManager->CreateListNodeProxy(TransactionId);
}

IEntityNode::TPtr TNodeFactory::CreateEntity()
{
    ythrow yexception() << "Entity nodes cannot be created inside Cypress";
}

////////////////////////////////////////////////////////////////////////////////

TMapNodeProxy::TMapNodeProxy(
    INodeTypeHandler* typeHandler,
    TCypressManager* cypressManager,
    const TTransactionId& transactionId,
    const TNodeId& nodeId)
    : TCompositeNodeProxyBase(
        typeHandler,
        cypressManager,
        transactionId,
        nodeId)
{ }

void TMapNodeProxy::Clear()
{
    EnsureLocked();
    
    auto& impl = GetTypedImplForUpdate();

    FOREACH(const auto& pair, impl.NameToChild()) {
        auto& childImpl = GetImplForUpdate(pair.Second());
        DetachChild(childImpl);
    }

    impl.NameToChild().clear();
    impl.ChildToName().clear();
}

int TMapNodeProxy::GetChildCount() const
{
    return GetTypedImpl().NameToChild().ysize();
}

yvector< TPair<Stroka, INode::TPtr> > TMapNodeProxy::GetChildren() const
{
    yvector< TPair<Stroka, INode::TPtr> > result;
    const auto& map = GetTypedImpl().NameToChild();
    result.reserve(map.ysize());
    FOREACH (const auto& pair, map) {
        result.push_back(MakePair(
            pair.First(),
            GetProxy<INode>(pair.Second())));
    }
    return result;
}

INode::TPtr TMapNodeProxy::FindChild(const Stroka& name) const
{
    const auto& map = GetTypedImpl().NameToChild();
    auto it = map.find(name);
    return it == map.end() ? NULL : GetProxy<INode>(it->Second());
}

bool TMapNodeProxy::AddChild(INode::TPtr child, const Stroka& name)
{
    YASSERT(!name.empty());

    EnsureLocked();

    auto& impl = GetTypedImplForUpdate();

    auto* childProxy = ToProxy(~child);
    auto childId = childProxy->GetNodeId();

    if (!impl.NameToChild().insert(MakePair(name, childId)).Second())
        return false;

    auto& childImpl = childProxy->GetImplForUpdate();
    YVERIFY(impl.ChildToName().insert(MakePair(childId, name)).Second());
    AttachChild(childImpl);

    return true;
}

bool TMapNodeProxy::RemoveChild(const Stroka& name)
{
    EnsureLocked();

    auto& impl = GetTypedImplForUpdate();

    auto it = impl.NameToChild().find(name);
    if (it == impl.NameToChild().end())
        return false;

    const auto& childId = it->Second();
    auto childProxy = GetProxy<ICypressNodeProxy>(childId);
    auto& childImpl = childProxy->GetImplForUpdate();
    
    impl.NameToChild().erase(it);
    YVERIFY(impl.ChildToName().erase(childId) == 1);

    DetachChild(childImpl);
    
    return true;
}

void TMapNodeProxy::RemoveChild(INode::TPtr child)
{
    EnsureLocked();

    auto& impl = GetTypedImplForUpdate();
    
    auto* childProxy = ToProxy(~child);
    auto& childImpl = childProxy->GetImplForUpdate();

    auto it = impl.ChildToName().find(childProxy->GetNodeId());
    YASSERT(it != impl.ChildToName().end());

    Stroka name = it->Second();
    impl.ChildToName().erase(it);
    YVERIFY(impl.NameToChild().erase(name) == 1);

    DetachChild(childImpl);
}

void TMapNodeProxy::ReplaceChild(INode::TPtr oldChild, INode::TPtr newChild)
{
    if (oldChild == newChild)
        return;

    EnsureLocked();

    auto& impl = GetTypedImplForUpdate();

    auto* oldChildProxy = ToProxy(~oldChild);
    auto& oldChildImpl = oldChildProxy->GetImplForUpdate();
    auto* newChildProxy = ToProxy(~newChild);
    auto& newChildImpl = newChildProxy->GetImplForUpdate();

    auto it = impl.ChildToName().find(oldChildProxy->GetNodeId());
    YASSERT(it != impl.ChildToName().end());

    Stroka name = it->Second();

    impl.ChildToName().erase(it);
    DetachChild(oldChildImpl);

    impl.NameToChild()[name] = newChildProxy->GetNodeId();
    YVERIFY(impl.ChildToName().insert(MakePair(newChildProxy->GetNodeId(), name)).Second());
    AttachChild(newChildImpl);
}

Stroka TMapNodeProxy::GetChildKey(INode* child)
{
    auto& impl = GetTypedImpl();
    
    auto* childProxy = ToProxy(child);

    auto it = impl.ChildToName().find(childProxy->GetNodeId());
    YASSERT(it != impl.ChildToName().end());

    return it->Second();
}

void TMapNodeProxy::DoInvoke(NRpc::IServiceContext* context)
{
    if (TMapNodeMixin::DoInvoke(context)) {
        // The verb is already handled.
    } else {
        TCompositeNodeProxyBase::DoInvoke(context);
    }
}

void TMapNodeProxy::CreateRecursive(TYPath path, INode* value)
{
    TMapNodeMixin::SetRecursive(path, value);
}

IYPathService::TResolveResult TMapNodeProxy::ResolveRecursive(TYPath path, const Stroka& verb)
{
    return TMapNodeMixin::ResolveRecursive(path, verb);
}

void TMapNodeProxy::SetRecursive(TYPath path, TReqSet* request, TRspSet* response, TCtxSet::TPtr context)
{
    UNUSED(response);

    TMapNodeMixin::SetRecursive(path, request);
    context->Reply();
}


////////////////////////////////////////////////////////////////////////////////

TListNodeProxy::TListNodeProxy(
    INodeTypeHandler* typeHandler,
    TCypressManager* cypressManager,
    const TTransactionId& transactionId,
    const TNodeId& nodeId)
    : TCompositeNodeProxyBase(
        typeHandler,
        cypressManager,
        transactionId,
        nodeId)
{ }

void TListNodeProxy::Clear()
{
    EnsureLocked();

    auto& impl = GetTypedImplForUpdate();

    FOREACH(auto& nodeId, impl.IndexToChild()) {
        auto& childImpl = GetImplForUpdate(nodeId);
        DetachChild(childImpl);
    }

    impl.IndexToChild().clear();
    impl.ChildToIndex().clear();
}

int TListNodeProxy::GetChildCount() const
{
    return GetTypedImpl().IndexToChild().ysize();
}

yvector<INode::TPtr> TListNodeProxy::GetChildren() const
{
    yvector<INode::TPtr> result;
    const auto& list = GetTypedImpl().IndexToChild();
    result.reserve(list.ysize());
    FOREACH (const auto& nodeId, list) {
        result.push_back(GetProxy<INode>(nodeId));
    }
    return result;
}

INode::TPtr TListNodeProxy::FindChild(int index) const
{
    const auto& list = GetTypedImpl().IndexToChild();
    return index >= 0 && index < list.ysize() ? GetProxy<INode>(list[index]) : NULL;
}

void TListNodeProxy::AddChild(INode::TPtr child, int beforeIndex /*= -1*/)
{
    EnsureLocked();

    auto& impl = GetTypedImplForUpdate();
    auto& list = impl.IndexToChild();

    auto* childProxy = ToProxy(~child);
    auto childId = childProxy->GetNodeId();
    auto& childImpl = childProxy->GetImplForUpdate();

    if (beforeIndex < 0) {
        YVERIFY(impl.ChildToIndex().insert(MakePair(childId, list.ysize())).Second());
        list.push_back(childId);
    } else {
        YVERIFY(impl.ChildToIndex().insert(MakePair(childId, beforeIndex)).Second());
        list.insert(list.begin() + beforeIndex, childId);
    }

    AttachChild(childImpl);
}

bool TListNodeProxy::RemoveChild(int index)
{
    EnsureLocked();

    auto& impl = GetTypedImplForUpdate();
    auto& list = impl.IndexToChild();

    if (index < 0 || index >= list.ysize())
        return false;

    auto childProxy = GetProxy<ICypressNodeProxy>(list[index]);
    auto& childImpl = childProxy->GetImplForUpdate();

    list.erase(list.begin() + index);
    YVERIFY(impl.ChildToIndex().erase(childProxy->GetNodeId()));
    DetachChild(childImpl);

    return true;
}

void TListNodeProxy::RemoveChild(INode::TPtr child)
{
    EnsureLocked();

    auto& impl = GetTypedImplForUpdate();
    auto& list = impl.IndexToChild();
    
    auto childProxy = ToProxy(~child);
    auto& childImpl = childProxy->GetImplForUpdate();

    auto it = impl.ChildToIndex().find(childProxy->GetNodeId());
    YASSERT(it != impl.ChildToIndex().end());

    int index = it->Second();
    impl.ChildToIndex().erase(it);
    list.erase(list.begin() + index);
    DetachChild(childImpl);
}

void TListNodeProxy::ReplaceChild(INode::TPtr oldChild, INode::TPtr newChild)
{
    if (oldChild == newChild)
        return;

    EnsureLocked();

    auto& impl = GetTypedImplForUpdate();

    auto* oldChildProxy = ToProxy(~oldChild);
    auto& oldChildImpl = oldChildProxy->GetImplForUpdate();
    auto* newChildProxy = ToProxy(~newChild);
    auto& newChildImpl = newChildProxy->GetImplForUpdate();

    auto it = impl.ChildToIndex().find(oldChildProxy->GetNodeId());
    YASSERT(it != impl.ChildToIndex().end());

    int index = it->Second();

    DetachChild(oldChildImpl);

    impl.IndexToChild()[index] = newChildProxy->GetNodeId();
    impl.ChildToIndex().erase(it);
    YVERIFY(impl.ChildToIndex().insert(MakePair(newChildProxy->GetNodeId(), index)).Second());
    AttachChild(newChildImpl);
}

int TListNodeProxy::GetChildIndex(INode* child)
{
    auto& impl = GetTypedImplForUpdate();

    auto childProxy = ToProxy(child);

    auto it = impl.ChildToIndex().find(childProxy->GetNodeId());
    YASSERT(it != impl.ChildToIndex().end());

    return it->Second();
}

void TListNodeProxy::CreateRecursive(TYPath path, INode* value)
{
    TListNodeMixin::SetRecursive(path, value);
}

IYPathService::TResolveResult TListNodeProxy::ResolveRecursive(TYPath path, const Stroka& verb)
{
    return TListNodeMixin::ResolveRecursive(path, verb);
}

void TListNodeProxy::SetRecursive(TYPath path, TReqSet* request, TRspSet* response, TCtxSet::TPtr context)
{
    UNUSED(response);

    TListNodeMixin::SetRecursive(path, request);
    context->Reply();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NCypress
} // namespace NYT

