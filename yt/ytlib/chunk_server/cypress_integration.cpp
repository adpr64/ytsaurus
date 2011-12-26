#include "stdafx.h"
#include "cypress_integration.h"

#include "../misc/string.h"
#include "../ytree/virtual.h"
#include "../ytree/fluent.h"
#include "../cypress/virtual.h"
#include "../cypress/node_proxy_detail.h"
#include "../cypress/cypress_ypath_proxy.h"

namespace NYT {
namespace NChunkServer {

using namespace NYTree;
using namespace NCypress;
using namespace NChunkClient;
using namespace NMetaState;

////////////////////////////////////////////////////////////////////////////////

class TVirtualChunkMap
    : public TVirtualMapBase
{
public:
    DECLARE_ENUM(EChunkFilter,
        (All)
        (Lost)
        (OverReplicated)
        (UnderReplicated)
    );

    TVirtualChunkMap(TChunkManager* chunkManager, EChunkFilter filter)
        : ChunkManager(chunkManager)
        , Filter(filter)
    { }

private:
    TChunkManager::TPtr ChunkManager;
    EChunkFilter Filter;

    const yhash_set<TChunkId>& GetFilteredChunkIds() const
    {
        switch (Filter) {
            case EChunkFilter::Lost:
                return ChunkManager->LostChunkIds();
            case EChunkFilter::OverReplicated:
                return ChunkManager->OverReplicatedChunkIds();
            case EChunkFilter::UnderReplicated:
                return ChunkManager->UnderReplicatedChunkIds();
            default:
                YUNREACHABLE();
        }
    }

    bool CheckFilter(const TChunkId& chunkId) const
    {
        if (Filter == EChunkFilter::All)
            return true;

        const auto& chunkIds = GetFilteredChunkIds();
        return chunkIds.find(chunkId) != chunkIds.end();
    }

    virtual yvector<Stroka> GetKeys(size_t sizeLimit) const
    {
        if (Filter == EChunkFilter::All) {
            const auto& chunkIds = ChunkManager->GetChunkIds();
            return ConvertToStrings(chunkIds.begin(), Min(chunkIds.size(), sizeLimit));
        } else {
            const auto& chunkIds = GetFilteredChunkIds();
            return ConvertToStrings(chunkIds.begin(), Min(chunkIds.size(), sizeLimit));
        }
    }

    virtual size_t GetSize() const
    {
        if (Filter == EChunkFilter::All) {
            return ChunkManager->GetChunkCount();
        } else {
            return GetFilteredChunkIds().size();
        }
    }

    virtual IYPathService::TPtr GetItemService(const Stroka& key) const
    {
        auto id = TChunkId::FromString(key);

        if (!CheckFilter(id)) {
            return NULL;
        }

        auto* chunk = ChunkManager->FindChunk(id);
        if (!chunk) {
            return NULL;
        }

        return IYPathService::FromProducer(~FromFunctor([=] (IYsonConsumer* consumer)
            {
                BuildYsonFluently(consumer)
                    .BeginMap()
                        .Item("chunk_list_id").Scalar(chunk->GetChunkListId().ToString())
                        .Item("ref_counter").Scalar(chunk->GetRefCounter())
                        .Item("stored_locations").DoListFor(chunk->StoredLocations(), [=] (TFluentList fluent, THolderId holderId)
                            {
                                const auto& holder = ChunkManager->GetHolder(holderId);
                                fluent.Item().Scalar(holder.GetAddress());
                            })
                        .DoIf(~chunk->CachedLocations(), [=] (TFluentMap fluent)
                            {
                                fluent
                                    .Item("cached_locations")
                                    .DoListFor(*chunk->CachedLocations(), [=] (TFluentList fluent, THolderId holderId)
                                        {
                                            const auto& holder = ChunkManager->GetHolder(holderId);
                                            fluent.Item().Scalar(holder.GetAddress());
                                        });
                            })
                        .DoIf(chunk->GetSize() != TChunk::UnknownSize, [=] (TFluentMap fluent)
                            {
                                fluent.Item("size").Scalar(chunk->GetSize());
                            })
                        .DoIf(chunk->IsConfirmed(), [=] (TFluentMap fluent)
                            {
                                auto attributes = chunk->DeserializeAttributes();
                                auto type = EChunkType(attributes.type());
                                fluent.Item("chunk_type").Scalar(type.ToString());
                            })
                    .EndMap();
            }));
    }
};

INodeTypeHandler::TPtr CreateChunkMapTypeHandler(
    TCypressManager* cypressManager,
    TChunkManager* chunkManager)
{
    YASSERT(cypressManager);
    YASSERT(chunkManager);

    return CreateVirtualTypeHandler(
        cypressManager,
        ERuntimeNodeType::ChunkMap,
        // TODO: extract type name
        "chunk_map",
        ~New<TVirtualChunkMap>(chunkManager, TVirtualChunkMap::EChunkFilter::All));
}

INodeTypeHandler::TPtr CreateLostChunkMapTypeHandler(
    TCypressManager* cypressManager,
    TChunkManager* chunkManager)
{
    YASSERT(cypressManager);
    YASSERT(chunkManager);

    return CreateVirtualTypeHandler(
        cypressManager,
        ERuntimeNodeType::ChunkMap,
        // TODO: extract type name
        "lost_chunk_map",
        ~New<TVirtualChunkMap>(chunkManager, TVirtualChunkMap::EChunkFilter::Lost));
}

INodeTypeHandler::TPtr CreateOverReplicatedChunkMapTypeHandler(
    TCypressManager* cypressManager,
    TChunkManager* chunkManager)
{
    YASSERT(cypressManager);
    YASSERT(chunkManager);

    return CreateVirtualTypeHandler(
        cypressManager,
        ERuntimeNodeType::ChunkMap,
        // TODO: extract type name
        "over_replicated_chunk_map",
        ~New<TVirtualChunkMap>(chunkManager, TVirtualChunkMap::EChunkFilter::OverReplicated));
}

INodeTypeHandler::TPtr CreateUnderReplicatedChunkMapTypeHandler(
    TCypressManager* cypressManager,
    TChunkManager* chunkManager)
{
    YASSERT(cypressManager);
    YASSERT(chunkManager);

    return CreateVirtualTypeHandler(
        cypressManager,
        ERuntimeNodeType::ChunkMap,
        // TODO: extract type name
        "under_replicated_chunk_map",
        ~New<TVirtualChunkMap>(chunkManager, TVirtualChunkMap::EChunkFilter::UnderReplicated));
}

////////////////////////////////////////////////////////////////////////////////

class TVirtualChunkListMap
    : public TVirtualMapBase
{
public:
    TVirtualChunkListMap(TChunkManager* chunkManager)
        : ChunkManager(chunkManager)
    { }

private:
    TChunkManager::TPtr ChunkManager;

    virtual yvector<Stroka> GetKeys(size_t sizeLimit) const
    {
        const auto& chunkListIds = ChunkManager->GetChunkListIds();
        return ConvertToStrings(chunkListIds.begin(), Min(chunkListIds.size(), sizeLimit));
    }

    virtual size_t GetSize() const
    {
        return ChunkManager->GetChunkListCount();
    }

    virtual IYPathService::TPtr GetItemService(const Stroka& key) const
    {
        auto id = TChunkListId::FromString(key);
        auto* chunkList = ChunkManager->FindChunkList(id);
        if (!chunkList) {
            return NULL;
        }

        return IYPathService::FromProducer(~FromFunctor([=] (IYsonConsumer* consumer)
            {
                BuildYsonFluently(consumer)
                    .BeginMap()
                        .Item("replica_count").Scalar(chunkList->GetReplicaCount())
                        .Item("ref_counter").Scalar(chunkList->GetRefCounter())
                        .Item("chunk_ids").DoListFor(chunkList->ChunkIds(), [=] (TFluentList fluent, TChunkId chunkId)
                            {
                                fluent.Item().Scalar(chunkId.ToString());
                            })
                    .EndMap();
            }));
    }
};

INodeTypeHandler::TPtr CreateChunkListMapTypeHandler(
    TCypressManager* cypressManager,
    TChunkManager* chunkManager)
{
    YASSERT(cypressManager);
    YASSERT(chunkManager);

    return CreateVirtualTypeHandler(
        cypressManager,
        ERuntimeNodeType::ChunkListMap,
        // TODO: extract type name
        "chunk_list_map",
        ~New<TVirtualChunkListMap>(chunkManager));
}

////////////////////////////////////////////////////////////////////////////////

class THolderRegistry
    : public IHolderRegistry
{
public:
    typedef TIntrusivePtr<THolderRegistry> TPtr;

    THolderRegistry(TCypressManager* cypressManager)
        : CypressManager(cypressManager)
    { }

    virtual bool IsHolderAuthorized(const Stroka& address)
    {
        UNUSED(address);
        return true;
    }
    
private:
    TCypressManager::TPtr CypressManager;

};

IHolderRegistry::TPtr CreateHolderRegistry(
    TCypressManager* cypressManager)
{
    return New<THolderRegistry>(cypressManager);
}

////////////////////////////////////////////////////////////////////////////////

class THolderTypeHandler
    : public TMapNodeTypeHandler
{
public:
    typedef THolderTypeHandler TThis;
    typedef TIntrusivePtr<TThis> TPtr;

    THolderTypeHandler(
        TCypressManager* cypressManager,
        TChunkManager* chunkManager)
        : TMapNodeTypeHandler(cypressManager)
        , CypressManager(cypressManager)
        , ChunkManager(chunkManager)
    {
        // NB: No smartpointer for this here.
        RegisterGetter("alive", FromMethod(&TThis::GetAlive, this));
    }

    virtual ERuntimeNodeType GetRuntimeType()
    {
        return ERuntimeNodeType::Holder;
    }

    virtual Stroka GetTypeName()
    {
        // TODO: extract type name
        return "holder";
    }

    virtual TAutoPtr<ICypressNode> CreateFromManifest(
        const TNodeId& nodeId,
        const TTransactionId& transactionId,
        NYTree::INode* manifest)
    {
        UNUSED(transactionId);
        UNUSED(manifest);
        return Create(TBranchedNodeId(nodeId, NullTransactionId));
    }

private:
    TCypressManager::TPtr CypressManager;
    TChunkManager::TPtr ChunkManager;

    Stroka GetAddress(const ICypressNode& node)
    {
        auto proxy = CypressManager->GetNodeProxy(node.GetId().NodeId, NullTransactionId);
        return proxy->GetParent()->AsMap()->GetChildKey(~proxy);
    }

    void GetAlive(const TGetAttributeParam& param)
    {
        Stroka address = GetAddress(*param.Node);
        bool alive = ChunkManager->FindHolder(address);
        BuildYsonFluently(param.Consumer)
            .Scalar(alive);
    }
};

INodeTypeHandler::TPtr CreateHolderTypeHandler(
    TCypressManager* cypressManager,
    TChunkManager* chunkManager)
{
    YASSERT(cypressManager);
    YASSERT(chunkManager);

    return New<THolderTypeHandler>(cypressManager, chunkManager);
}

////////////////////////////////////////////////////////////////////////////////

class THolderMapBehavior
    : public TNodeBehaviorBase<TMapNode, TMapNodeProxy>
{
public:
    typedef TNodeBehaviorBase<TMapNode, TMapNodeProxy> TBase;
    typedef THolderMapBehavior TThis;
    typedef TIntrusivePtr<TThis> TPtr;

    THolderMapBehavior(
        const ICypressNode& node,
        IMetaStateManager* metaStateManager,
        TCypressManager* cypressManager,
        TChunkManager* chunkManager)
        : TBase(node, cypressManager)
        , ChunkManager(chunkManager)
    {
        OnRegistered_ =
            FromMethod(&TThis::OnRegistered, TPtr(this))
            ->Via(metaStateManager->GetEpochStateInvoker());
        ChunkManager->HolderRegistered().Subscribe(OnRegistered_);
    }

    virtual void Destroy()
    {
        ChunkManager->HolderRegistered().Unsubscribe(OnRegistered_);
        OnRegistered_.Reset();
    }

private:
    TChunkManager::TPtr ChunkManager;

    IParamAction<const THolder&>::TPtr OnRegistered_;
    
    void OnRegistered(const THolder& holder)
    {
        Stroka address = holder.GetAddress();

        auto node = GetProxy();
        if (node->FindChild(address))
            return;

        // TODO: use fluent
        // TODO: make a single transaction
        // TODO: extract literals

        {
            auto request = TCypressYPathProxy::Create();
            request->SetPath(Sprintf("/%s", ~address));
            request->set_type("holder");     
            request->set_manifest("{}");     
            ExecuteVerb(
                ~IYPathService::FromNode(~node),
                ~request,
                ~CypressManager);
        }

        {
            auto request = TCypressYPathProxy::Create();
            request->SetPath(Sprintf("/%s/orchid", ~address));
            request->set_type("orchid");     
            request->set_manifest(Sprintf("{remote_address=\"%s\"}", ~address));     
            ExecuteVerb(
                ~IYPathService::FromNode(~node),
                ~request,
                ~CypressManager);
        }
    }

};

class THolderMapTypeHandler
    : public TMapNodeTypeHandler
{
public:
    typedef THolderMapTypeHandler TThis;
    typedef TIntrusivePtr<TThis> TPtr;

    THolderMapTypeHandler(
        IMetaStateManager* metaStateManager,
        TCypressManager* cypressManager,
        TChunkManager* chunkManager)
        : TMapNodeTypeHandler(cypressManager)
        , MetaStateManager(metaStateManager)
        , CypressManager(cypressManager)
        , ChunkManager(chunkManager)
    {
        // NB: No smartpointer for this here.
        RegisterGetter("alive", FromMethod(&TThis::GetAliveHolders, this));
        RegisterGetter("dead", FromMethod(&TThis::GetDeadHolders, this));
    }

    virtual ERuntimeNodeType GetRuntimeType()
    {
        return ERuntimeNodeType::HolderMap;
    }

    virtual Stroka GetTypeName()
    {
        // TODO: extract type name
        return "holder_map";
    }

    virtual TAutoPtr<ICypressNode> CreateFromManifest(
        const TNodeId& nodeId,
        const TTransactionId& transactionId,
        NYTree::INode* manifest)
    {
        UNUSED(transactionId);
        UNUSED(manifest);
        return Create(TBranchedNodeId(nodeId, NullTransactionId));
    }

    virtual INodeBehavior::TPtr CreateBehavior(const ICypressNode& node)
    {
        return New<THolderMapBehavior>(node, ~MetaStateManager, ~CypressManager, ~ChunkManager);
    }

private:
    IMetaStateManager::TPtr MetaStateManager;
    TCypressManager::TPtr CypressManager;
    TChunkManager::TPtr ChunkManager;

    void GetAliveHolders(const TGetAttributeParam& param)
    {
        BuildYsonFluently(param.Consumer)
            .DoListFor(ChunkManager->GetHolderIds(), [=] (TFluentList fluent, THolderId id)
                {
                    const auto& holder = ChunkManager->GetHolder(id);
                    fluent.Item().Scalar(holder.GetAddress());
                });
    }

    void GetDeadHolders(const TGetAttributeParam& param)
    {
        BuildYsonFluently(param.Consumer)
            .DoListFor(param.Node->NameToChild(), [=] (TFluentList fluent, TPair<Stroka, TNodeId> pair)
                {
                    Stroka address = pair.first;
                    if (!ChunkManager->FindHolder(address)) {
                        param.Consumer->OnListItem();
                        param.Consumer->OnStringScalar(address);
                    }
                });
    }
};

INodeTypeHandler::TPtr CreateHolderMapTypeHandler(
    IMetaStateManager* metaStateManager,
    TCypressManager* cypressManager,
    TChunkManager* chunkManager)
{
    YASSERT(cypressManager);
    YASSERT(chunkManager);

    return New<THolderMapTypeHandler>(metaStateManager, cypressManager, chunkManager);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
