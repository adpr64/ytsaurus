#pragma once

#include "public.h"
#include "chunk_tree_statistics.h"

#include <yt/server/cypress_server/public.h>

#include <yt/ytlib/table_client/public.h>

#include <yt/core/yson/public.h>

namespace NYT {
namespace NChunkServer {

////////////////////////////////////////////////////////////////////////////////

template <class F>
void VisitUniqueAncestors(TChunkList* chunkList, F functor);

template <class F>
void VisitAncestors(TChunkList* chunkList, F functor);

template <class F>
void AttachToChunkList(
    TChunkList* chunkList,
    TChunkTree** childrenBegin,
    TChunkTree** childrenEnd,
    F childAction);

template <class F>
void DetachFromChunkList(
    TChunkList* chunkList,
    TChunkTree** childrenBegin,
    TChunkTree** childrenEnd,
    F childAction);

void SetChunkTreeParent(TChunkList* parent, TChunkTree* child);
void ResetChunkTreeParent(TChunkList* parent, TChunkTree* child);

TChunkTreeStatistics GetChunkTreeStatistics(TChunkTree* chunkTree);
void AccumulateChildStatistics(
    TChunkList* chunkList,
    TChunkTree* child,
    TChunkTreeStatistics* statistics);
void AccumulateUniqueAncestorsStatistics(
    TChunkList* chunkList,
    const TChunkTreeStatistics& statisticsDelta);
void ResetChunkListStatistics(TChunkList* chunkList);
void RecomputeChunkListStatistics(TChunkList* chunkList);

NSecurityServer::TClusterResources GetDiskUsage(
    const NChunkClient::NProto::TDataStatistics& statistics,
    int replicationFactor);

std::vector<TChunkOwnerBase*> GetOwningNodes(
    TChunkTree* chunkTree);
TFuture<NYson::TYsonString> GetMulticellOwningNodes(
    NCellMaster::TBootstrap* bootstrap,
    TChunkTree* chunkTree);

NTableClient::TOwningKey GetMaxKey(const TChunk* chunk);
NTableClient::TOwningKey GetMaxKey(const TChunkList* chunkList);
NTableClient::TOwningKey GetMaxKey(const TChunkTree* chunkTree);

NTableClient::TOwningKey GetMinKey(const TChunk* chunk);
NTableClient::TOwningKey GetMinKey(const TChunkList* chunkList);
NTableClient::TOwningKey GetMinKey(const TChunkTree* chunkTree);

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT

#define HELPERS_INL_H_
#include "helpers-inl.h"
#undef HELPERS_INL_H_
