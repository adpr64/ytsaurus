#include "batch_request_impl.h"
#include "mock_client.h"
#include "operation.h"
#include "rpc_parameters_serialization.h"

#include <mapreduce/yt/interface/client.h>

#include <mapreduce/yt/common/log.h>
#include <mapreduce/yt/common/helpers.h>
#include <mapreduce/yt/common/config.h>
#include <mapreduce/yt/common/fluent.h>
#include <mapreduce/yt/common/finally_guard.h>

#include <mapreduce/yt/http/http.h>
#include <mapreduce/yt/http/requests.h>
#include <mapreduce/yt/http/retry_request.h>

#include <mapreduce/yt/io/client_reader.h>
#include <mapreduce/yt/io/client_writer.h>
#include <mapreduce/yt/io/job_reader.h>
#include <mapreduce/yt/io/job_writer.h>
#include <mapreduce/yt/io/yamr_table_reader.h>
#include <mapreduce/yt/io/yamr_table_writer.h>
#include <mapreduce/yt/io/node_table_reader.h>
#include <mapreduce/yt/io/node_table_writer.h>
#include <mapreduce/yt/io/proto_table_reader.h>
#include <mapreduce/yt/io/proto_table_writer.h>
#include <mapreduce/yt/io/proto_helpers.h>
#include <mapreduce/yt/io/file_reader.h>
#include <mapreduce/yt/io/file_writer.h>
#include <mapreduce/yt/io/block_writer.h>

#include <library/yson/json_writer.h>

#include <exception>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

ITransactionPtr CreateTransactionObject(
    const TAuth& auth,
    const TTransactionId& transactionId,
    bool isOwning,
    const TStartTransactionOptions& options = TStartTransactionOptions());

////////////////////////////////////////////////////////////////////////////////

class TClientBase
    : virtual public IClientBase
{
public:
    explicit TClientBase(
        const TAuth& auth,
        const TTransactionId& transactionId = TTransactionId())
        : Auth_(auth)
        , TransactionId_(transactionId)
    { }

    ITransactionPtr StartTransaction(
        const TStartTransactionOptions& options) override
    {
        return CreateTransactionObject(Auth_, TransactionId_, true, options);
    }

    // cypress

    TNodeId Create(
        const TYPath& path,
        ENodeType type,
        const TCreateOptions& options) override
    {
        THttpHeader header("POST", "create");
        header.AddMutationId();
        header.SetParameters(NDetail::SerializeParamsForCreate(TransactionId_, path, type, options));
        return ParseGuidFromResponse(RetryRequest(Auth_, header));
    }

    void Remove(
        const TYPath& path,
        const TRemoveOptions& options) override
    {
        THttpHeader header("POST", "remove");
        header.AddMutationId();
        header.SetParameters(NDetail::SerializeParamsForRemove(TransactionId_, path, options));
        RetryRequest(Auth_, header);
    }

    bool Exists(
        const TYPath& path) override
    {
        THttpHeader header("GET", "exists");
        header.SetParameters(NDetail::SerializeParamsForExists(TransactionId_, path));
        return ParseBoolFromResponse(RetryRequest(Auth_, header));
    }

    TNode Get(
        const TYPath& path,
        const TGetOptions& options) override
    {
        THttpHeader header("GET", "get");
        header.SetParameters(NDetail::SerializeParamsForGet(TransactionId_, path, options));
        return NodeFromYsonString(RetryRequest(Auth_, header));
    }

    void Set(
        const TYPath& path,
        const TNode& value) override
    {
        THttpHeader header("PUT", "set");
        header.AddMutationId();
        header.SetParameters(NDetail::SerializeParamsForSet(TransactionId_, path));
        RetryRequest(Auth_, header, NodeToYsonString(value));
    }

    TNode::TList List(
        const TYPath& path,
        const TListOptions& options) override
    {
        THttpHeader header("GET", "list");

        TYPath updatedPath = AddPathPrefix(path);
        // FIXME: ugly but quick empty path special case
        // Translate "//" to "/"
        // Translate "//some/constom/prefix/from/config/" to "//some/constom/prefix/from/config"
        if (path.empty() && updatedPath.EndsWith('/')) {
            updatedPath.pop_back();
        }
        header.SetParameters(NDetail::SerializeParamsForList(TransactionId_, updatedPath, options));
        return NodeFromYsonString(RetryRequest(Auth_, header)).AsList();
    }

    TNodeId Copy(
        const TYPath& sourcePath,
        const TYPath& destinationPath,
        const TCopyOptions& options) override
    {
        THttpHeader header("POST", "copy");
        header.AddParam("source_path", AddPathPrefix(sourcePath));
        header.AddParam("destination_path", AddPathPrefix(destinationPath));
        header.AddTransactionId(TransactionId_);
        header.AddMutationId();

        header.AddParam("recursive", options.Recursive_);
        header.AddParam("force", options.Force_);
        header.AddParam("preserve_account", options.PreserveAccount_);
        if (options.PreserveExpirationTime_) {
            header.AddParam("preserve_expiration_time", *options.PreserveExpirationTime_);
        }
        return ParseGuidFromResponse(RetryRequest(Auth_, header));
    }

    TNodeId Move(
        const TYPath& sourcePath,
        const TYPath& destinationPath,
        const TMoveOptions& options) override
    {
        THttpHeader header("POST", "move");
        header.AddParam("source_path", AddPathPrefix(sourcePath));
        header.AddParam("destination_path", AddPathPrefix(destinationPath));
        header.AddTransactionId(TransactionId_);
        header.AddMutationId();

        header.AddParam("recursive", options.Recursive_);
        header.AddParam("force", options.Force_);
        header.AddParam("preserve_account", options.PreserveAccount_);
        if (options.PreserveExpirationTime_) {
            header.AddParam("preserve_expiration_time", *options.PreserveExpirationTime_);
        }
        return ParseGuidFromResponse(RetryRequest(Auth_, header));
    }

    TNodeId Link(
        const TYPath& targetPath,
        const TYPath& linkPath,
        const TLinkOptions& options) override
    {
        THttpHeader header("POST", "link");
        header.AddParam("target_path", AddPathPrefix(targetPath));
        header.AddParam("link_path", AddPathPrefix(linkPath));
        header.AddTransactionId(TransactionId_);
        header.AddMutationId();

        header.AddParam("recursive", options.Recursive_);
        header.AddParam("ignore_existing", options.IgnoreExisting_);
        if (options.Attributes_) {
            header.SetParameters(AttributesToYsonString(*options.Attributes_));
        }
        return ParseGuidFromResponse(RetryRequest(Auth_, header));
    }

    void Concatenate(
        const yvector<TYPath>& sourcePaths,
        const TYPath& destinationPath,
        const TConcatenateOptions& options) override
    {
        THttpHeader header("POST", "concatenate");
        header.AddTransactionId(TransactionId_);
        header.AddMutationId();

        TRichYPath path(AddPathPrefix(destinationPath));
        path.Append(options.Append_);
        header.SetParameters(BuildYsonStringFluently().BeginMap()
            .Item("source_paths").DoListFor(sourcePaths,
                [] (TFluentList fluent, const TYPath& thePath) {
                    fluent.Item().Value(AddPathPrefix(thePath));
                })
            .Item("destination_path").Value(path)
        .EndMap());

        RetryRequest(Auth_, header);
    }

    // io

    IFileReaderPtr CreateFileReader(
        const TRichYPath& path,
        const TFileReaderOptions& options) override
    {
        return new TFileReader(
            CanonizePath(Auth_, path),
            Auth_,
            TransactionId_,
            options);
    }

    IFileWriterPtr CreateFileWriter(
        const TRichYPath& path,
        const TFileWriterOptions& options) override
    {
        auto realPath = CanonizePath(Auth_, path);
        if (!NYT::Exists(Auth_, TransactionId_, realPath.Path_)) {
            NYT::Create(Auth_, TransactionId_, realPath.Path_, "file");
        }
        return new TFileWriter(realPath, Auth_, TransactionId_, options);
    }

    TRawTableReaderPtr CreateRawReader(
        const TRichYPath& path,
        EDataStreamFormat format,
        const TTableReaderOptions& options,
        const TString& formatConfig = TString()) override
    {
        return CreateClientReader(path, format, options, formatConfig).Get();
    }

    TRawTableWriterPtr CreateRawWriter(
        const TRichYPath& path,
        EDataStreamFormat format,
        const TTableWriterOptions& options,
        const TString& formatConfig = TString()) override
    {
        return ::MakeIntrusive<TBlockWriter>(
            Auth_,
            TransactionId_,
            GetWriteTableCommand(),
            format,
            formatConfig,
            CanonizePath(Auth_, path),
            BUFFER_SIZE,
            options).Get();
    }

    // operations

    TOperationId DoMap(
        const TMapOperationSpec& spec,
        IJob* mapper,
        const TOperationOptions& options) override
    {
        return ExecuteMap(
            Auth_,
            TransactionId_,
            spec,
            mapper,
            options);
    }

    TOperationId DoReduce(
        const TReduceOperationSpec& spec,
        IJob* reducer,
        const TOperationOptions& options) override
    {
        return ExecuteReduce(
            Auth_,
            TransactionId_,
            spec,
            reducer,
            options);
    }

    TOperationId DoJoinReduce(
        const TJoinReduceOperationSpec& spec,
        IJob* reducer,
        const TOperationOptions& options) override
    {
        return ExecuteJoinReduce(
            Auth_,
            TransactionId_,
            spec,
            reducer,
            options);
    }

    TOperationId DoMapReduce(
        const TMapReduceOperationSpec& spec,
        IJob* mapper,
        IJob* reduceCombiner,
        IJob* reducer,
        const TMultiFormatDesc& outputMapperDesc,
        const TMultiFormatDesc& inputReduceCombinerDesc,
        const TMultiFormatDesc& outputReduceCombinerDesc,
        const TMultiFormatDesc& inputReducerDesc,
        const TOperationOptions& options) override
    {
        return ExecuteMapReduce(
            Auth_,
            TransactionId_,
            spec,
            mapper,
            reduceCombiner,
            reducer,
            outputMapperDesc,
            inputReduceCombinerDesc,
            outputReduceCombinerDesc,
            inputReducerDesc,
            options);
    }

    TOperationId Sort(
        const TSortOperationSpec& spec,
        const TOperationOptions& options) override
    {
        return ExecuteSort(
            Auth_,
            TransactionId_,
            spec,
            options);
    }

    TOperationId Merge(
        const TMergeOperationSpec& spec,
        const TOperationOptions& options) override
    {
        return ExecuteMerge(
            Auth_,
            TransactionId_,
            spec,
            options);
    }

    TOperationId Erase(
        const TEraseOperationSpec& spec,
        const TOperationOptions& options) override
    {
        return ExecuteErase(
            Auth_,
            TransactionId_,
            spec,
            options);
    }

    EOperationStatus CheckOperation(const TOperationId& operationId) override
    {
        return NYT::CheckOperation(Auth_, TransactionId_, operationId);
    }

    void AbortOperation(const TOperationId& operationId) override
    {
        NYT::AbortOperation(Auth_, TransactionId_, operationId);
    }

    void WaitForOperation(const TOperationId& operationId) override
    {
        NYT::WaitForOperation(Auth_, TransactionId_, operationId);
    }

    // schema

    void AlterTable(
        const TYPath& path,
        const TAlterTableOptions& options = TAlterTableOptions()) override
    {
        THttpHeader header("POST", "alter_table");
        header.AddTransactionId(TransactionId_);
        header.AddPath(AddPathPrefix(path));

        if (options.Dynamic_) {
            header.AddParam("dynamic", *options.Dynamic_);
        }
        if (options.Schema_) {
            header.SetParameters(BuildYsonStringFluently().BeginMap()
                .Item("schema")
                .Value(*options.Schema_)
            .EndMap());
        }
        RetryRequest(Auth_, header);
    }

protected:
    const TAuth Auth_;
    TTransactionId TransactionId_;

private:
    ::TIntrusivePtr<TClientReader> CreateClientReader(
        const TRichYPath& path,
        EDataStreamFormat format,
        const TTableReaderOptions& options,
        const TString& formatConfig = TString())
    {
        return ::MakeIntrusive<TClientReader>(
            CanonizePath(Auth_, path),
            Auth_,
            TransactionId_,
            format,
            formatConfig,
            options);
    }

    THolder<TClientWriter> CreateClientWriter(
        const TRichYPath& path,
        EDataStreamFormat format,
        const TTableWriterOptions& options,
        const TString& formatConfig = TString())
    {
        auto realPath = CanonizePath(Auth_, path);
        if (!NYT::Exists(Auth_, TransactionId_, realPath.Path_)) {
            NYT::Create(Auth_, TransactionId_, realPath.Path_, "table");
        }
        return MakeHolder<TClientWriter>(
            realPath, Auth_, TransactionId_, format, formatConfig, options);
    }

    ::TIntrusivePtr<INodeReaderImpl> CreateNodeReader(
        const TRichYPath& path, const TTableReaderOptions& options) override
    {
        return new TNodeTableReader(
            CreateClientReader(path, DSF_YSON_BINARY, options), options.SizeLimit_);
    }

    ::TIntrusivePtr<IYaMRReaderImpl> CreateYaMRReader(
        const TRichYPath& path, const TTableReaderOptions& options) override
    {
        return new TYaMRTableReader(
            CreateClientReader(path, DSF_YAMR_LENVAL, options));
    }

    ::TIntrusivePtr<IProtoReaderImpl> CreateProtoReader(
        const TRichYPath& path,
        const TTableReaderOptions& options,
        const Message* prototype) override
    {
        yvector<const ::google::protobuf::Descriptor*> descriptors;
        descriptors.push_back(prototype->GetDescriptor());

        if (TConfig::Get()->UseClientProtobuf) {
            return new TProtoTableReader(
                CreateClientReader(path, DSF_YSON_BINARY, options),
                std::move(descriptors));
        } else {
            auto formatConfig = NodeToYsonString(MakeProtoFormatConfig(prototype));
            return new TLenvalProtoTableReader(
                CreateClientReader(path, DSF_PROTO, options, formatConfig),
                std::move(descriptors));
        }
    }

    ::TIntrusivePtr<INodeWriterImpl> CreateNodeWriter(
        const TRichYPath& path, const TTableWriterOptions& options) override
    {
        return new TNodeTableWriter(
            CreateClientWriter(path, DSF_YSON_BINARY, options));
    }

    ::TIntrusivePtr<IYaMRWriterImpl> CreateYaMRWriter(
        const TRichYPath& path, const TTableWriterOptions& options) override
    {
        return new TYaMRTableWriter(
            CreateClientWriter(path, DSF_YAMR_LENVAL, options));
    }

    ::TIntrusivePtr<IProtoWriterImpl> CreateProtoWriter(
        const TRichYPath& path,
        const TTableWriterOptions& options,
        const Message* prototype) override
    {
        yvector<const ::google::protobuf::Descriptor*> descriptors;
        descriptors.push_back(prototype->GetDescriptor());

        if (TConfig::Get()->UseClientProtobuf) {
            return new TProtoTableWriter(
                CreateClientWriter(path, DSF_YSON_BINARY, options),
                std::move(descriptors));
        } else {
            auto formatConfig = NodeToYsonString(MakeProtoFormatConfig(prototype));
            return new TLenvalProtoTableWriter(
                CreateClientWriter(path, DSF_PROTO, options, formatConfig),
                std::move(descriptors));
        }
    }

    // Raw table writer buffer size
    static const size_t BUFFER_SIZE;
};

const size_t TClientBase::BUFFER_SIZE = 64 << 20;

class TTransaction
    : public ITransaction
    , public TClientBase
{
public:
    TTransaction(
        const TAuth& auth,
        const TTransactionId& transactionId,
        bool isOwning,
        const TStartTransactionOptions& options)
        : TClientBase(auth)
        , PingableTx_(isOwning ?
            new TPingableTransaction(
                auth,
                transactionId, // parent id
                options.Timeout_,
                options.PingAncestors_,
                options.Attributes_)
            : nullptr)
    {
        TransactionId_ = isOwning ? PingableTx_->GetId() : transactionId;
    }

    const TTransactionId& GetId() const override
    {
        return TransactionId_;
    }

    TLockId Lock(
        const TYPath& path,
        ELockMode mode,
        const TLockOptions& options) override
    {
        THttpHeader header("POST", "lock");
        header.AddPath(AddPathPrefix(path));
        header.AddParam("mode", NDetail::ToString(mode));
        header.AddTransactionId(TransactionId_);
        header.AddMutationId();

        header.AddParam("waitable", options.Waitable_);
        if (options.AttributeKey_) {
            header.AddParam("attribute_key", *options.AttributeKey_);
        }
        if (options.ChildKey_) {
            header.AddParam("child_key", *options.ChildKey_);
        }
        return ParseGuidFromResponse(RetryRequest(Auth_, header));
    }

    void Commit() override
    {
        if (PingableTx_) {
            PingableTx_->Commit();
        } else {
            CommitTransaction(Auth_, TransactionId_);
        }
    }

    void Abort() override
    {
        if (PingableTx_) {
            PingableTx_->Abort();
        } else {
            AbortTransaction(Auth_, TransactionId_);
        }
    }

private:
    THolder<TPingableTransaction> PingableTx_;
};

ITransactionPtr CreateTransactionObject(
    const TAuth& auth,
    const TTransactionId& transactionId,
    bool isOwning,
    const TStartTransactionOptions& options)
{
    return new TTransaction(auth, transactionId, isOwning, options);
}

////////////////////////////////////////////////////////////////////////////////

class TClient
    : public IClient
    , public TClientBase
{
public:
    TClient(
        const TAuth& auth,
        const TTransactionId& globalId)
        : TClientBase(auth, globalId)
    { }

    ITransactionPtr AttachTransaction(
        const TTransactionId& transactionId) override
    {
        return CreateTransactionObject(Auth_, transactionId, false);
    }

    void MountTable(
        const TYPath& path,
        const TMountTableOptions& options = TMountTableOptions()) override
    {
        THttpHeader header("POST", "mount_table");
        SetTabletParams(header, path, options);
        if (options.CellId_) {
            header.AddParam("cell_id", GetGuidAsString(*options.CellId_));
        }
        header.AddParam("freeze", options.Freeze_);
        RetryRequest(Auth_, header);
    }

    void UnmountTable(
        const TYPath& path,
        const TUnmountTableOptions& options = TUnmountTableOptions()) override
    {
        THttpHeader header("POST", "unmount_table");
        SetTabletParams(header, path, options);
        header.AddParam("force", options.Force_);
        RetryRequest(Auth_, header);
    }

    void RemountTable(
        const TYPath& path,
        const TRemountTableOptions& options = TRemountTableOptions()) override
    {
        THttpHeader header("POST", "remount_table");
        SetTabletParams(header, path, options);
        RetryRequest(Auth_, header);
    }

    void ReshardTable(
        const TYPath& path,
        const yvector<TKey>& keys,
        const TReshardTableOptions& options = TReshardTableOptions()) override
    {
        THttpHeader header("POST", "reshard_table");
        SetTabletParams(header, path, options);
        header.SetParameters(BuildYsonStringFluently().BeginMap()
            .Item("pivot_keys").List(keys)
        .EndMap());
        RetryRequest(Auth_, header);
    }

    void ReshardTable(
        const TYPath& path,
        i32 tabletCount,
        const TReshardTableOptions& options = TReshardTableOptions()) override
    {
        THttpHeader header("POST", "reshard_table");
        SetTabletParams(header, path, options);
        header.AddParam("tablet_count", static_cast<i64>(tabletCount));
        RetryRequest(Auth_, header);
    }

    void InsertRows(
        const TYPath& path,
        const TNode::TList& rows,
        const TInsertRowsOptions& options) override
    {
        THttpHeader header("PUT", "insert_rows");
        header.SetDataStreamFormat(DSF_YSON_BINARY);
        header.SetParameters(NDetail::SerializeParametersForInsertRows(path, options));

        auto body = NodeListToYsonString(rows);
        RetryRequest(Auth_, header, body, true);
    }

    void DeleteRows(
        const TYPath& path,
        const TNode::TList& keys,
        const TDeleteRowsOptions& options) override
    {
        THttpHeader header("PUT", "delete_rows");
        header.SetDataStreamFormat(DSF_YSON_BINARY);
        header.SetParameters(NDetail::SerializeParametersForDeleteRows(path, options));

        auto body = NodeListToYsonString(keys);
        RetryRequest(Auth_, header, body, true);
    }

    TNode::TList LookupRows(
        const TYPath& path,
        const TNode::TList& keys,
        const TLookupRowsOptions& options) override
    {
        Y_UNUSED(options);
        THttpHeader header("PUT", "lookup_rows");
        header.AddPath(AddPathPrefix(path));
        header.SetDataStreamFormat(DSF_YSON_BINARY);

        header.SetParameters(BuildYsonStringFluently().BeginMap()
            .DoIf(options.Timeout_.Defined(), [&] (TFluentMap fluent) {
                fluent.Item("timeout").Value(static_cast<i64>(options.Timeout_->MilliSeconds()));
            })
            .Item("keep_missing_rows").Value(options.KeepMissingRows_)
            .DoIf(options.Columns_.Defined(), [&] (TFluentMap fluent) {
                fluent.Item("column_names").Value(*options.Columns_);
            })
        .EndMap());

        auto body = NodeListToYsonString(keys);
        auto response = RetryRequest(Auth_, header, body, true);
        return NodeFromYsonString(response, YT_LIST_FRAGMENT).AsList();
    }

    TNode::TList SelectRows(
        const TString& query,
        const TSelectRowsOptions& options) override
    {
        THttpHeader header("GET", "select_rows");
        header.SetDataStreamFormat(DSF_YSON_BINARY);

        header.SetParameters(BuildYsonStringFluently().BeginMap()
            .Item("query").Value(query)
            .DoIf(options.Timeout_.Defined(), [&] (TFluentMap fluent) {
                fluent.Item("timeout").Value(static_cast<i64>(options.Timeout_->MilliSeconds()));
            })
            .DoIf(options.InputRowLimit_.Defined(), [&] (TFluentMap fluent) {
                fluent.Item("input_row_limit").Value(*options.InputRowLimit_);
            })
            .DoIf(options.OutputRowLimit_.Defined(), [&] (TFluentMap fluent) {
                fluent.Item("output_row_limit").Value(*options.OutputRowLimit_);
            })
            .Item("range_expansion_limit").Value(options.RangeExpansionLimit_)
            .Item("fail_on_incomplete_result").Value(options.FailOnIncompleteResult_)
            .Item("verbose_logging").Value(options.VerboseLogging_)
            .Item("enable_code_cache").Value(options.EnableCodeCache_)
        .EndMap());

        auto response = RetryRequest(Auth_, header, "", true);
        return NodeFromYsonString(response, YT_LIST_FRAGMENT).AsList();
    }

    void EnableTableReplica(const TReplicaId& replicaid) override {
        THttpHeader header("POST", "enable_table_replica");
        header.AddParam("replica_id", GetGuidAsString(replicaid));
        RetryRequest(Auth_, header);
    }

    void DisableTableReplica(const TReplicaId& replicaid) override {
        THttpHeader header("POST", "disable_table_replica");
        header.AddParam("replica_id", GetGuidAsString(replicaid));
        RetryRequest(Auth_, header);
    }

    ui64 GenerateTimestamp() override {
        THttpHeader header("GET", "generate_timestamp");
        auto response = RetryRequest(Auth_, header, "", true);
        return NodeFromYsonString(response).AsUint64();
    }

    void ExecuteBatch(const TBatchRequest& request, const TExecuteBatchOptions& options) override
    {
        if (request.Impl_->IsExecuted()) {
            ythrow yexception() << "Cannot execute batch request since it is alredy executed";
        }
        NDetail::TFinallyGuard g([&] {
            request.Impl_->MarkExecuted();
        });

        NDetail::TAttemptLimitedRetryPolicy retryPolicy(TConfig::Get()->RetryCount);

        const auto concurrency = options.Concurrency_.GetOrElse(50);
        const auto batchPartMaxSize = options.BatchPartMaxSize_.GetOrElse(concurrency * 5);

        while (request.Impl_->BatchSize()) {
            NDetail::TBatchRequestImpl retryBatch;

            while (request.Impl_->BatchSize()) {
                auto parameters = TNode::CreateMap();
                TInstant nextTry;
                request.Impl_->FillParameterList(batchPartMaxSize, &parameters["requests"], &nextTry);
                if (nextTry) {
                    SleepUntil(nextTry);
                }
                parameters["concurrency"] = concurrency;
                auto body = NodeToYsonString(parameters);
                THttpHeader header("POST", "execute_batch");
                header.AddMutationId();
                NDetail::TResponseInfo result;
                try {
                    result = RetryRequest(Auth_, header, body, retryPolicy);
                } catch (const yexception& e) {
                    request.Impl_->SetErrorResult(std::current_exception());
                    retryBatch.SetErrorResult(std::current_exception());
                    throw;
                }
                request.Impl_->ParseResponse(std::move(result), retryPolicy, &retryBatch);
            }

            *request.Impl_ = std::move(retryBatch);
        }
    }

private:
    template <class TOptions>
    void SetTabletParams(
        THttpHeader& header,
        const TYPath& path,
        const TOptions& options)
    {
        header.AddPath(AddPathPrefix(path));
        if (options.FirstTabletIndex_) {
            header.AddParam("first_tablet_index", *options.FirstTabletIndex_);
        }
        if (options.LastTabletIndex_) {
            header.AddParam("last_tablet_index", *options.LastTabletIndex_);
        }
    }
};

IClientPtr CreateClient(
    const TString& serverName,
    const TCreateClientOptions& options)
{
    bool mockRun = getenv("YT_CLIENT_MOCK_RUN") ? FromString<bool>(getenv("YT_CLIENT_MOCK_RUN")) : false;
    if (mockRun) {
        LOG_INFO("Running client in mock regime");
        return new TMockClient();
    }

    auto globalTxId = GetGuid(TConfig::Get()->GlobalTxId);

    TAuth auth;
    auth.ServerName = serverName;
    if (serverName.find('.') == TString::npos &&
        serverName.find(':') == TString::npos)
    {
        auth.ServerName += ".yt.yandex.net";
    }

    auth.Token = TConfig::Get()->Token;
    if (options.Token_) {
        auth.Token = options.Token_;
    } else if (options.TokenPath_) {
        auth.Token = TConfig::LoadTokenFromFile(options.TokenPath_);
    }
    TConfig::ValidateToken(auth.Token);

    return new TClient(auth, globalTxId);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
