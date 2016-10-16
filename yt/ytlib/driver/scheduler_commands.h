#pragma once

#include "command.h"

#include <yt/ytlib/job_tracker_client/public.h>

#include <yt/ytlib/scheduler/public.h>

namespace NYT {
namespace NDriver {

//////////////////////////////////////////////////////////////////////////////

class TDumpJobContextCommand
    : public TTypedCommand<NApi::TDumpJobContextOptions>
{
private:
    NJobTrackerClient::TJobId JobId;
    NYPath::TYPath Path;

public:
    TDumpJobContextCommand()
    {
        RegisterParameter("job_id", JobId);
        RegisterParameter("path", Path);
    }

    void Execute(ICommandContextPtr context);

};

class TGetJobStderrCommand
    : public TTypedCommand<NApi::TGetJobStderrOptions>
{
private:
    NJobTrackerClient::TJobId JobId;

public:
    TGetJobStderrCommand()
    {
        RegisterParameter("job_id", JobId);
    }

    void Execute(ICommandContextPtr context);

};

class TStraceJobCommand
    : public TTypedCommand<NApi::TStraceJobOptions>
{
private:
    NJobTrackerClient::TJobId JobId;

public:
    TStraceJobCommand()
    {
        RegisterParameter("job_id", JobId);
    }

    void Execute(ICommandContextPtr context);

};

class TSignalJobCommand
    : public TTypedCommand<NApi::TSignalJobOptions>
{
private:
    NJobTrackerClient::TJobId JobId;
    Stroka SignalName;

public:
    TSignalJobCommand()
    {
        RegisterParameter("job_id", JobId);
        RegisterParameter("signal_name", SignalName);
    }

    void Execute(ICommandContextPtr context);

};

struct TAbandonJobCommand
    : public TTypedCommand<NApi::TAbandonJobOptions>
{
private:
    NJobTrackerClient::TJobId JobId;

public:
    TAbandonJobCommand()
    {
        RegisterParameter("job_id", JobId);
    }

    void Execute(ICommandContextPtr context);

};

struct TPollJobShellCommand
    : public TTypedCommand<NApi::TPollJobShellOptions>
{
private:
    NJobTrackerClient::TJobId JobId;
    NYTree::INodePtr Parameters;

    virtual void OnLoaded() override
    {
        TCommandBase::OnLoaded();

        // Compatibility with initial job shell protocol.
        if (Parameters->GetType() == NYTree::ENodeType::String) {
            Parameters = NYTree::ConvertToNode(NYson::TYsonString(Parameters->AsString()->GetValue()));
        }
    }

public:
    TPollJobShellCommand()
    {
        RegisterParameter("job_id", JobId);
        RegisterParameter("parameters", Parameters);
    }

    void Execute(ICommandContextPtr context);

};

struct TAbortJobCommand
    : public TTypedCommand<NApi::TAbortJobOptions>
{
private:
    NJobTrackerClient::TJobId JobId;

public:
    TAbortJobCommand()
    {
        RegisterParameter("job_id", JobId);
    }

    void Execute(ICommandContextPtr context);

};

//////////////////////////////////////////////////////////////////////////////

class TStartOperationCommandBase
    : public TTypedCommand<NApi::TStartOperationOptions>
{
private:
    NYTree::INodePtr Spec;

    virtual NScheduler::EOperationType GetOperationType() const = 0;

public:
    TStartOperationCommandBase()
    {
        RegisterParameter("spec", Spec);
    }

    void Execute(ICommandContextPtr context);

};

class TMapCommand
    : public TStartOperationCommandBase
{
private:
    virtual NScheduler::EOperationType GetOperationType() const override;

};

class TMergeCommand
    : public TStartOperationCommandBase
{
private:
    virtual NScheduler::EOperationType GetOperationType() const override;

};

class TSortCommand
    : public TStartOperationCommandBase
{
private:
    virtual NScheduler::EOperationType GetOperationType() const override;

};

class TEraseCommand
    : public TStartOperationCommandBase
{
private:
    virtual NScheduler::EOperationType GetOperationType() const override;

};

class TReduceCommand
    : public TStartOperationCommandBase
{
private:
    virtual NScheduler::EOperationType GetOperationType() const override;

};

class TJoinReduceCommand
    : public TStartOperationCommandBase
{
private:
    virtual NScheduler::EOperationType GetOperationType() const override;

};

class TMapReduceCommand
    : public TStartOperationCommandBase
{
private:
    virtual NScheduler::EOperationType GetOperationType() const override;

};

class TRemoteCopyCommand
    : public TStartOperationCommandBase
{
private:
    virtual NScheduler::EOperationType GetOperationType() const override;

};

template <class TOptions>
class TSimpleOperationCommandBase
    : public virtual TTypedCommandBase<TOptions>
{
protected:
    NScheduler::TOperationId OperationId;

public:
    TSimpleOperationCommandBase()
    {
        this->RegisterParameter("operation_id", OperationId);
    }

};

class TAbortOperationCommand
    : public TSimpleOperationCommandBase<NApi::TAbortOperationOptions>
{
public:
    TAbortOperationCommand()
    {
        RegisterParameter("abort_message", Options.AbortMessage)
            .Default();
    }

    void Execute(ICommandContextPtr context);

};

class TSuspendOperationCommand
    : public TSimpleOperationCommandBase<NApi::TSuspendOperationOptions>
{
public:
    TSuspendOperationCommand()
    {
        RegisterParameter("abort_running_jobs", Options.AbortRunningJobs)
            .Default(false);
    }

    void Execute(ICommandContextPtr context);

};

class TResumeOperationCommand
    : public TSimpleOperationCommandBase<NApi::TResumeOperationOptions>
{
public:
    void Execute(ICommandContextPtr context);

};

class TCompleteOperationCommand
    : public TSimpleOperationCommandBase<NApi::TCompleteOperationOptions>
{
public:
    void Execute(ICommandContextPtr context);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NDriver
} // namespace NYT

