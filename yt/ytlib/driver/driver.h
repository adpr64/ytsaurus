#pragma once

#include "public.h"

#include <core/misc/error.h>

#include <core/concurrency/async_stream.h>

#include <ytlib/formats/format.h>

#include <core/ytree/public.h>
#include <core/yson/consumer.h>
#include <core/yson/writer.h>

#include <core/rpc/public.h>

namespace NYT {
namespace NDriver {

////////////////////////////////////////////////////////////////////////////////

//! An instance of driver request.
struct TDriverRequest
{
    TDriverRequest();

    //! Command name to execute.
    Stroka CommandName;

    //! Stream used for reading command input.
    //! The stream must stay alive for the duration of #IDriver::Execute.
    NConcurrency::IAsyncInputStreamPtr InputStream;

    //! Stream where the command output is written.
    //! The stream must stay alive for the duration of #IDriver::Execute.
    NConcurrency::IAsyncOutputStreamPtr OutputStream;

    //! A map containing command parameters.
    NYTree::IMapNodePtr Arguments;

    //! Name of the user issuing the request.
    //! If |Null| then "root" is assumed.
    TNullable<Stroka> AuthenticatedUser;

    //! Provides means to return arbitrary structured data from any command.
    NYson::IYsonConsumer* ResponseParametersConsumer;
};

////////////////////////////////////////////////////////////////////////////////

//! An instance of driver request.
struct TDriverResponse
{
    TDriverResponse()
    { }

    explicit TDriverResponse(TError error)
        : Error(error)
    { }

    //! An error returned by the command, if any.
    TError Error;
};

////////////////////////////////////////////////////////////////////////////////

//! Command meta-descriptor.
/*!
 *  Contains various meta-information describing a given command type.
 */
struct TCommandDescriptor
{
    //! Name of the command.
    Stroka CommandName;

    //! Type of data expected by the command at #TDriverRequest::InputStream.
    NFormats::EDataType InputType;

    //! Type of data written by the command to #TDriverRequest::OutputStream.
    NFormats::EDataType OutputType;

    //! Whether the command changes the state of the cell.
    bool IsVolatile;

    //! Whether the execution of a command is lengthly and/or causes a heavy load.
    bool IsHeavy;

    TCommandDescriptor()
    { }

    TCommandDescriptor(
        const Stroka& commandName,
        NFormats::EDataType inputType,
        NFormats::EDataType outputType,
        bool isVolatile,
        bool isHeavy)
        : CommandName(commandName)
        , InputType(inputType)
        , OutputType(outputType)
        , IsVolatile(isVolatile)
        , IsHeavy(isHeavy)
    { }
};

////////////////////////////////////////////////////////////////////////////////

//! An instance of command execution engine.
/*!
 *  Each driver instance maintains a collection of cached connections to
 *  various YT subsystems (e.g. masters, scheduler).
 *
 *  Requests are executed synchronously.
 *
 *  IDriver instances are thread-safe and reentrant.
 */
struct IDriver
    : public virtual TRefCounted
{
    //! Asynchronously executes a given request.
    virtual TFuture<TDriverResponse> Execute(const TDriverRequest& request) = 0;

    //! Returns a descriptor for the command with a given name or
    //! |Null| if no command with this name is registered.
    virtual TNullable<TCommandDescriptor> FindCommandDescriptor(const Stroka& commandName) = 0;

    //! Returns a descriptor for then command with a given name.
    //! Fails if no command with this name is registered.
    TCommandDescriptor GetCommandDescriptor(const Stroka& commandName);

    //! Returns the list of descriptors for all supported commands.
    virtual std::vector<TCommandDescriptor> GetCommandDescriptors() = 0;

    //! Returns a cached master channel.
    virtual NRpc::IChannelPtr GetMasterChannel() = 0;

    //! Returns a cached scheduler channel.
    virtual NRpc::IChannelPtr GetSchedulerChannel() = 0;
};

////////////////////////////////////////////////////////////////////////////////

//! Creates an implementation of IDriver with a given configuration.
IDriverPtr CreateDriver(TDriverConfigPtr config);

////////////////////////////////////////////////////////////////////////////////

} // namespace NDriver
} // namespace NYT

