#pragma once

#include <yt/python/common/stream.h>

#include <yt/python/yson/object_builder.h>
#include <yt/python/yson/serialize.h>

#include <yt/client/driver/driver.h>

#include <yt/core/yson/consumer.h>

#include <Extensions.hxx> // pycxx

namespace NYT::NPython {

////////////////////////////////////////////////////////////////////////////////

class TDriverResponseHolder
    : public TIntrinsicRefCounted
{
public:
    TDriverResponseHolder();
    virtual ~TDriverResponseHolder();

    NYson::IYsonConsumer* GetResponseParametersConsumer();
    NYTree::TPythonObjectBuilder* GetPythonObjectBuilder();

    void HoldInputStream(std::unique_ptr<IInputStream> inputStream);
    void HoldOutputStream(std::unique_ptr<IOutputStream>& outputStream);
private:
    std::unique_ptr<IInputStream> InputStream_;
    std::unique_ptr<IOutputStream> OutputStream_;
    std::unique_ptr<NYTree::TPythonObjectBuilder> ResponseParametersBuilder_;
    std::unique_ptr<NYTree::TGilGuardedYsonConsumer> ResponseParametersConsumer_;
};

////////////////////////////////////////////////////////////////////////////////

class TDriverResponse
    : public Py::PythonClass<TDriverResponse>
{
public:
    TDriverResponse(Py::PythonClassInstance *self, Py::Tuple& args, Py::Dict& kwargs);

    void SetResponse(TFuture<void> response);
    TIntrusivePtr<TDriverResponseHolder> GetHolder() const;

    Py::Object ResponseParameters(Py::Tuple& args, Py::Dict& kwargs);
    PYCXX_KEYWORDS_METHOD_DECL(TDriverResponse, ResponseParameters);

    Py::Object Wait(Py::Tuple& args, Py::Dict& kwargs);
    PYCXX_KEYWORDS_METHOD_DECL(TDriverResponse, Wait);

    Py::Object IsSet(Py::Tuple& args, Py::Dict& kwargs);
    PYCXX_KEYWORDS_METHOD_DECL(TDriverResponse, IsSet);

    Py::Object IsOk(Py::Tuple& args, Py::Dict& kwargs);
    PYCXX_KEYWORDS_METHOD_DECL(TDriverResponse, IsOk);

    Py::Object Error(Py::Tuple& args, Py::Dict& kwargs);
    PYCXX_KEYWORDS_METHOD_DECL(TDriverResponse, Error);

    virtual ~TDriverResponse();

    static void InitType();

private:
    TFuture<void> Response_;
    TIntrusivePtr<TDriverResponseHolder> Holder_;

    Py::Object ResponseParameters_;
    bool ResponseParametersFinished_ = false;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NPython
