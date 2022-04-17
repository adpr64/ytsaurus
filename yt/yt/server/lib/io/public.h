#pragma once

#include <yt/yt/ytlib/misc/public.h>

namespace NYT::NIO {

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(EIOEngineType,
    (ThreadPool)
    (Uring)
    (FairShareThreadPool)
);

DEFINE_ENUM(EDirectIOPolicy,
    (Always)
    (Never)
    (OnDemand)
);

DECLARE_REFCOUNTED_STRUCT(IIOEngine)
DECLARE_REFCOUNTED_STRUCT(IDynamicIOEngine)

DECLARE_REFCOUNTED_CLASS(TChunkFileReader)
DECLARE_REFCOUNTED_CLASS(TChunkFileWriter)

DECLARE_REFCOUNTED_CLASS(TIOTrackerConfig)
DECLARE_REFCOUNTED_STRUCT(TCongestionDetectorConfig)
DECLARE_REFCOUNTED_STRUCT(TGentleLoaderConfig)

DECLARE_REFCOUNTED_STRUCT(IIOTracker)

DECLARE_REFCOUNTED_STRUCT(IIOEngineWorkloadModel)
DECLARE_REFCOUNTED_STRUCT(IRandomFileProvider);
DECLARE_REFCOUNTED_STRUCT(IGentleLoader);

class TIOEngineHandle;

using TIOEngineHandlePtr = TIntrusivePtr<TIOEngineHandle>;

struct TChunkFragmentDescriptor;

struct IBlocksExtCache;

struct TIOEvent;

constexpr i64 SectorSize = 512;

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NIO
