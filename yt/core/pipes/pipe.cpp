#include "stdafx.h"

#include "pipe.h"
#include "async_reader.h"
#include "async_writer.h"

#include <core/misc/proc.h>

namespace NYT {
namespace NPipes {

////////////////////////////////////////////////////////////////////////////////

TPipe::TPipe()
{ }

TPipe::TPipe(TPipe&& pipe)
{
    Init(std::move(pipe));
}

TPipe::TPipe(int fd[2])
    : ReadFD_(fd[0])
    , WriteFD_(fd[1])
{ }

void TPipe::Init(TPipe&& other)
{
    ReadFD_ = other.ReadFD_;
    WriteFD_ = other.WriteFD_;
    other.ReadFD_ = InvalidFD;
    other.WriteFD_ = InvalidFD;
}

TPipe::~TPipe()
{
    if (ReadFD_ != InvalidFD) {
        YCHECK(TryClose(ReadFD_, false));
    }

    if (WriteFD_ != InvalidFD) {
        YCHECK(TryClose(WriteFD_, false));
    }
}

void TPipe::operator=(TPipe&& other)
{
    if (this == &other) {
        return;
    }

    Init(std::move(other));
}

TAsyncWriterPtr TPipe::CreateAsyncWriter()
{
    YCHECK(WriteFD_ != InvalidFD);
    SafeMakeNonblocking(WriteFD_);
    return New<TAsyncWriter>(ReleaseWriteFD());
}

TAsyncReaderPtr TPipe::CreateAsyncReader()
{
    YCHECK(ReadFD_ != InvalidFD);
    SafeMakeNonblocking(ReadFD_);
    return New<TAsyncReader>(ReleaseReadFD());
}

int TPipe::ReleaseReadFD()
{
    YCHECK(ReadFD_ != InvalidFD);
    auto fd = ReadFD_;
    ReadFD_ = InvalidFD;
    return fd;
}

int TPipe::ReleaseWriteFD()
{
    YCHECK(WriteFD_ != InvalidFD);
    auto fd = WriteFD_;
    WriteFD_ = InvalidFD;
    return fd;
}

int TPipe::GetReadFD() const
{
    YCHECK(ReadFD_ != InvalidFD);
    return ReadFD_;
}

int TPipe::GetWriteFD() const
{
    YCHECK(WriteFD_ != InvalidFD);
    return WriteFD_;
}

void TPipe::CloseReadFD()
{
    YCHECK(ReadFD_ != InvalidFD);
    auto fd = ReadFD_;
    ReadFD_ = InvalidFD;
    SafeClose(fd, false);
}

void TPipe::CloseWriteFD()
{
    YCHECK(WriteFD_ != InvalidFD);
    auto fd = WriteFD_;
    WriteFD_ = InvalidFD;
    SafeClose(fd, false);
}

////////////////////////////////////////////////////////////////////////////////

Stroka ToString(const TPipe& pipe)
{
    return Format("ReadFD: %v, WriteFD: %v",
        pipe.GetReadFD(),
        pipe.GetWriteFD());
}

////////////////////////////////////////////////////////////////////////////////

TPipeFactory::TPipeFactory(int minFD)
    : MinFD_(minFD)
{ }

TPipeFactory::~TPipeFactory()
{
    for (int fd : ReservedFDs_) {
        YCHECK(TryClose(fd, false));
    }
}

TPipe TPipeFactory::Create()
{
    while (true) {
        int fd[2];
        SafePipe(fd);
        if (fd[0] >= MinFD_ && fd[1] >= MinFD_) {
            TPipe pipe(fd);
            return pipe;
        } else {
            ReservedFDs_.push_back(fd[0]);
            ReservedFDs_.push_back(fd[1]);
        }
    }
}

void TPipeFactory::Clear()
{
    for (int& fd : ReservedFDs_) {
        YCHECK(TryClose(fd, false));
        fd = TPipe::InvalidFD;
    }
    ReservedFDs_.clear();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NPipes
} // namespace NYT
