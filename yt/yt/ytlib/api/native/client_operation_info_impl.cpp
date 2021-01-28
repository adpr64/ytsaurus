#include "client_impl.h"
#include "config.h"
#include "connection.h"
#include "list_operations.h"
#include "private.h"
#include "rpc_helpers.h"

#include <yt/client/api/operation_archive_schema.h>
#include <yt/client/api/rowset.h>

#include <yt/client/security_client/helpers.h>

#include <yt/client/table_client/helpers.h>
#include <yt/client/table_client/name_table.h>
#include <yt/client/table_client/schema.h>

#include <yt/ytlib/object_client/object_service_proxy.h>

#include <yt/ytlib/security_client/helpers.h>

#include <yt/ytlib/scheduler/helpers.h>

#include <yt/core/concurrency/action_queue.h>
#include <yt/core/concurrency/scheduler.h>

#include <yt/core/ytree/ypath_resolver.h>

namespace NYT::NApi::NNative {

using namespace NConcurrency;
using namespace NYPath;
using namespace NYTree;
using namespace NYson;
using namespace NCypressClient;
using namespace NObjectClient;
using namespace NTransactionClient;
using namespace NRpc;
using namespace NTableClient;
using namespace NTableClient::NProto;
using namespace NSecurityClient;
using namespace NQueryClient;
using namespace NScheduler;

////////////////////////////////////////////////////////////////////////////////

// Attribute names allowed for 'get_operation' and 'list_operation' commands.
static const THashSet<TString> SupportedOperationAttributes = {
    "id",
    "state",
    "authenticated_user",
    "type",
    // COMPAT(levysotsky): "operation_type" is deprecated
    "operation_type",
    "progress",
    "spec",
    // COMPAT(gritukan): Drop it.
    "annotations",
    "full_spec",
    "unrecognized_spec",
    "brief_progress",
    "brief_spec",
    "runtime_parameters",
    "start_time",
    "finish_time",
    "result",
    "events",
    "memory_usage",
    "suspended",
    "slot_index_per_pool_tree",
    "alerts",
    "task_names",
};

////////////////////////////////////////////////////////////////////////////////

bool TClient::DoesOperationsArchiveExist()
{
    // NB: we suppose that archive should exist and work correctly if this map node is presented.
    return WaitFor(NodeExists("//sys/operations_archive", TNodeExistsOptions()))
        .ValueOrThrow();
}

int TClient::DoGetOperationsArchiveVersion()
{
    auto asyncVersionResult = GetNode(GetOperationsArchiveVersionPath(), TGetNodeOptions());
    auto versionNodeOrError = WaitFor(asyncVersionResult);

    if (!versionNodeOrError.IsOK()) {
        THROW_ERROR_EXCEPTION("Failed to get operations archive version")
            << versionNodeOrError;
    }

    int version = 0;
    try {
        version = ConvertTo<int>(versionNodeOrError.Value());
    } catch (const std::exception& ex) {
        THROW_ERROR_EXCEPTION("Failed to parse operations archive version")
            << ex;
    }

    return version;
}

// Map operation attribute names as they are requested in 'get_operation' or 'list_operations'
// commands to Cypress node attribute names.
static std::vector<TString> CreateCypressOperationAttributes(const THashSet<TString>& attributes)
{
    std::vector<TString> result;
    result.reserve(attributes.size());
    for (const auto& attribute : attributes) {
        if (!SupportedOperationAttributes.contains(attribute)) {
            THROW_ERROR_EXCEPTION(
                NApi::EErrorCode::NoSuchAttribute,
                "Operation attribute %Qv is not supported",
                attribute)
                << TErrorAttribute("attribute_name", attribute);

        }
        if (attribute == "id") {
            result.emplace_back("key");
        } else if (attribute == "type") {
            result.emplace_back("operation_type");
        } else {
            result.push_back(attribute);
        }
    }
    return result;
}

// Map operation attribute names as they are requested in 'get_operation' or 'list_operations'
// commands to operations archive column names.
static std::vector<TString> CreateArchiveOperationAttributes(const THashSet<TString>& attributes)
{
    std::vector<TString> result;
    // Plus 1 for 'id_lo' and 'id_hi' instead of 'id'.
    result.reserve(attributes.size() + 1);
    for (const auto& attribute : attributes) {
        if (!SupportedOperationAttributes.contains(attribute)) {
            THROW_ERROR_EXCEPTION(
                NApi::EErrorCode::NoSuchAttribute,
                "Operation attribute %Qv is not supported",
                attribute)
                << TErrorAttribute("attribute_name", attribute);
        }
        if (attribute == "id") {
            result.emplace_back("id_hi");
            result.emplace_back("id_lo");
        } else if (attribute == "type") {
            result.emplace_back("operation_type");
        } else if (attribute == "annotations") {
            // COMPAT(gritukan): This field is deprecated.
        } else {
            result.push_back(attribute);
        }
    }
    return result;
}

TClient::TGetOperationFromCypressResult TClient::DoGetOperationFromCypress(
    NScheduler::TOperationId operationId,
    TInstant deadline,
    const TGetOperationOptions& options)
{
    std::optional<std::vector<TString>> cypressAttributes;
    if (options.Attributes) {
        cypressAttributes = CreateCypressOperationAttributes(*options.Attributes);

        if (!options.Attributes->contains("controller_agent_address")) {
            cypressAttributes->push_back("controller_agent_address");
        }
        cypressAttributes->push_back("modification_time");
    }

    auto proxy = CreateReadProxy<TObjectServiceProxy>(options);
    auto batchReq = proxy->ExecuteBatch();
    SetBalancingHeader(batchReq, options);

    {
        auto req = TYPathProxy::Get(GetOperationPath(operationId) + "/@");
        if (cypressAttributes) {
            ToProto(req->mutable_attributes()->mutable_keys(), *cypressAttributes);
        }
        batchReq->AddRequest(req, "get_operation");
    }

    auto batchRsp = WaitFor(batchReq->Invoke())
        .ValueOrThrow();
    auto cypressNodeRspOrError = batchRsp->GetResponse<TYPathProxy::TRspGet>("get_operation");

    INodePtr cypressNode;
    if (cypressNodeRspOrError.IsOK()) {
        const auto& cypressNodeRsp = cypressNodeRspOrError.Value();
        cypressNode = ConvertToNode(TYsonString(cypressNodeRsp->value()));
    } else {
        if (!cypressNodeRspOrError.FindMatching(NYTree::EErrorCode::ResolveError)) {
            THROW_ERROR cypressNodeRspOrError;
        }
    }

    if (!cypressNode) {
        return {};
    }

    auto attrNode = cypressNode->AsMap();

    // XXX(ignat): remove opaque from node. Make option to ignore it in conversion methods.
    if (auto fullSpecNode = attrNode->FindChild("full_spec")) {
        fullSpecNode->MutableAttributes()->Remove("opaque");
    }

    if (auto child = attrNode->FindChild("operation_type")) {
        // COMPAT(levysotsky): When "operation_type" is disallowed, this code
        // will be simplified to unconditionally removing the child
        // (and also child will not have to be cloned).
        if (options.Attributes && !options.Attributes->contains("operation_type")) {
            attrNode->RemoveChild("operation_type");
        }

        attrNode->RemoveChild("type");
        YT_VERIFY(attrNode->AddChild("type", CloneNode(child)));
    }

    if (auto child = attrNode->FindChild("key")) {
        attrNode->RemoveChild("key");
        attrNode->RemoveChild("id");
        YT_VERIFY(attrNode->AddChild("id", child));
    }

    if (options.Attributes && !options.Attributes->contains("state")) {
        attrNode->RemoveChild("state");
    }

    auto modificationTimeNode = attrNode->FindChild("modification_time");
    YT_VERIFY(modificationTimeNode);
    auto modificationTime = ConvertTo<TInstant>(modificationTimeNode);
    attrNode->RemoveChild(modificationTimeNode);

    if (!options.Attributes) {
        auto keysToKeep = ConvertTo<THashSet<TString>>(attrNode->GetChildOrThrow("user_attribute_keys"));
        keysToKeep.insert("id");
        keysToKeep.insert("type");
        for (const auto& key : attrNode->GetKeys()) {
            if (!keysToKeep.contains(key)) {
                attrNode->RemoveChild(key);
            }
        }
    }

    std::optional<TString> controllerAgentAddress;
    if (auto child = attrNode->FindChild("controller_agent_address")) {
        controllerAgentAddress = child->AsString()->GetValue();
        if (options.Attributes && !options.Attributes->contains("controller_agent_address")) {
            attrNode->RemoveChild(child);
        }
    }

    std::vector<std::pair<TString, bool>> runtimeAttributes ={
        /* {Name, ShouldRequestFromScheduler} */
        {"progress", true},
        {"brief_progress", false},
        {"memory_usage", false}
    };

    if (options.IncludeRuntime) {
        auto batchReq = proxy->ExecuteBatch();

        auto addProgressAttributeRequest = [&] (const TString& attribute, bool shouldRequestFromScheduler) {
            if (shouldRequestFromScheduler) {
                auto req = TYPathProxy::Get(GetSchedulerOrchidOperationPath(operationId) + "/" + attribute);
                batchReq->AddRequest(req, "get_operation_" + attribute);
            }
            if (controllerAgentAddress) {
                auto path = GetControllerAgentOrchidOperationPath(*controllerAgentAddress, operationId);
                auto req = TYPathProxy::Get(path + "/" + attribute);
                batchReq->AddRequest(req, "get_operation_" + attribute);
            }
        };

        for (const auto& attribute : runtimeAttributes) {
            if (!options.Attributes || options.Attributes->contains(attribute.first)) {
                addProgressAttributeRequest(attribute.first, attribute.second);
            }
        }

        if (batchReq->GetSize() != 0) {
            auto batchRsp = WaitFor(batchReq->Invoke())
                .ValueOrThrow();

            auto handleProgressAttributeRequest = [&] (const TString& attribute) {
                INodePtr progressAttributeNode;

                auto responses = batchRsp->GetResponses<TYPathProxy::TRspGet>("get_operation_" + attribute);
                for (const auto& rsp : responses) {
                    if (rsp.IsOK()) {
                        auto node = ConvertToNode(TYsonString(rsp.Value()->value()));
                        if (!progressAttributeNode) {
                            progressAttributeNode = node;
                        } else {
                            progressAttributeNode = PatchNode(progressAttributeNode, node);
                        }
                    } else {
                        if (!rsp.FindMatching(NYTree::EErrorCode::ResolveError)) {
                            THROW_ERROR rsp;
                        }
                    }

                    if (progressAttributeNode) {
                        attrNode->RemoveChild(attribute);
                        YT_VERIFY(attrNode->AddChild(attribute, progressAttributeNode));
                    }
                }
            };

            for (const auto& attribute : runtimeAttributes) {
                if (!options.Attributes || options.Attributes->contains(attribute.first)) {
                    handleProgressAttributeRequest(attribute.first);
                }
            }
        }
    }

    return TGetOperationFromCypressResult{
        .Operation = ConvertToYsonString(attrNode),
        .NodeModificationTime = modificationTime,
    };
}

TYsonString TClient::DoGetOperationFromArchive(
    NScheduler::TOperationId operationId,
    TInstant deadline,
    const TGetOperationOptions& options)
{
    auto attributes = options.Attributes.value_or(SupportedOperationAttributes);
    // Ignoring memory_usage and suspended in archive.
    attributes.erase("memory_usage");
    attributes.erase("suspended");

    auto fieldsVector = CreateArchiveOperationAttributes(attributes);
    THashSet<TString> fields(fieldsVector.begin(), fieldsVector.end());

    TOrderedByIdTableDescriptor tableDescriptor;
    std::vector<int> columnIndices;
    for (const auto& field : fields) {
        columnIndices.push_back(tableDescriptor.NameTable->GetIdOrThrow(field));
    }
    const auto columnFilter = NTableClient::TColumnFilter(columnIndices);
    auto rowset = LookupOperationsInArchive(
        this,
        {operationId},
        columnFilter,
        deadline - Now())
        .ValueOrThrow();
    auto rows = rowset->GetRows();
    YT_VERIFY(!rows.Empty());
    auto row = rows[0];
    if (!row) {
        return {};
    }

#define SET_ITEM_STRING_VALUE_WITH_FIELD(itemKey, fieldName) \
    SET_ITEM_VALUE_WITH_FIELD(itemKey, fieldName, FromUnversionedValue<TString>(row[index]))
#define SET_ITEM_STRING_VALUE(itemKey) SET_ITEM_STRING_VALUE_WITH_FIELD(itemKey, itemKey)
#define SET_ITEM_YSON_STRING_VALUE(itemKey) \
    SET_ITEM_VALUE(itemKey, FromUnversionedValue<TYsonString>(row[index]))
#define SET_ITEM_INSTANT_VALUE(itemKey) \
    SET_ITEM_VALUE(itemKey, FromUnversionedValue<TInstant>(row[index]))
#define SET_ITEM_VALUE_WITH_FIELD(itemKey, fieldName, operation) \
    .DoIf(fields.find(fieldName) != fields.end() && row[GET_INDEX(fieldName)].Type != EValueType::Null, \
        [&] (TFluentMap fluent) { \
            auto index = GET_INDEX(fieldName); \
            fluent.Item(itemKey).Value(operation); \
        })
#define SET_ITEM_VALUE(itemKey, operation) SET_ITEM_VALUE_WITH_FIELD(itemKey, itemKey, operation)
#define GET_INDEX(itemKey) columnFilter.GetPosition(tableDescriptor.NameTable->GetIdOrThrow(itemKey))

    return BuildYsonStringFluently()
        .BeginMap()
            .DoIf(fields.contains("id_lo"), [&] (TFluentMap fluent) {
                fluent.Item("id").Value(operationId);
            })
            SET_ITEM_STRING_VALUE("state")
            SET_ITEM_STRING_VALUE("authenticated_user")
            SET_ITEM_STRING_VALUE_WITH_FIELD("type", "operation_type")
            // COMPAT(levysotsky): Add this field under old name for
            // backward compatibility. Should be removed when all the clients migrate.
            SET_ITEM_STRING_VALUE("operation_type")
            SET_ITEM_YSON_STRING_VALUE("progress")
            SET_ITEM_YSON_STRING_VALUE("spec")
            SET_ITEM_YSON_STRING_VALUE("full_spec")
            SET_ITEM_YSON_STRING_VALUE("unrecognized_spec")
            SET_ITEM_YSON_STRING_VALUE("brief_progress")
            SET_ITEM_YSON_STRING_VALUE("brief_spec")
            SET_ITEM_YSON_STRING_VALUE("runtime_parameters")
            SET_ITEM_INSTANT_VALUE("start_time")
            SET_ITEM_INSTANT_VALUE("finish_time")
            SET_ITEM_YSON_STRING_VALUE("result")
            SET_ITEM_YSON_STRING_VALUE("events")
            SET_ITEM_YSON_STRING_VALUE("slot_index_per_pool_tree")
            SET_ITEM_YSON_STRING_VALUE("alerts")
            SET_ITEM_YSON_STRING_VALUE("task_names")
        .EndMap();
#undef SET_ITEM_STRING_VALUE
#undef SET_ITEM_YSON_STRING_VALUE
#undef SET_ITEM_INSTANT_VALUE
#undef SET_ITEM_VALUE
#undef GET_INDEX
}

TOperationId TClient::ResolveOperationAlias(
    const TString& alias,
    const TMasterReadOptions& options,
    TInstant deadline)
{
    auto proxy = CreateReadProxy<TObjectServiceProxy>(options);
    auto req = TYPathProxy::Get(GetSchedulerOrchidAliasPath(alias) + "/operation_id");
    auto rspOrError = WaitFor(proxy->Execute(req));
    if (rspOrError.IsOK()) {
        return ConvertTo<TOperationId>(TYsonString(rspOrError.Value()->value()));
    } else if (!rspOrError.FindMatching(NYTree::EErrorCode::ResolveError)) {
        THROW_ERROR_EXCEPTION("Error while resolving alias from scheduler")
            << rspOrError
            << TErrorAttribute("operation_alias", alias);
    }

    TOperationAliasesTableDescriptor tableDescriptor;
    auto rowBuffer = New<TRowBuffer>();

    std::vector<TUnversionedRow> keys;
    auto key = rowBuffer->AllocateUnversioned(1);
    key[0] = MakeUnversionedStringValue(alias, tableDescriptor.Index.Alias);
    keys.push_back(key);

    TLookupRowsOptions lookupOptions;
    lookupOptions.KeepMissingRows = true;
    lookupOptions.Timeout = deadline - Now();

    auto rowset = WaitFor(LookupRows(
        GetOperationsArchiveOperationAliasesPath(),
        tableDescriptor.NameTable,
        MakeSharedRange(std::move(keys), std::move(rowBuffer)),
        lookupOptions))
        .ValueOrThrow();

    auto rows = rowset->GetRows();
    YT_VERIFY(!rows.Empty());
    if (rows[0]) {
        TOperationId operationId;
        operationId.Parts64[0] = rows[0][tableDescriptor.Index.OperationIdHi].Data.Uint64;
        operationId.Parts64[1] = rows[0][tableDescriptor.Index.OperationIdLo].Data.Uint64;
        return operationId;
    }

    THROW_ERROR_EXCEPTION("Operation alias is unknown")
        << TErrorAttribute("alias", alias);
}

static TInstant GetProgressBuildTime(const TYsonString& progressYson)
{
    if (!progressYson) {
        return TInstant();
    }
    auto maybeTimeString = TryGetString(progressYson.AsStringBuf(), "/build_time");
    if (!maybeTimeString) {
        return TInstant();
    }
    return ConvertTo<TInstant>(*maybeTimeString);
}

static TYsonString GetLatestProgress(const TYsonString& cypressProgress, const TYsonString& archiveProgress)
{
    return GetProgressBuildTime(cypressProgress) > GetProgressBuildTime(archiveProgress)
        ? cypressProgress
        : archiveProgress;
}

TYsonString TClient::DoGetOperationImpl(
    TOperationId operationId,
    TInstant deadline,
    const TGetOperationOptions& options)
{
    std::vector<TFuture<void>> getOperationFutures;

    auto cypressFuture = BIND(&TClient::DoGetOperationFromCypress, MakeStrong(this), operationId, deadline, options)
        .AsyncVia(Connection_->GetInvoker())
        .Run();
    getOperationFutures.push_back(cypressFuture.As<void>());

    // We request state to distinguish controller agent's archive entries
    // from operation cleaner's ones (the latter must have "state" field).
    auto archiveOptions = options;
    if (archiveOptions.Attributes) {
        archiveOptions.Attributes->insert("state");
    }

    auto archiveFuture = MakeFuture(TYsonString());
    if (DoesOperationsArchiveExist()) {
        archiveFuture = BIND(&TClient::DoGetOperationFromArchive,
            MakeStrong(this),
            operationId,
            deadline,
            std::move(archiveOptions))
            .AsyncVia(Connection_->GetInvoker())
            .Run()
            .WithTimeout(options.ArchiveTimeout);
    }
    getOperationFutures.push_back(archiveFuture.As<void>());

    WaitFor(AllSet<void>(getOperationFutures))
        .ValueOrThrow();

    auto [cypressResult, operationNodeModificationTime] = cypressFuture.Get()
        .ValueOrThrow();

    auto archiveResultOrError = archiveFuture.Get();
    TYsonString archiveResult;
    if (archiveResultOrError.IsOK()) {
        archiveResult = archiveResultOrError.Value();
    } else {
        YT_LOG_DEBUG("Failed to get information for operation from archive (OperationId: %v, Error: %v)",
            operationId,
            archiveResultOrError);
    }

    static const std::vector<TString> ProgressFieldNames = {"brief_progress", "progress"};

    auto mergeResults = [] (const TYsonString& cypressResult, const TYsonString& archiveResult) {
        if (!archiveResult) {
            return cypressResult;
        }

        auto cypressResultNode = ConvertToNode(cypressResult);
        YT_VERIFY(cypressResultNode->GetType() == ENodeType::Map);
        const auto& cypressResultMap = cypressResultNode->AsMap();

        for (const auto& fieldName : ProgressFieldNames) {
            auto cypressFieldNode = cypressResultMap->FindChild(fieldName);
            cypressResultMap->RemoveChild(fieldName);

            auto archiveFieldString = TryGetAny(archiveResult.AsStringBuf(), "/" + fieldName);

            TYsonString archiveFieldYsonString;
            if (archiveFieldString) {
                archiveFieldYsonString = TYsonString(*archiveFieldString);
            }

            TYsonString cypressFieldYsonString;
            if (cypressFieldNode) {
                cypressFieldYsonString = ConvertToYsonString(cypressFieldNode);
            }

            if (auto result = GetLatestProgress(cypressFieldYsonString, archiveFieldYsonString)) {
                cypressResultMap->AddChild(fieldName, ConvertToNode(result));
            }
        }
        return ConvertToYsonString(cypressResultMap);
    };

    if (cypressResult && archiveResultOrError.IsOK()) {
        return mergeResults(cypressResult, archiveResult);
    }

    auto getOldestBuildTime = [&] (const TYsonString& result) {
        auto oldestBuildTime = TInstant::Max();
        for (const auto& fieldName : ProgressFieldNames) {
            if (options.Attributes && !options.Attributes->contains(fieldName)) {
                continue;
            }
            TInstant buildTime;
            if (auto maybeProgressYson = TryGetAny(result.AsStringBuf(), "/" + fieldName)) {
                buildTime = GetProgressBuildTime(TYsonString(*maybeProgressYson));
            }
            oldestBuildTime = Min(oldestBuildTime, buildTime);
        }
        return oldestBuildTime;
    };

    if (cypressResult) {
        auto cypressProgressAge = operationNodeModificationTime - getOldestBuildTime(cypressResult);
        if (cypressProgressAge <= options.MaximumCypressProgressAge) {
            return cypressResult;
        }

        YT_LOG_DEBUG(archiveResultOrError,
            "Operation progress in Cypress is outdated, while archive request failed "
            "(OperationId: %v, CypressProgressAge: %v, MaximumCypressProgressAge: %v)",
            operationId,
            cypressProgressAge,
            options.MaximumCypressProgressAge);
        if (archiveResultOrError.FindMatching(NYT::EErrorCode::Timeout)) {
            try {
                archiveResultOrError = DoGetOperationFromArchive(operationId, deadline, archiveOptions);
            } catch (const TErrorException& error) {
                archiveResultOrError = error;
            }
        }
        if (!archiveResultOrError.IsOK()) {
            THROW_ERROR_EXCEPTION(
                EErrorCode::OperationProgressOutdated,
                "Operation progress in Cypress is outdated while archive request failed")
                << archiveResultOrError;
        }
        return mergeResults(cypressResult, archiveResultOrError.Value());
    }

    // Check whether archive row was written by controller agent or operation cleaner.
    // Here we assume that controller agent does not write "state" field to the archive.
    auto isCompleteArchiveResult = [] (const TYsonString& archiveResult) {
        return TryGetString(archiveResult.AsStringBuf(), "/state").has_value();
    };

    if (archiveResult) {
        // We have a non-empty response from archive and an empty response from Cypress.
        // If the archive response is incomplete (i.e. written by controller agent),
        // we need to retry the archive request as there might be a race
        // between these two requests and operation archivation.
        //
        // ---------------------------------------------------> time
        //         |               |             |
        //    archive rsp.   archivation   cypress rsp.
        if (!isCompleteArchiveResult(archiveResult)) {
            YT_LOG_DEBUG("Got empty response from Cypress and incomplete response from archive, "
                "retrying (OperationId: %v)",
                operationId);
            archiveResult = DoGetOperationFromArchive(operationId, deadline, archiveOptions);
        }
    } else if (!archiveResultOrError.IsOK()) {
        // The operation is missing from Cypress and the archive request finished with errors.
        // If it is timeout error, we retry without timeout.
        // Otherwise we throw the error as there is no hope.
        if (!archiveResultOrError.FindMatching(NYT::EErrorCode::Timeout)) {
            archiveResultOrError.ThrowOnError();
        }
        archiveResult = DoGetOperationFromArchive(operationId, deadline, archiveOptions);
    }

    if (!archiveResult || !isCompleteArchiveResult(archiveResult)) {
        THROW_ERROR_EXCEPTION(
            NApi::EErrorCode::NoSuchOperation,
            "No such operation %v",
            operationId);
    }

    if (!options.Attributes || options.Attributes->contains("state")) {
        return archiveResult;
    }
    // Remove "state" field if it was not requested.
    auto archiveResultNode = ConvertToNode(archiveResult);
    YT_VERIFY(archiveResultNode->GetType() == ENodeType::Map);
    archiveResultNode->AsMap()->RemoveChild("state");
    return ConvertToYsonString(archiveResultNode);
}

TYsonString TClient::DoGetOperation(
    const TOperationIdOrAlias& operationIdOrAlias,
    const TGetOperationOptions& options)
{
    auto timeout = options.Timeout.value_or(Connection_->GetConfig()->DefaultGetOperationTimeout);
    auto deadline = timeout.ToDeadLine();

    TOperationId operationId;
    Visit(operationIdOrAlias.Payload,
        [&] (const TOperationId& id) {
            operationId = id;
        },
        [&] (const TString& alias) {
            if (!options.IncludeRuntime) {
                THROW_ERROR_EXCEPTION(
                    "Operation alias cannot be resolved without using runtime information; "
                    "consider setting include_runtime = %true");
            }
            operationId = ResolveOperationAlias(alias, options, deadline);
        });

    while (true) {
        try {
            return DoGetOperationImpl(operationId, deadline, options);
        } catch (const TErrorException& error) {
            YT_LOG_DEBUG(error, "Failed to get operation (OperationId: %v)",
                operationId);
            if (!error.Error().FindMatching(EErrorCode::OperationProgressOutdated)) {
                throw;
            }
            if (TInstant::Now() > deadline) {
                throw;
            }
            TDelayedExecutor::WaitForDuration(Connection_->GetConfig()->DefaultGetOperationRetryInterval);
        }
    }
}

static THashSet<TString> DeduceActualAttributes(
    const std::optional<THashSet<TString>>& originalAttributes,
    const THashSet<TString>& requiredAttributes,
    const THashSet<TString>& defaultAttributes,
    const THashSet<TString>& ignoredAttributes)
{
    auto attributes = originalAttributes.value_or(defaultAttributes);
    attributes.insert(requiredAttributes.begin(), requiredAttributes.end());
    for (const auto& attribute : ignoredAttributes) {
        attributes.erase(attribute);
    }
    return attributes;
}

// Searches in Cypress for operations satisfying given filters.
// Adds found operations to |idToOperation| map.
// The operations are returned with requested fields plus necessarily "start_time" and "id".
void TClient::DoListOperationsFromCypress(
    TInstant deadline,
    TListOperationsCountingFilter& countingFilter,
    const TListOperationsOptions& options,
    THashMap<NScheduler::TOperationId, TOperation>* idToOperation)
{
    // These attributes will be requested for every operation in Cypress.
    // All the other attributes are considered heavy and if they are present in
    // the set of requested attributes an extra batch of "get" requests
    // (one for each operation satisfying filters) will be issued, so:
    // XXX(levysotsky): maintain this list up-to-date.
    const THashSet<TString> LightAttributes = {
        "authenticated_user",
        "brief_progress",
        "brief_spec",
        "events",
        "finish_time",
        "id",
        "type",
        "result",
        "runtime_parameters",
        "start_time",
        "state",
        "suspended",
    };

    const THashSet<TString> RequiredAttributes = {"id", "start_time"};

    const THashSet<TString> DefaultAttributes = {
        "authenticated_user",
        "brief_progress",
        "brief_spec",
        "finish_time",
        "id",
        "type",
        "runtime_parameters",
        "start_time",
        "state",
        "suspended",
    };

    const THashSet<TString> IgnoredAttributes = {};

    auto requestedAttributes = DeduceActualAttributes(options.Attributes, RequiredAttributes, DefaultAttributes, IgnoredAttributes);

    auto filteringAttributes = LightAttributes;
    if (options.SubstrFilter) {
        filteringAttributes.emplace("annotations");
    }
    auto filteringCypressAttributes = CreateCypressOperationAttributes(filteringAttributes);

    TObjectServiceProxy proxy(GetOperationArchiveChannel(options.ReadFrom), Connection_->GetStickyGroupSizeCache());
    auto requestOperations = [&] (int hashBegin, int hashEnd) {
        auto listBatchReq = proxy.ExecuteBatch();
        SetBalancingHeader(listBatchReq, options);
        for (int hash = hashBegin; hash < hashEnd; ++hash) {
            auto hashStr = Format("%02x", hash);
            auto req = TYPathProxy::List("//sys/operations/" + hashStr);
            SetCachingHeader(req, options);
            ToProto(req->mutable_attributes()->mutable_keys(), filteringCypressAttributes);
            listBatchReq->AddRequest(req, "list_operations_" + hashStr);
        }
        return listBatchReq->Invoke();
    };

    constexpr int HashCount = 256;
    constexpr int BatchSize = 16;
    static_assert(HashCount % BatchSize == 0);
    std::vector<TFuture<TObjectServiceProxy::TRspExecuteBatchPtr>> batchFutures;
    for (int hashBegin = 0; hashBegin < HashCount; hashBegin += BatchSize) {
        batchFutures.push_back(requestOperations(hashBegin, hashBegin + BatchSize));
    }
    auto responses = WaitFor(AllSucceeded<TObjectServiceProxy::TRspExecuteBatchPtr>(batchFutures))
        .ValueOrThrow();

    std::vector<TYsonString> operationsYson;
    operationsYson.reserve(HashCount);
    for (int hashBegin = 0, responseIndex = 0; hashBegin < HashCount; hashBegin += BatchSize, ++responseIndex) {
        YT_VERIFY(responseIndex < responses.size());
        const auto& batchRsp = responses[responseIndex];

        for (int hash = hashBegin; hash < hashBegin + BatchSize; ++hash) {
            auto rspOrError = batchRsp->GetResponse<TYPathProxy::TRspList>(Format("list_operations_%02x", hash));
            if (rspOrError.FindMatching(NYTree::EErrorCode::ResolveError)) {
                continue;
            }
            auto rsp = rspOrError.ValueOrThrow();
            operationsYson.emplace_back(std::move(*rsp->mutable_value()));
        }
    }

    TListOperationsFilter filter(operationsYson, countingFilter, options);

    // Lookup all operations with currently filtered ids, add their brief progress.
    if (DoesOperationsArchiveExist()) {
        TOrderedByIdTableDescriptor tableDescriptor;
        std::vector<TOperationId> ids;
        ids.reserve(filter.GetCount());
        filter.ForEachOperationImmutable([&] (int index, const TListOperationsFilter::TLightOperation& lightOperation) {
            ids.push_back(lightOperation.GetId());
        });

        auto columnFilter = NTableClient::TColumnFilter({tableDescriptor.Index.BriefProgress});
        auto rowsetOrError = LookupOperationsInArchive(
            this,
            ids,
            columnFilter,
            options.ArchiveFetchingTimeout);

        if (!rowsetOrError.IsOK()) {
            YT_LOG_DEBUG(rowsetOrError, "Failed to get information about operations' brief_progress from Archive");
        } else {
            auto rows = rowsetOrError.ValueOrThrow()->GetRows();
            YT_VERIFY(rows.Size() == filter.GetCount());

            auto position = columnFilter.FindPosition(tableDescriptor.Index.BriefProgress);
            filter.ForEachOperationMutable([&] (int index, TListOperationsFilter::TLightOperation& lightOperation) {
                auto row = rows[index];
                if (!row) {
                    return;
                }
                if (!position) {
                    return;
                }
                auto value = row[*position];
                if (value.Type == EValueType::Null) {
                    return;
                }
                YT_VERIFY(value.Type == EValueType::Any);
                lightOperation.UpdateBriefProgress(TStringBuf(value.Data.String, value.Length));
            });
        }
    }

    filter.OnBriefProgressFinished();

    auto areAllRequestedAttributesLight = std::all_of(
        requestedAttributes.begin(),
        requestedAttributes.end(),
        [&] (const TString& attribute) {
            return LightAttributes.contains(attribute);
        });
    if (!areAllRequestedAttributesLight) {
        auto getBatchReq = proxy.ExecuteBatch();
        SetBalancingHeader(getBatchReq, options);

        const auto cypressRequestedAttributes = CreateCypressOperationAttributes(requestedAttributes);
        filter.ForEachOperationImmutable([&] (int index, const TListOperationsFilter::TLightOperation& lightOperation) {
            auto req = TYPathProxy::Get(GetOperationPath(lightOperation.GetId()));
            SetCachingHeader(req, options);
            ToProto(req->mutable_attributes()->mutable_keys(), cypressRequestedAttributes);
            getBatchReq->AddRequest(req);
        });

        auto getBatchRsp = WaitFor(getBatchReq->Invoke())
            .ValueOrThrow();
        auto responses = getBatchRsp->GetResponses<TYPathProxy::TRspGet>();
        filter.ForEachOperationMutable([&] (int index, TListOperationsFilter::TLightOperation& lightOperation) {
            const auto& rspOrError = responses[index];
            if (rspOrError.FindMatching(NYTree::EErrorCode::ResolveError)) {
                return;
            }
            lightOperation.SetYson(rspOrError.ValueOrThrow()->value());
        });
    }

    auto operations = filter.BuildOperations(requestedAttributes);
    idToOperation->reserve(idToOperation->size() + operations.size());
    for (auto& operation : operations) {
        (*idToOperation)[*operation.Id] = std::move(operation);
    }
}

THashMap<TOperationId, TOperation> TClient::LookupOperationsInArchiveTyped(
    const std::vector<TOperationId>& ids,
    const std::optional<THashSet<TString>>& attributes,
    std::optional<TDuration> timeout)
{
    const THashSet<TString> RequiredAttributes = {"id", "start_time"};
    const THashSet<TString> DefaultAttributes = {
        "authenticated_user",
        "brief_progress",
        "brief_spec",
        "finish_time",
        "id",
        "runtime_parameters",
        "start_time",
        "state",
        "type",
    };
    const THashSet<TString> IgnoredAttributes = {"suspended", "memory_usage"};

    auto attributesToRequest = DeduceActualAttributes(
        attributes,
        RequiredAttributes,
        DefaultAttributes,
        IgnoredAttributes);

    TOrderedByIdTableDescriptor tableDescriptor;
    std::vector<int> columns;
    for (const auto& columnName : CreateArchiveOperationAttributes(attributesToRequest)) {
        columns.push_back(tableDescriptor.NameTable->GetIdOrThrow(columnName));
    }
    auto columnFilter = NTableClient::TColumnFilter(columns);
    auto rowset = LookupOperationsInArchive(this, ids, columnFilter, timeout).ValueOrThrow();

    auto getYson = [&] (TUnversionedValue value) {
        // TODO(babenko): consider replacing with FromUnversionedValue as a whole.
        return value.Type == EValueType::Null
            ? TYsonString()
            : FromUnversionedValue<TYsonString>(value);
    };
    auto getString = [&] (TUnversionedValue value, TStringBuf name) {
        if (value.Type == EValueType::Null) {
            THROW_ERROR_EXCEPTION("Unexpected null value in column %Qv in job archive", name);
        }
        return FromUnversionedValue<TStringBuf>(value);
    };

    THashMap<TOperationId, TOperation> idToOperation;

    const auto& tableIndex = tableDescriptor.Index;
    auto idHiIndex = columnFilter.GetPosition(tableIndex.IdHi);
    auto idLoIndex = columnFilter.GetPosition(tableIndex.IdLo);
    auto typeIndex = columnFilter.FindPosition(tableIndex.OperationType);
    auto stateIndex = columnFilter.FindPosition(tableIndex.State);
    auto authenticatedUserIndex = columnFilter.FindPosition(tableIndex.AuthenticatedUser);
    auto startTimeIndex = columnFilter.FindPosition(tableIndex.StartTime);
    auto finishTimeIndex = columnFilter.FindPosition(tableIndex.FinishTime);
    auto briefSpecIndex = columnFilter.FindPosition(tableIndex.BriefSpec);
    auto fullSpecIndex = columnFilter.FindPosition(tableIndex.FullSpec);
    auto specIndex = columnFilter.FindPosition(tableIndex.Spec);
    auto unrecognizedSpecIndex = columnFilter.FindPosition(tableIndex.UnrecognizedSpec);
    auto briefProgressIndex = columnFilter.FindPosition(tableIndex.BriefProgress);
    auto progressIndex = columnFilter.FindPosition(tableIndex.Progress);
    auto runtimeParametersIndex = columnFilter.FindPosition(tableIndex.RuntimeParameters);
    auto eventsIndex = columnFilter.FindPosition(tableIndex.Events);
    auto resultIndex = columnFilter.FindPosition(tableIndex.Result);
    auto slotIndexPerPoolTreeIndex = columnFilter.FindPosition(tableIndex.SlotIndexPerPoolTree);
    auto alertsIndex = columnFilter.FindPosition(tableIndex.Alerts);
    auto taskNamesIndex = columnFilter.FindPosition(tableIndex.TaskNames);

    for (auto row : rowset->GetRows()) {
        if (!row) {
            continue;
        }

        TOperation operation;

        operation.Id = TOperationId(
            FromUnversionedValue<ui64>(row[idHiIndex]),
            FromUnversionedValue<ui64>(row[idLoIndex]));

        if (typeIndex) {
            operation.Type = ParseEnum<EOperationType>(getString(row[*typeIndex], "operation_type"));
        }

        if (stateIndex) {
            operation.State = ParseEnum<EOperationState>(getString(row[*stateIndex], "state"));
        }

        if (authenticatedUserIndex) {
            operation.AuthenticatedUser = TString(getString(row[*authenticatedUserIndex], "authenticated_user"));
        }

        if (startTimeIndex) {
            auto value = row[*startTimeIndex];
            if (value.Type == EValueType::Null) {
                THROW_ERROR_EXCEPTION("Unexpected null value in column start_time in operations archive");
            }
            operation.StartTime = TInstant::MicroSeconds(FromUnversionedValue<i64>(value));
        }

        if (finishTimeIndex) {
            if (row[*finishTimeIndex].Type != EValueType::Null) {
                operation.FinishTime = TInstant::MicroSeconds(FromUnversionedValue<i64>(row[*finishTimeIndex]));
            }
        }

        if (briefSpecIndex) {
            operation.BriefSpec = getYson(row[*briefSpecIndex]);
        }
        if (fullSpecIndex) {
            operation.FullSpec = getYson(row[*fullSpecIndex]);
        }
        if (specIndex) {
            operation.Spec = getYson(row[*specIndex]);
        }
        if (unrecognizedSpecIndex) {
            operation.UnrecognizedSpec = getYson(row[*unrecognizedSpecIndex]);
        }

        if (briefProgressIndex) {
            operation.BriefProgress = getYson(row[*briefProgressIndex]);
        }
        if (progressIndex) {
            operation.Progress = getYson(row[*progressIndex]);
        }

        if (runtimeParametersIndex) {
            operation.RuntimeParameters = getYson(row[*runtimeParametersIndex]);
        }

        if (eventsIndex) {
            operation.Events = getYson(row[*eventsIndex]);
        }
        if (resultIndex) {
            operation.Result = getYson(row[*resultIndex]);
        }

        if (slotIndexPerPoolTreeIndex) {
            operation.SlotIndexPerPoolTree = getYson(row[*slotIndexPerPoolTreeIndex]);
        }

        if (alertsIndex) {
            operation.Alerts = getYson(row[*alertsIndex]);
        }

        if (taskNamesIndex) {
            operation.TaskNames = getYson(row[*taskNamesIndex]);
        }

        idToOperation.emplace(*operation.Id, std::move(operation));
    }

    return idToOperation;
}

// Searches in archive for operations satisfying given filters.
// Returns operations with requested fields plus necessarily "start_time" and "id".
THashMap<TOperationId, TOperation> TClient::DoListOperationsFromArchive(
    TInstant deadline,
    TListOperationsCountingFilter& countingFilter,
    const TListOperationsOptions& options)
{
    if (!options.FromTime) {
        THROW_ERROR_EXCEPTION("Missing required parameter \"from_time\"");
    }

    if (!options.ToTime) {
        THROW_ERROR_EXCEPTION("Missing required parameter \"to_time\"");
    }

    if (options.AccessFilter) {
        constexpr int requiredVersion = 30;
        if (DoGetOperationsArchiveVersion() < requiredVersion) {
            THROW_ERROR_EXCEPTION("\"access\" filter is not supported in operations archive of version < %v",
                requiredVersion);
        }
    }

    auto addCommonWhereConjuncts = [&] (NQueryClient::TQueryBuilder* builder) {
        builder->AddWhereConjunct(Format("start_time > %v AND start_time <= %v",
            (*options.FromTime).MicroSeconds(),
            (*options.ToTime).MicroSeconds()));

        if (options.SubstrFilter) {
            builder->AddWhereConjunct(
                Format("is_substr(%Qv, filter_factors)", *options.SubstrFilter));
        }

        if (options.AccessFilter) {
            builder->AddWhereConjunct(Format("NOT is_null(acl) AND _yt_has_permissions(acl, %Qv, %Qv)",
                ConvertToYsonString(options.AccessFilter->SubjectTransitiveClosure, EYsonFormat::Text),
                ConvertToYsonString(options.AccessFilter->Permissions, EYsonFormat::Text)));
        }
    };

    if (options.IncludeCounters) {
        NQueryClient::TQueryBuilder builder;
        builder.SetSource(GetOperationsArchiveOrderedByStartTimePath());

        auto poolsIndex = builder.AddSelectExpression("pools_str");
        auto authenticatedUserIndex = builder.AddSelectExpression("authenticated_user");
        auto stateIndex = builder.AddSelectExpression("state");
        auto operationTypeIndex = builder.AddSelectExpression("operation_type");
        auto poolIndex = builder.AddSelectExpression("pool");
        auto hasFailedJobsIndex = builder.AddSelectExpression("has_failed_jobs");
        auto countIndex = builder.AddSelectExpression("sum(1)", "count");

        addCommonWhereConjuncts(&builder);

        builder.AddGroupByExpression("any_to_yson_string(pools)", "pools_str");
        builder.AddGroupByExpression("authenticated_user");
        builder.AddGroupByExpression("state");
        builder.AddGroupByExpression("operation_type");
        builder.AddGroupByExpression("pool");
        builder.AddGroupByExpression("has_failed_jobs");

        TSelectRowsOptions selectOptions;
        selectOptions.Timeout = deadline - Now();
        selectOptions.InputRowLimit = std::numeric_limits<i64>::max();
        selectOptions.MemoryLimitPerNode = 100_MB;

        auto resultCounts = WaitFor(SelectRows(builder.Build(), selectOptions))
            .ValueOrThrow();

        for (auto row : resultCounts.Rowset->GetRows()) {
            std::optional<std::vector<TString>> pools;
            if (row[poolsIndex].Type != EValueType::Null) {
                // NB: "any_to_yson_string" returns a string; cf. YT-12047.
                pools = ConvertTo<std::vector<TString>>(TYsonString(FromUnversionedValue<TString>(row[poolsIndex])));
            }
            auto user = FromUnversionedValue<TStringBuf>(row[authenticatedUserIndex]);
            auto state = ParseEnum<EOperationState>(FromUnversionedValue<TStringBuf>(row[stateIndex]));
            auto type = ParseEnum<EOperationType>(FromUnversionedValue<TStringBuf>(row[operationTypeIndex]));
            if (row[poolIndex].Type != EValueType::Null) {
                if (!pools) {
                    pools.emplace();
                }
                pools->push_back(FromUnversionedValue<TString>(row[poolIndex]));
            }
            auto count = FromUnversionedValue<i64>(row[countIndex]);
            if (!countingFilter.Filter(pools, user, state, type, count)) {
                continue;
            }

            bool hasFailedJobs = false;
            if (row[hasFailedJobsIndex].Type != EValueType::Null) {
                hasFailedJobs = FromUnversionedValue<bool>(row[hasFailedJobsIndex]);
            }
            countingFilter.FilterByFailedJobs(hasFailedJobs, count);
        }
    }

    NQueryClient::TQueryBuilder builder;
    builder.SetSource(GetOperationsArchiveOrderedByStartTimePath());

    auto idHiIndex = builder.AddSelectExpression("id_hi");
    auto idLoIndex = builder.AddSelectExpression("id_lo");

    addCommonWhereConjuncts(&builder);

    std::optional<EOrderByDirection> orderByDirection;

    switch (options.CursorDirection) {
        case EOperationSortDirection::Past:
            if (options.CursorTime) {
                builder.AddWhereConjunct(Format("start_time <= %v", (*options.CursorTime).MicroSeconds()));
            }
            orderByDirection = EOrderByDirection::Descending;
            break;
        case EOperationSortDirection::Future:
            if (options.CursorTime) {
                builder.AddWhereConjunct(Format("start_time > %v", (*options.CursorTime).MicroSeconds()));
            }
            orderByDirection = EOrderByDirection::Ascending;
            break;
        case EOperationSortDirection::None:
            break;
        default:
            YT_ABORT();
    }

    builder.AddOrderByExpression("start_time", orderByDirection);
    builder.AddOrderByExpression("id_hi", orderByDirection);
    builder.AddOrderByExpression("id_lo", orderByDirection);

    if (options.Pool) {
        builder.AddWhereConjunct(Format("list_contains(pools, %Qv) OR pool = %Qv", *options.Pool, *options.Pool));
    }

    if (options.StateFilter) {
        builder.AddWhereConjunct(Format("state = %Qv", FormatEnum(*options.StateFilter)));
    }

    if (options.TypeFilter) {
        builder.AddWhereConjunct(Format("operation_type = %Qv", FormatEnum(*options.TypeFilter)));
    }

    if (options.UserFilter) {
        builder.AddWhereConjunct(Format("authenticated_user = %Qv", *options.UserFilter));
    }

    if (options.WithFailedJobs) {
        if (*options.WithFailedJobs) {
            builder.AddWhereConjunct("has_failed_jobs");
        } else {
            builder.AddWhereConjunct("not has_failed_jobs");
        }
    }

    // Retain more operations than limit to track (in)completeness of the response.
    builder.SetLimit(1 + options.Limit);

    TSelectRowsOptions selectOptions;
    selectOptions.Timeout = deadline - Now();
    selectOptions.InputRowLimit = std::numeric_limits<i64>::max();
    selectOptions.MemoryLimitPerNode = 100_MB;
    auto rowsItemsId = WaitFor(SelectRows(builder.Build(), selectOptions))
        .ValueOrThrow();
    auto rows = rowsItemsId.Rowset->GetRows();

    std::vector<TOperationId> ids;
    ids.reserve(rows.Size());
    for (auto row : rows) {
        ids.emplace_back(FromUnversionedValue<ui64>(row[idHiIndex]), FromUnversionedValue<ui64>(row[idLoIndex]));
    }
    return LookupOperationsInArchiveTyped(ids, options.Attributes, deadline - Now());
}

// XXX(levysotsky): The counters may be incorrect if |options.IncludeArchive| is |true|
// and an operation is in both Cypress and archive.
TListOperationsResult TClient::DoListOperations(const TListOperationsOptions& oldOptions)
{
    auto options = oldOptions;

    auto timeout = options.Timeout.value_or(Connection_->GetConfig()->DefaultListOperationsTimeout);
    auto deadline = timeout.ToDeadLine();

    if (options.CursorTime && (
        (options.ToTime && *options.CursorTime > *options.ToTime) ||
        (options.FromTime && *options.CursorTime < *options.FromTime)))
    {
        THROW_ERROR_EXCEPTION("Time cursor (%v) is out of range [from_time (%v), to_time (%v)]",
            *options.CursorTime,
            *options.FromTime,
            *options.ToTime);
    }

    constexpr ui64 MaxLimit = 1000;
    if (options.Limit > MaxLimit) {
        THROW_ERROR_EXCEPTION("Requested result limit (%v) exceeds maximum allowed limit (%v)",
            options.Limit,
            MaxLimit);
    }

    if (options.SubstrFilter) {
        options.SubstrFilter = to_lower(*options.SubstrFilter);
    }

    if (options.AccessFilter) {
        TObjectServiceProxy proxy(GetOperationArchiveChannel(options.ReadFrom), Connection_->GetStickyGroupSizeCache());
        options.AccessFilter->SubjectTransitiveClosure = GetSubjectClosure(
            options.AccessFilter->Subject,
            proxy,
            Connection_->GetConfig(),
            options);
        if (options.AccessFilter->Subject == RootUserName ||
            options.AccessFilter->SubjectTransitiveClosure.contains(SuperusersGroupName))
        {
            options.AccessFilter.Reset();
        }
    }

    TListOperationsCountingFilter countingFilter(options);

    THashMap<NScheduler::TOperationId, TOperation> idToOperation;
    if (options.IncludeArchive && DoesOperationsArchiveExist()) {
        idToOperation = DoListOperationsFromArchive(
            deadline,
            countingFilter,
            options);
    }

    DoListOperationsFromCypress(
        deadline,
        countingFilter,
        options,
        &idToOperation);

    std::vector<TOperation> operations;
    operations.reserve(idToOperation.size());
    for (auto& item : idToOperation) {
        operations.push_back(std::move(item.second));
    }

    std::sort(operations.begin(), operations.end(), [&] (const TOperation& lhs, const TOperation& rhs) {
        // Reverse order: most recent first.
        return
        std::tie(*lhs.StartTime, (*lhs.Id).Parts64[0], (*lhs.Id).Parts64[1])
        >
        std::tie(*rhs.StartTime, (*rhs.Id).Parts64[0], (*rhs.Id).Parts64[1]);
    });

    TListOperationsResult result;

    result.Operations = std::move(operations);
    if (result.Operations.size() > options.Limit) {
        if (options.CursorDirection == EOperationSortDirection::Past) {
            result.Operations.resize(options.Limit);
        } else {
            result.Operations.erase(result.Operations.begin(), result.Operations.end() - options.Limit);
        }
        result.Incomplete = true;
    }

    // Fetching progress for operations with mentioned ids.
    if (DoesOperationsArchiveExist()) {
        std::vector<TOperationId> ids;
        for (const auto& operation: result.Operations) {
            ids.push_back(*operation.Id);
        }

        bool needBriefProgress = !options.Attributes || options.Attributes->contains("brief_progress");
        bool needProgress = options.Attributes && options.Attributes->contains("progress");

        TOrderedByIdTableDescriptor tableDescriptor;
        std::vector<int> columnIndices;
        if (needBriefProgress) {
            columnIndices.push_back(tableDescriptor.Index.BriefProgress);
        }
        if (needProgress) {
            columnIndices.push_back(tableDescriptor.Index.Progress);
        }

        auto columnFilter = NTableClient::TColumnFilter(columnIndices);
        auto rowsetOrError = LookupOperationsInArchive(
            this,
            ids,
            columnFilter,
            options.ArchiveFetchingTimeout);

        if (!rowsetOrError.IsOK()) {
            YT_LOG_DEBUG(rowsetOrError, "Failed to get information about operations' progress and brief_progress from Archive");
        } else {
            auto briefProgressPosition = columnFilter.FindPosition(tableDescriptor.Index.BriefProgress);
            auto progressPosition = columnFilter.FindPosition(tableDescriptor.Index.Progress);

            const auto& rows = rowsetOrError.Value()->GetRows();

            for (size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
                const auto& row = rows[rowIndex];
                if (!row) {
                    continue;
                }

                auto& operation = result.Operations[rowIndex];
                if (briefProgressPosition) {
                    auto briefProgressValue = row[*briefProgressPosition];
                    if (briefProgressValue.Type != EValueType::Null) {
                        auto briefProgressYsonString = FromUnversionedValue<TYsonString>(briefProgressValue);
                        operation.BriefProgress = GetLatestProgress(operation.BriefProgress, briefProgressYsonString);
                    }
                }
                if (progressPosition) {
                    auto progressValue = row[*progressPosition];
                    if (progressValue.Type != EValueType::Null) {
                        auto progressYsonString = FromUnversionedValue<TYsonString>(progressValue);
                        operation.Progress = GetLatestProgress(operation.Progress, progressYsonString);
                    }
                }
            }
        }
    }

    if (options.IncludeCounters) {
        result.PoolCounts = std::move(countingFilter.PoolCounts);
        result.UserCounts = std::move(countingFilter.UserCounts);
        result.StateCounts = std::move(countingFilter.StateCounts);
        result.TypeCounts = std::move(countingFilter.TypeCounts);
        result.FailedJobsCount = countingFilter.FailedJobsCount;
    }

    return result;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi::NNative
