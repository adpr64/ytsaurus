#pragma once

#include "public.h"

#include <yt/server/master/cell_master/public.h>

#include <yt/server/master/node_tracker_server//public.h>

#include <yt/core/concurrency/public.h>
#include <yt/core/concurrency/thread_affinity.h>

#include <yt/core/misc/small_set.h>
#include <yt/core/misc/optional.h>

namespace NYT::NTabletServer {

////////////////////////////////////////////////////////////////////////////////

class TTabletTracker
    : public TRefCounted
{
public:
    explicit TTabletTracker(NCellMaster::TBootstrap* bootstrap);

    void Start();
    void Stop();

private:
    class TImpl;
    TIntrusivePtr<TImpl> Impl_;
};

DEFINE_REFCOUNTED_TYPE(TTabletTracker)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletServer
