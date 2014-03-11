#include "stdafx.h"
#include "pipe.h"
#include "schemaful_reader.h"
#include "schemaful_writer.h"
#include "row_buffer.h"

#include <core/misc/ring_queue.h>

namespace NYT {
namespace NVersionedTableClient {

////////////////////////////////////////////////////////////////////////////////

static auto PresetResult = MakeFuture(TError());

////////////////////////////////////////////////////////////////////////////////

struct TSchemafulPipe::TData
    : public TIntrinsicRefCounted
{
    TData()
        : WriterOpened(NewPromise())
        , ReaderReadyEvent(NewPromise<TError>())
        , WriterReadyEvent(NewPromise<TError>())
        , RowsWritten(0)
        , RowsRead(0)
        , ReaderOpened(false)
        , WriterClosed(false)
        , Failed(false)
    { }

    TSpinLock SpinLock;
    
    TPromise<void> WriterOpened;

    TTableSchema Schema;

    TRowBuffer RowBuffer;
    TRingQueue<TUnversionedRow> RowQueue;
    
    TPromise<TError> ReaderReadyEvent;
    TPromise<TError> WriterReadyEvent;

    int RowsWritten;
    int RowsRead;
    bool ReaderOpened;
    bool WriterClosed;
    bool Failed;

};

////////////////////////////////////////////////////////////////////////////////

class TSchemafulPipe::TReader
    : public ISchemafulReader
{
public:
    explicit TReader(TDataPtr data)
        : Data_(std::move(data))
        , ReadyEvent_(PresetResult)
    { }

    virtual TAsyncError Open(const TTableSchema& schema) override
    {
        return Data_->WriterOpened.ToFuture().Apply(BIND(
            &TReader::OnOpened,
            MakeStrong(this),
            schema));
    }

    virtual bool Read(std::vector<TUnversionedRow>* rows) override
    {
        YASSERT(rows->capacity() > 0);
        rows->clear();

        {
            TGuard<TSpinLock> guard(Data_->SpinLock);
            
            YCHECK(Data_->ReaderOpened);

            if (Data_->WriterClosed && Data_->RowsWritten == Data_->RowsRead) {
                return false;
            }

            if (!Data_->Failed) {
                auto& rowQueue = Data_->RowQueue;
                while (!rowQueue.empty() && rows->size() < rows->capacity()) {
                    rows->push_back(rowQueue.front());
                    rowQueue.pop();
                    ++Data_->RowsRead;
                }
            }

            if (rows->empty()) {
                ReadyEvent_ = Data_->ReaderReadyEvent.ToFuture();
            }
        }

        return true;
    }

    virtual TAsyncError GetReadyEvent() override
    {
        return ReadyEvent_;
    }

private:
    TDataPtr Data_;
    TAsyncError ReadyEvent_;


    TError OnOpened(const TTableSchema& readerSchema)
    {
        {
            TGuard<TSpinLock> guard(Data_->SpinLock);
            
            YCHECK(!Data_->ReaderOpened);
            Data_->ReaderOpened = true;
        }

        if (readerSchema != Data_->Schema) {
            return TError("Reader/writer schema mismatch");
        }

        return TError();
    }

};

////////////////////////////////////////////////////////////////////////////////

class TSchemafulPipe::TWriter
    : public ISchemafulWriter
{
public:
    explicit TWriter(TDataPtr data)
        : Data_(std::move(data))
    { }

    virtual TAsyncError Open(
        const TTableSchema& schema,
        const TNullable<TKeyColumns>& /*keyColumns*/) override
    {
        Data_->Schema = schema;
        Data_->WriterOpened.Set();
        return PresetResult;
    }

    virtual TAsyncError Close() override
    {
        TPromise<TError> readerReadyEvent;
        TPromise<TError> writerReadyEvent;

        bool doClose = false;

        {
            TGuard<TSpinLock> guard(Data_->SpinLock);

            YCHECK(!Data_->WriterClosed);
            Data_->WriterClosed = true;

            if (!Data_->Failed) {
                doClose = true;
            }

            readerReadyEvent = Data_->ReaderReadyEvent;
            writerReadyEvent = Data_->WriterReadyEvent;
        }

        readerReadyEvent.Set(TError());
        if (doClose) {
            writerReadyEvent.Set(TError());
        }

        return writerReadyEvent;
    }

    virtual bool Write(const std::vector<TUnversionedRow>& rows) override
    {
        // Copy data (no lock).
        auto capturedRows = Data_->RowBuffer.Capture(rows);

        // Enqueue rows (with lock).
        TPromise<TError> readerReadyEvent;

        {
            TGuard<TSpinLock> guard(Data_->SpinLock);

            YCHECK(Data_->WriterOpened.IsSet());
            YCHECK(!Data_->WriterClosed);

            if (Data_->Failed) {
                return false;
            }

            for (auto row : capturedRows) {
                Data_->RowQueue.push(row);
                ++Data_->RowsWritten;
            }

            readerReadyEvent = std::move(Data_->ReaderReadyEvent);
            Data_->ReaderReadyEvent = NewPromise<TError>();
        }

        // Signal readers.
        readerReadyEvent.Set(TError());

        return true;
    }

    virtual TAsyncError GetReadyEvent() override
    {
        // TODO(babenko): implement backpressure from reader
        TGuard<TSpinLock> guard(Data_->SpinLock);
        YCHECK(Data_->Failed);
        return Data_->WriterReadyEvent;
    }

private:
    TDataPtr Data_;

};

////////////////////////////////////////////////////////////////////////////////

class TSchemafulPipe::TImpl
    : public TIntrinsicRefCounted
{
public:
    TImpl()
        : Data_(New<TData>())
        , Reader_(New<TReader>(Data_))
        , Writer_(New<TWriter>(Data_))
    { }

    ISchemafulReaderPtr GetReader() const
    {
        return Reader_;
    }

    ISchemafulWriterPtr  GetWriter() const
    {
        return Writer_;
    }

    void Fail(const TError& error)
    {
        YCHECK(!error.IsOK());

        TPromise<TError> readerReadyEvent;
        TPromise<TError> writerReadyEvent;

        {
            TGuard<TSpinLock> guard(Data_->SpinLock);
            if (Data_->WriterClosed || Data_->Failed)
                return;

            Data_->Failed = true;
            readerReadyEvent = Data_->ReaderReadyEvent;
            writerReadyEvent = Data_->WriterReadyEvent;
        }

        readerReadyEvent.Set(error);
        writerReadyEvent.Set(error);
    }

private:
    TDataPtr Data_;
    TReaderPtr Reader_;
    TWriterPtr Writer_;

};

////////////////////////////////////////////////////////////////////////////////

TSchemafulPipe::TSchemafulPipe()
    : Impl_(New<TImpl>())
{ }

TSchemafulPipe::~TSchemafulPipe()
{ }

ISchemafulReaderPtr TSchemafulPipe::GetReader() const
{
    return Impl_->GetReader();
}

ISchemafulWriterPtr TSchemafulPipe::GetWriter() const
{
    return Impl_->GetWriter();
}

void TSchemafulPipe::Fail(const TError& error)
{
    Impl_->Fail(error);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NVersionedTableClient
} // namespace NYT
