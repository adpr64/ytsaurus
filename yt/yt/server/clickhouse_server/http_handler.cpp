#include "http_handler.h"

#include "query_context.h"
#include "host.h"
#include "helpers.h"

#include <Poco/Util/LayeredConfiguration.h>
#include <Server/HTTPHandler.h>
#include <Server/NotFoundHandler.h>
#include <Server/StaticRequestHandler.h>

#include <Access/AccessControlManager.h>
#include <Access/User.h>

#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <Poco/URI.h>

#include <common/getFQDNOrHostName.h>

#include <util/string/cast.h>

namespace NYT::NClickHouseServer {

using namespace NTracing;
using namespace NLogging;

////////////////////////////////////////////////////////////////////////////////

class TMovedPermanentlyRequestHandler
    : public Poco::Net::HTTPRequestHandler
{
public:
    TMovedPermanentlyRequestHandler(DB::IServer& server)
        : Server_(server)
    { }

    void handleRequest(
        Poco::Net::HTTPServerRequest & /* request */,
        Poco::Net::HTTPServerResponse & response) override
    {
        try {
            response.set("X-ClickHouse-Server-Display-Name", Server_.config().getString("display_name", getFQDNOrHostName()));
            response.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_MOVED_PERMANENTLY);
            response.send() << "Instance moved or is moving from this address.\n";
        } catch (...) {
            DB::tryLogCurrentException("MovedPermanentlyHandler");
        }
    }

private:
    DB::IServer& Server_;
};

class THttpHandler
    : public DB::DynamicQueryHandler
{
public:
    THttpHandler(THost* host, DB::IServer& server, const Poco::Net::HTTPServerRequest& request)
        : DB::DynamicQueryHandler(server, "handler")
        , Host_(host)
        , Server_(server)
    {
        TraceContext_ = SetupTraceContext(ClickHouseYtLogger, request);

        // By default, trace id coincides with query id. It makes significantly easier to
        // debug singular queries like those which are issued via YQL.
        // If trace id is external (i.e. suggested via traceparent header), we cannot use
        // trace id as a query id as it will result in several queries sharing same query id.
        // So, we form query id as a mix of trace id lower part and a span id.
        if (request.get("traceparent", "").empty()) {
            QueryId_ = TraceContext_->GetTraceId();
        } else {
            // If trace id = 11111111-22222222-33333333-44444444 and span id = 5555555566666666,
            // then query id will be 33333333-44444444-55555555-66666666.
            QueryId_.Parts64[1] = TraceContext_->GetTraceId().Parts64[0];
            QueryId_.Parts64[0] = TraceContext_->GetSpanId();
        }
    }

    virtual void customizeContext(Poco::Net::HTTPServerRequest & /* request */, DB::Context& context) override
    {
        YT_VERIFY(TraceContext_);

        // For HTTP queries (which are always initial) query id is same as trace id.
        context.getClientInfo().current_query_id = context.getClientInfo().initial_query_id = ToString(QueryId_);
        SetupHostContext(Host_, context, QueryId_, TraceContext_, DataLensRequestId_);
    }

    virtual void handleRequest(Poco::Net::HTTPServerRequest& request, Poco::Net::HTTPServerResponse& response) override
    {
        response.set("X-Yt-Trace-Id", ToString(TraceContext_->GetTraceId()));

        auto userName = request.get("X-ClickHouse-User", "");
        if (userName.empty()) {
            response.setStatusAndReason(Poco::Net::HTTPResponse::HTTP_UNAUTHORIZED);
            response.send() << "User name should be specified via X-ClickHouse-User header" << std::endl;
            return;
        }

        const auto& Logger = ClickHouseYtLogger;

        YT_LOG_DEBUG("Registering new user (UserName: %v)", userName);
        RegisterNewUser(Server_.context().getAccessControlManager(), TString(userName));
        YT_LOG_DEBUG("User registered");

        DB::HTTPHandler::handleRequest(request, response);
    }

private:
    THost* const Host_;
    DB::IServer& Server_;
    TTraceContextPtr TraceContext_;
    std::optional<TString> DataLensRequestId_;
    TQueryId QueryId_;

    //! If span is present in query headers, parse it and setup trace context which is its child.
    //! Otherwise, generate our own trace id (aka query id) and maybe generate root trace context
    //! if X-Yt-Sampled = true.
    TTraceContextPtr SetupTraceContext(
        const TLogger& logger,
        const Poco::Net::HTTPServerRequest& request)
    {
        const auto& Logger = logger;

        TSpanContext parentSpan;
        auto requestTraceId = request.get("X-Yt-Trace-Id", "");
        auto requestSpanId = request.get("X-Yt-Span-Id", "");
        if (!TTraceId::FromString(requestTraceId, &parentSpan.TraceId) ||
            !TryIntFromString<16>(requestSpanId, parentSpan.SpanId))
        {
            parentSpan = TSpanContext{TTraceId::Create(), InvalidSpanId, false, false};
            YT_LOG_INFO(
                "Parent span context is absent or not parseable, generating our own trace id aka query id "
                "(RequestTraceId: %Qv, RequestSpanId: %Qv, GeneratedTraceId: %v)",
                requestTraceId,
                requestSpanId,
                parentSpan.TraceId);
        } else {
            YT_LOG_INFO("Parsed parent span context (RequestTraceId: %Qv, RequestSpanId: %Qv)",
                requestTraceId,
                requestSpanId);
        }

        auto requestSampled = TString(request.get("X-Yt-Sampled", ""));
        int sampled;
        if (int intValue; TryIntFromString<10>(requestSampled, intValue) && intValue >= 0 && intValue <= 1) {
            YT_LOG_INFO("Parsed X-Yt-Sampled (RequestSampled: %Qv)", requestSampled);
            sampled = intValue;
        } else if (bool boolValue; TryFromString<bool>(requestSampled, boolValue)) {
            YT_LOG_INFO("Parsed X-Yt-Sampled (RequestSampled: %Qv)", requestSampled);
            sampled = boolValue;
        } else {
            YT_LOG_INFO("Cannot parse X-Yt-Sampled, assuming false (RequestSampled: %Qv)", requestSampled);
            sampled = 0;
        }

        auto traceContext = New<TTraceContext>(parentSpan, "HttpHandler");
        if (sampled == 1) {
            traceContext->SetSampled();
        }

        auto maybeDataLensRequestId = request.get("X-Request-Id", "");
        if (maybeDataLensRequestId.starts_with("dl.")) {
            YT_LOG_INFO("Request contains DataLens request id (RequestId: %v)", maybeDataLensRequestId);
            DataLensRequestId_ = TString(maybeDataLensRequestId);
        }

        return traceContext;
    }
};

////////////////////////////////////////////////////////////////////////////////

class THttpHandlerFactory
    : public Poco::Net::HTTPRequestHandlerFactory
{
public:
    THttpHandlerFactory(THost* host, DB::IServer& server)
        : Host_(host)
        , Server_(server)
    { }

    Poco::Net::HTTPRequestHandler* createRequestHandler(const Poco::Net::HTTPServerRequest& request) override
    {
        Poco::URI uri(request.getURI());

        const auto& Logger = ClickHouseYtLogger;
        YT_LOG_INFO("HTTP request received (Method: %v, URI: %v, Address: %v, UserAgent: %v)",
            request.getMethod(),
            uri.toString(),
            request.clientAddress().toString(),
            (request.has("User-Agent") ? request.get("User-Agent") : "none"));

        // Light health-checking requests.
        if (request.getMethod() == Poco::Net::HTTPServerRequest::HTTP_HEAD ||
            request.getMethod() == Poco::Net::HTTPServerRequest::HTTP_GET)
        {
            if (uri == "/" || uri == "/ping") {
                return new DB::StaticRequestHandler(Server_, "Ok.\n");
            }
        }

        auto cliqueId = request.find("X-Clique-Id");
        if (Host_->GetInstanceState() == EInstanceState::Stopped ||
            (cliqueId != request.end() && TString(cliqueId->second) != ToString(Host_->GetConfig()->CliqueId)))
        {
            return new TMovedPermanentlyRequestHandler(Server_);
        }

        if (request.getMethod() == Poco::Net::HTTPServerRequest::HTTP_GET ||
            request.getMethod() == Poco::Net::HTTPServerRequest::HTTP_POST)
        {
            if ((uri.getPath() == "/") ||
                (uri.getPath() == "/query")) {
                auto* handler = new THttpHandler(Host_, Server_, request);
                return handler;
            }
        }

        return new DB::NotFoundHandler();
    }

private:
    THost* Host_;
    DB::IServer& Server_;
};

////////////////////////////////////////////////////////////////////////////////

Poco::Net::HTTPRequestHandlerFactory::Ptr CreateHttpHandlerFactory(THost* host, DB::IServer& server)
{
    return new THttpHandlerFactory(host, server);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NClickHouseServer
