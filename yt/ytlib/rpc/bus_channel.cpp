#include "stdafx.h"
#include "bus_channel.h"
#include "private.h"
#include "client.h"
#include "message.h"
#include "rpc_dispatcher.h"

#include <ytlib/misc/delayed_invoker.h>
#include <ytlib/misc/thread_affinity.h>

#include <ytlib/bus/bus.h>
#include <ytlib/bus/tcp_client.h>
#include <ytlib/bus/config.h>

#include <ytlib/ytree/ypath_client.h>

#include <ytlib/rpc/rpc.pb.h>

namespace NYT {
namespace NRpc {

using namespace NBus;
using namespace NYTree;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = RpcClientLogger;
static NProfiling::TProfiler& Profiler = RpcClientProfiler;

////////////////////////////////////////////////////////////////////////////////

namespace {

class TChannel
    : public IChannel
{
public:
    TChannel(IBusClientPtr client, TNullable<TDuration> defaultTimeout)
        : Client(MoveRV(client))
        , DefaultTimeout(defaultTimeout)
        , Terminated(false)
    {
        YCHECK(Client);
    }

    virtual TNullable<TDuration> GetDefaultTimeout() const override
    {
        return DefaultTimeout;
    }

    virtual bool GetRetryEnabled() const override
    {
        return false;
    }

    virtual void Send(
        IClientRequestPtr request,
        IClientResponseHandlerPtr responseHandler,
        TNullable<TDuration> timeout) override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        auto sessionOrError = GetOrCreateSession();
        if (!sessionOrError.IsOK()) {
            responseHandler->OnError(sessionOrError);
            return;
        }

        sessionOrError.Value()->Send(request, responseHandler, timeout);
    }

    virtual void Terminate(const TError& error) override
    {
        YCHECK(!error.IsOK());
        VERIFY_THREAD_AFFINITY_ANY();

        TSessionPtr session;
        {
            TGuard<TSpinLock> guard(SpinLock);
            
            if (Terminated) {
                return;
            }
            
            session = Session;
            Session.Reset();

            Terminated = true;
            TerminationError = error;
        }

        if (session) {
            session->Terminate(error);
        }
    }

private:
    class TSession;
    typedef TIntrusivePtr<TSession> TSessionPtr;

    //! Provides a weak wrapper around a session and breaks the cycle
    //! between the session and its underlying bus.
    class TMessageHandler
        : public IMessageHandler
    {
    public:
        explicit TMessageHandler(TSessionPtr session)
            : Session(session)
        { }

        virtual void OnMessage(IMessagePtr message, IBusPtr replyBus) override
        {
            auto session_ = Session.Lock();
            if (session_) {
                session_->OnMessage(message, replyBus);
            }
        }

    private:
        TWeakPtr<TSession> Session;

    };

    //! Directs requests sent via a channel to go through its underlying bus.
    //! Terminates when the underlying bus does so.
    class TSession
        : public IMessageHandler
    {
    public:
        explicit TSession(TNullable<TDuration> defaultTimeout)
            : DefaultTimeout(defaultTimeout)
            , Terminated(false)
        { }

        void Init(IBusPtr bus)
        {
            YCHECK(bus);
            Bus = bus;
        }

        void Terminate(const TError& error)
        {
            // Mark the channel as terminated to disallow any further usage.
            // Swap out all active requests and mark them as failed.
            TRequestMap activeRequests;

            {
                TGuard<TSpinLock> guard(SpinLock);
                Terminated = true;
                TerminationError = error;
                activeRequests.swap(ActiveRequests);
            }

            FOREACH (auto& pair, activeRequests) {
                const auto& requestId = pair.first;
                auto& request = pair.second;
                LOG_DEBUG("Request failed due to channel termination (RequestId: %s)",
                    ~requestId.ToString());
                FinalizeRequest(request);
                request.ResponseHandler->OnError(error);
            }
        }

        void Send(
            IClientRequestPtr request,
            IClientResponseHandlerPtr responseHandler,
            TNullable<TDuration> timeout)
        {
            YCHECK(request);
            YCHECK(responseHandler);
            VERIFY_THREAD_AFFINITY_ANY();

            auto requestId = request->GetRequestId();

            TActiveRequest activeRequest;
            activeRequest.ClientRequest = request;
            activeRequest.ResponseHandler = responseHandler;
            activeRequest.Timer = Profiler.TimingStart(
                "/services/" +
                EscapeYPathToken(request->GetPath()) + "/" +
                EscapeYPathToken(request->GetVerb()) +
                "/time",
                NProfiling::ETimerMode::Sequential);

            IBusPtr bus;
            {
                TGuard<TSpinLock> guard(SpinLock);

                if (Terminated) {
                    auto error = TerminationError;
                    guard.Release();

                    LOG_DEBUG("Request via terminated channel is dropped (RequestId: %s, Path: %s, Verb: %s)",
                        ~requestId.ToString(),
                        ~request->GetPath(),
                        ~request->GetVerb());

                    responseHandler->OnError(error);
                    return;
                }

                if (timeout) {
                    activeRequest.TimeoutCookie = TDelayedInvoker::Submit(
                        BIND(&TSession::OnTimeout, MakeStrong(this), requestId),
                        timeout.Get());
                }

                YCHECK(ActiveRequests.insert(MakePair(requestId, activeRequest)).second);
                bus = Bus;
            }

            if (request->IsHeavy()) {
                BIND(&IClientRequest::Serialize, request)
                    .AsyncVia(TRpcDispatcher::Get()->GetPoolInvoker())
                    .Run()
                    .Subscribe(BIND(
                        &TSession::OnRequestSerialized,
                        this,
                        bus,
                        request,
                        timeout));
            } else {
                auto requestMessage = request->Serialize();
                OnRequestSerialized(bus, request, timeout, requestMessage);
            }
        }

        void OnMessage(IMessagePtr message, IBusPtr replyBus)
        {
            VERIFY_THREAD_AFFINITY_ANY();
            UNUSED(replyBus);

            NProto::TResponseHeader header;
            if (!ParseResponseHeader(message, &header)) {
                LOG_ERROR("Error parsing response header");
                return;
            }

            auto requestId = TRequestId::FromProto(header.request_id());

            IClientResponseHandlerPtr responseHandler;
            {
                TGuard<TSpinLock> guard(SpinLock);

                if (Terminated) {
                    LOG_WARNING("Response received via a terminated channel (RequestId: %s)",
                        ~requestId.ToString());
                    return;
                }

                auto it = ActiveRequests.find(requestId);
                if (it == ActiveRequests.end()) {
                    // This may happen when the other party responds to an already timed-out request.
                    LOG_DEBUG("Response for an incorrect or obsolete request received (RequestId: %s)",
                        ~requestId.ToString());
                    return;
                }

                auto& activeRequest = it->second;
                Profiler.TimingCheckpoint(activeRequest.Timer, "reply");
                responseHandler = activeRequest.ResponseHandler;

                UnregisterRequest(it);
            }

            auto error = FromProto(header.error());
            if (error.IsOK()) {
                responseHandler->OnResponse(message);
            } else {
                if (error.GetCode() == EErrorCode::PoisonPill) {
                    LOG_FATAL("Poison pill received\n%s", ~ToString(error));
                }
                responseHandler->OnError(error);
            }
        }

    private:
        IBusPtr Bus;
        TNullable<TDuration> DefaultTimeout;

        struct TActiveRequest
        {
            IClientRequestPtr ClientRequest;
            IClientResponseHandlerPtr ResponseHandler;
            TDelayedInvoker::TCookie TimeoutCookie;
            NProfiling::TTimer Timer;
        };

        typedef yhash_map<TRequestId, TActiveRequest> TRequestMap;

        TSpinLock SpinLock;
        TRequestMap ActiveRequests;
        volatile bool Terminated;
        TError TerminationError;

        void OnRequestSerialized(
            IBusPtr bus,
            IClientRequestPtr request,
            TNullable<TDuration> timeout,
            IMessagePtr requestMessage)
        {
            const auto& requestId = request->GetRequestId();

            bus->Send(requestMessage).Subscribe(BIND(
                &TSession::OnAcknowledgement,
                MakeStrong(this),
                requestId));

            LOG_DEBUG("Request sent (RequestId: %s, Path: %s, Verb: %s, Timeout: %s)",
                ~requestId.ToString(),
                ~request->GetPath(),
                ~request->GetVerb(),
                ~ToString(timeout));
        }

        void OnAcknowledgement(const TRequestId& requestId, TError error)
        {
            VERIFY_THREAD_AFFINITY_ANY();

            TGuard<TSpinLock> guard(SpinLock);

            auto it = ActiveRequests.find(requestId);
            if (it == ActiveRequests.end()) {
                // This one may easily get the actual response before the acknowledgment.
                LOG_DEBUG("Acknowledgment for an incorrect or obsolete request received (RequestId: %s)",
                    ~requestId.ToString());
                return;
            }

            // NB: Make copies, the instance will die soon.
            auto& activeRequest = it->second;
            auto responseHandler = activeRequest.ResponseHandler;

            Profiler.TimingCheckpoint(activeRequest.Timer, "ack");

            if (error.IsOK()) {
                if (activeRequest.ClientRequest->IsOneWay()) {
                    UnregisterRequest(it);
                }

                // Don't need the guard anymore.
                guard.Release();

                responseHandler->OnAcknowledgement();
            } else {
                UnregisterRequest(it);

                // Don't need the guard anymore.
                guard.Release();

                responseHandler->OnError(error);
            }
        }

        void OnTimeout(const TRequestId& requestId)
        {
            VERIFY_THREAD_AFFINITY_ANY();

            IClientResponseHandlerPtr responseHandler;
            {
                TGuard<TSpinLock> guard(SpinLock);

                auto it = ActiveRequests.find(requestId);
                if (it == ActiveRequests.end()) {
                    LOG_DEBUG("Timeout for an incorrect or obsolete request occurred (RequestId: %s)",
                        ~requestId.ToString());
                    return;
                }

                auto& activeRequest = it->second;
                Profiler.TimingCheckpoint(activeRequest.Timer, "timeout");
                responseHandler = activeRequest.ResponseHandler;

                UnregisterRequest(it);
            }

            responseHandler->OnError(TError(EErrorCode::Timeout, "Request timed out"));
        }

        void FinalizeRequest(TActiveRequest& request)
        {
            TDelayedInvoker::CancelAndClear(request.TimeoutCookie);
            Profiler.TimingStop(request.Timer);
        }

        void UnregisterRequest(TRequestMap::iterator it)
        {
            VERIFY_SPINLOCK_AFFINITY(SpinLock);

            FinalizeRequest(it->second);
            ActiveRequests.erase(it);
        }

    };

    IBusClientPtr Client;
    TNullable<TDuration> DefaultTimeout;

    TSpinLock SpinLock;
    volatile bool Terminated;
    TError TerminationError;
    TSessionPtr Session;

    TValueOrError<TSessionPtr> GetOrCreateSession()
    {
        IBusPtr bus;
        TSessionPtr session;
        {
            TGuard<TSpinLock> guard(SpinLock);

            if (Session) {
                return Session;
            }

            if (Terminated) {
                return TError(EErrorCode::TransportError, "Channel terminated")
                    << TerminationError;
            }

            session = New<TSession>(DefaultTimeout);
            auto messageHandler = New<TMessageHandler>(session);

            try {
                bus = Client->CreateBus(messageHandler);
            } catch (const std::exception& ex) {
                return ex;
            }

            session->Init(bus);
            Session = session;
        }

        bus->SubscribeTerminated(BIND(
            &TChannel::OnBusTerminated,
            MakeWeak(this),
            MakeWeak(session)));
        return session;
    }

    void OnBusTerminated(TWeakPtr<TSession> session, TError error)
    {
        auto session_ = session.Lock();
        if (!session_) {
            return;
        }

        {
            TGuard<TSpinLock> guard(SpinLock);
            if (Session == session_) {
                Session.Reset();
            }
        }
        
        session_->Terminate(error);
    }
};          

} // namespace 

IChannelPtr CreateBusChannel(
    IBusClientPtr client,
    TNullable<TDuration> defaultTimeout)
{
    YCHECK(client);

    return New<TChannel>(client, defaultTimeout);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NRpc
} // namespace NYT
