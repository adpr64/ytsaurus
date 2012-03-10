#include "stdafx.h"
#include "change_log_cache.h"
#include "common.h"
#include "meta_state_manager.h"
#include "change_log.h"

#include <ytlib/misc/fs.h>

#include <util/folder/dirut.h>

namespace NYT {
namespace NMetaState {

using namespace NFS;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = MetaStateLogger;
static const char* LogExtension = "log";

////////////////////////////////////////////////////////////////////////////////

TCachedAsyncChangeLog::TCachedAsyncChangeLog(TChangeLog* changeLog)
    : TCacheValueBase<i32, TCachedAsyncChangeLog>(changeLog->GetId())
    , TAsyncChangeLog(changeLog)
{ }

////////////////////////////////////////////////////////////////////////////////

TChangeLogCache::TChangeLogCache(const Stroka& path)
    // TODO: introduce config
    : TSizeLimitedCache<i32, TCachedAsyncChangeLog>(4)
    , Path(path)
{ }

void TChangeLogCache::Start()
{
    LOG_DEBUG("Preparing changelog directory %s", ~Path.Quote());

    NFS::ForcePath(Path);
    NFS::CleanTempFiles(Path);
}

Stroka TChangeLogCache::GetChangeLogFileName(i32 changeLogId)
{
    return CombinePaths(Path, Sprintf("%09d.%s", changeLogId, LogExtension));
}

TChangeLogCache::TGetResult TChangeLogCache::Get(i32 changeLogId)
{
    TInsertCookie cookie(changeLogId);
    if (BeginInsert(&cookie)) {
        auto fileName = GetChangeLogFileName(changeLogId);
        if (!isexist(~fileName)) {
            cookie.Cancel(TError(
                EErrorCode::NoSuchChangeLog,
                Sprintf("No such changelog (ChangeLogId: %d)", changeLogId)));
        } else {
            try {
                auto changeLog = New<TChangeLog>(fileName, changeLogId);
                changeLog->Open();
                cookie.EndInsert(New<TCachedAsyncChangeLog>(~changeLog));
            } catch (const std::exception& ex) {
                LOG_FATAL("Error opening changelog (ChangeLogId: %d)\n%s",
                    changeLogId,
                    ex.what());
            }
        }
    }
    return cookie.GetAsyncResult()->Get();
}

TCachedAsyncChangeLogPtr TChangeLogCache::Create(
    i32 changeLogId,
    i32 prevRecordCount)
{
    TInsertCookie cookie(changeLogId);
    if (!BeginInsert(&cookie)) {
        LOG_FATAL("Trying to create an already existing changelog (ChangeLogId: %d)",
            changeLogId);
    }

    auto fileName = GetChangeLogFileName(changeLogId);

    try {
        auto changeLog = New<TChangeLog>(fileName, changeLogId);
        changeLog->Create(prevRecordCount);
        cookie.EndInsert(New<TCachedAsyncChangeLog>(~changeLog));
    } catch (const std::exception& ex) {
        LOG_FATAL("Error creating changelog (ChangeLogId: %d)\n%s",
            changeLogId,
            ex.what());
    }

    return cookie.GetAsyncResult()->Get().Value();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NMetaState
} // namespace NYT
