#pragma once

#include "common.h"
#include "cypress_state.h"
#include "../ytree/ytree.h"
#include "../ytree/ypath.h"

namespace NYT {
namespace NCypress {

////////////////////////////////////////////////////////////////////////////////

struct ICypressNodeProxy
    : public virtual TRefCountedBase
    , public virtual INode
{
    typedef TIntrusivePtr<ICypressNodeProxy> TPtr;

    virtual TTransactionId GetTransactionId() const = 0;
    virtual TNodeId GetNodeId() const = 0;

    virtual const ICypressNode& GetImpl() const = 0;
    virtual ICypressNode& GetMutableImpl() = 0;
};

////////////////////////////////////////////////////////////////////////////////

class TNodeFactory
    : public INodeFactory
{
public:
    TNodeFactory(
        TCypressState::TPtr state,
        const TTransactionId& transactionId)
        : State(state)
        , TransactionId(transactionId)
    {
        YASSERT(~state != NULL);
    }

    virtual IStringNode::TPtr CreateString()
    {
        return State->CreateStringNode(TransactionId);
    }

    virtual IInt64Node::TPtr CreateInt64()
    {
        return State->CreateInt64Node(TransactionId);
    }

    virtual IDoubleNode::TPtr CreateDouble()
    {
        return State->CreateDoubleNode(TransactionId);
    }

    virtual IMapNode::TPtr CreateMap()
    {
        return State->CreateMapNode(TransactionId);
    }

    virtual IListNode::TPtr CreateList()
    {
        YASSERT(false);
        return NULL;
    }

    virtual IEntityNode::TPtr CreateEntity()
    {
        YASSERT(false);
        return NULL;
    }

private:
    TCypressState::TPtr State;
    TTransactionId TransactionId;

};

////////////////////////////////////////////////////////////////////////////////

template <class IBase, class TImpl>
class TCypressNodeProxyBase
    : public TNodeBase
    , public ICypressNodeProxy
    , public virtual IBase
{
public:
    typedef TIntrusivePtr<TCypressNodeProxyBase> TPtr;

    TCypressNodeProxyBase(
        TCypressState::TPtr state,
        const TTransactionId& transactionId,
        const TNodeId& nodeId)
        : State(state)
        , TransactionId(transactionId)
        , NodeId(nodeId)
        , NodeFactory(state, transactionId)
    {
        YASSERT(~state != NULL);
    }

    INodeFactory* GetFactory() const
    {
        return &NodeFactory;
    }

    virtual TTransactionId GetTransactionId() const
    {
        return TransactionId;
    }

    virtual TNodeId GetNodeId() const
    {
        return NodeId;
    }

    virtual const ICypressNode& GetImpl() const
    {
        return this->GetTypedImpl();
    }

    virtual ICypressNode& GetMutableImpl()
    {
        return this->GetMutableTypedImpl();
    }

    virtual ICompositeNode::TPtr GetParent() const
    {
        return GetProxy<ICompositeNode>(GetImpl().ParentId());
    }

    virtual void SetParent(ICompositeNode::TPtr parent)
    {
        auto parentProxy = ToProxy(INode::TPtr(~parent));
        GetMutableImpl().ParentId() = parentProxy->GetNodeId();
    }

    virtual IMapNode::TPtr GetAttributes() const
    {
        return NULL;
    }

    virtual void SetAttributes(IMapNode::TPtr attributes)
    {
        UNUSED(attributes);
        YASSERT(false);
    }

protected:
    TCypressState::TPtr State;
    TTransactionId TransactionId;
    TNodeId NodeId;

    mutable TNodeFactory NodeFactory;

    const TImpl& GetTypedImpl() const
    {
        auto impl = State->FindNode(TBranchedNodeId(NodeId, TransactionId));
        if (impl == NULL) {
            impl = State->FindNode(TBranchedNodeId(NodeId, NullTransactionId));
        }
        YASSERT(impl != NULL);
        return *dynamic_cast<const TImpl*>(impl);
    }

    TImpl& GetMutableTypedImpl()
    {
        auto impl = State->FindNodeForUpdate(TBranchedNodeId(NodeId, TransactionId));
        if (impl == NULL) {
            impl = State->FindNodeForUpdate(TBranchedNodeId(NodeId, NullTransactionId));
        }
        YASSERT(impl != NULL);
        return *dynamic_cast<TImpl*>(impl);
    }

    template <class T>
    TIntrusivePtr<T> GetProxy(const TNodeId& nodeId) const
    {
        auto node = State->FindNode(nodeId, TransactionId);
        YASSERT(~node != NULL);
        return dynamic_cast<T*>(~node);
    }

    static typename ICypressNodeProxy::TPtr ToProxy(INode::TPtr node)
    {
        YASSERT(~node != NULL);
        return dynamic_cast<ICypressNodeProxy*>(~node);
    }
};

//////////////////////////////////////////////////////////////////////////////// 

template <class TValue, class IBase, class TImpl>
class TScalarNodeProxy
    : public TCypressNodeProxyBase<IBase, TImpl>
{
public:
    TScalarNodeProxy(
        TCypressState::TPtr state,
        const TTransactionId& transactionId,
        const TNodeId& nodeId)
        : TCypressNodeProxyBase<IBase, TImpl>(
            state,
            transactionId,
            nodeId)
    { }

    virtual TValue GetValue() const
    {
        return this->GetTypedImpl().Value();
    }

    virtual void SetValue(const TValue& value)
    {
        this->GetMutableTypedImpl().Value() = value;
    }
};

//////////////////////////////////////////////////////////////////////////////// 

#define DECLARE_TYPE_OVERRIDES(name) \
public: \
    virtual ENodeType GetType() const \
    { \
        return ENodeType::name; \
    } \
    \
    virtual TIntrusiveConstPtr<I ## name ## Node> As ## name() const \
    { \
        return const_cast<T ## name ## NodeProxy*>(this); \
    } \
    \
    virtual TIntrusivePtr<I ## name ## Node> As ## name() \
    { \
        return this; \
    }

////////////////////////////////////////////////////////////////////////////////

#define DECLARE_SCALAR_TYPE(name, type) \
    class T ## name ## NodeProxy \
        : public TScalarNodeProxy<type, I ## name ## Node, T ## name ## Node> \
    { \
        DECLARE_TYPE_OVERRIDES(name) \
    \
    public: \
        T ## name ## NodeProxy( \
            TCypressState::TPtr state, \
            const TTransactionId& transactionId, \
            const TNodeId& nodeId) \
            : TScalarNodeProxy<type, I ## name ## Node, T ## name ## Node>( \
                state, \
                transactionId, \
                nodeId) \
        { } \
    }; \
    \
    inline ICypressNodeProxy::TPtr T ## name ## Node::GetProxy( \
        TIntrusivePtr<TCypressState> state, \
        const TTransactionId& transactionId) const \
    { \
        return ~New<T ## name ## NodeProxy>(state, transactionId, Id.NodeId); \
    }


DECLARE_SCALAR_TYPE(String, Stroka)
DECLARE_SCALAR_TYPE(Int64, i64)
DECLARE_SCALAR_TYPE(Double, double)

#undef DECLARE_SCALAR_TYPE

////////////////////////////////////////////////////////////////////////////////

template <class IBase, class TImpl>
class TCompositeNodeProxyBase
    : public TCypressNodeProxyBase<IBase, TImpl>
{
protected:
    TCompositeNodeProxyBase(
        TCypressState::TPtr state,
        const TTransactionId& transactionId,
        const TNodeId& nodeId)
        : TCypressNodeProxyBase<IBase, TImpl>(
            state,
            transactionId,
            nodeId)
    { }

public:
    virtual TIntrusiveConstPtr<ICompositeNode> AsComposite() const
    {
        return const_cast<TCompositeNodeProxyBase*>(this);
    }

    virtual TIntrusivePtr<ICompositeNode> AsComposite()
    {
        return this;
    }
};

////////////////////////////////////////////////////////////////////////////////

class TMapNodeProxy
    : public TCompositeNodeProxyBase<IMapNode, TMapNode>
{
    DECLARE_TYPE_OVERRIDES(Map)

public:
    TMapNodeProxy(
        TCypressState::TPtr state,
        const TTransactionId& transactionId,
        const TNodeId& nodeId);

    virtual void Clear();
    virtual int GetChildCount() const;
    virtual yvector< TPair<Stroka, INode::TPtr> > GetChildren() const;
    virtual INode::TPtr FindChild(const Stroka& name) const;
    virtual bool AddChild(INode::TPtr child, const Stroka& name);
    virtual bool RemoveChild(const Stroka& name);
    virtual void ReplaceChild(INode::TPtr oldChild, INode::TPtr newChild);
    virtual void RemoveChild(INode::TPtr child);

    virtual TNavigateResult Navigate(TYPath path);
    virtual TSetResult Set(
        TYPath path,
        TYsonProducer::TPtr producer);
};

////////////////////////////////////////////////////////////////////////////////

#undef DECLARE_TYPE_OVERRIDES

////////////////////////////////////////////////////////////////////////////////

} // namespace NCypress
} // namespace NYT
