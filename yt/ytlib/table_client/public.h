﻿#pragma once

#include <core/misc/common.h>
#include <core/misc/small_vector.h>

#include <ytlib/table_client/public.h>

#include <ytlib/chunk_client/public.h>

namespace NYT {
namespace NTableClient {

////////////////////////////////////////////////////////////////////////////////

DECLARE_ENUM(EErrorCode,
    ((MasterCommunicationFailed)  (300))
    ((SortOrderViolation)         (301))
);

DECLARE_ENUM(EControlAttribute,
    (TableIndex)
);

////////////////////////////////////////////////////////////////////////////////

const int DefaultPartitionTag = -1;
const i64 MaxRowWeightLimit = (i64) 128 * 1024 * 1024;
const size_t MaxColumnNameSize = 256;
const int MaxColumnCount = 1024;

////////////////////////////////////////////////////////////////////////////////

struct IWriterBase;
typedef TIntrusivePtr<IWriterBase> IWriterBasePtr;

struct IAsyncWriter;
typedef TIntrusivePtr<IAsyncWriter> IAsyncWriterPtr;

struct ISyncWriter;
typedef TIntrusivePtr<ISyncWriter> ISyncWriterPtr;

struct ISyncWriterUnsafe;
typedef TIntrusivePtr<ISyncWriterUnsafe> ISyncWriterUnsafePtr;

struct ISyncReader;
typedef TIntrusivePtr<ISyncReader> ISyncReaderPtr;

struct IAsyncReader;
typedef TIntrusivePtr<IAsyncReader> IAsyncReaderPtr;

class TChunkWriterConfig;
typedef TIntrusivePtr<TChunkWriterConfig> TChunkWriterConfigPtr;

class TTableChunkWriter;
typedef TIntrusivePtr<TTableChunkWriter> TTableChunkWriterPtr;

class TTableChunkWriterFacade;

class TTableChunkWriterProvider;
typedef TIntrusivePtr<TTableChunkWriterProvider> TTableChunkWriterProviderPtr;

class TPartitionChunkWriter;
typedef TIntrusivePtr<TPartitionChunkWriter> TPartitionChunkWriterPtr;

class TPartitionChunkWriterFacade;

class TPartitionChunkWriterProvider;
typedef TIntrusivePtr<TPartitionChunkWriterProvider> TPartitionChunkWriterProviderPtr;

class TTableChunkReader;
typedef TIntrusivePtr<TTableChunkReader> TTableChunkReaderPtr;

class TTableChunkReaderProvider;
typedef TIntrusivePtr<TTableChunkReaderProvider> TTableChunkReaderProviderPtr;

class TPartitionChunkReader;
typedef TIntrusivePtr<TPartitionChunkReader> TPartitionChunkReaderPtr;

class TPartitionChunkReaderProvider;
typedef TIntrusivePtr<TPartitionChunkReaderProvider> TPartitionChunkReaderProviderPtr;

class TChannelWriter;
typedef TIntrusivePtr<TChannelWriter> TChannelWriterPtr;

class TChannelReader;
typedef TIntrusivePtr<TChannelReader> TChannelReaderPtr;

class TChunkWriterConfig;
typedef TIntrusivePtr<TChunkWriterConfig> TChunkWriterConfigPtr;

class TChunkWriterOptions;
typedef TIntrusivePtr<TChunkWriterOptions> TChunkWriterOptionsPtr;

class TTableWriterConfig;
typedef TIntrusivePtr<TTableWriterConfig> TTableWriterConfigPtr;

class TTableWriterOptions;
typedef TIntrusivePtr<TTableWriterOptions> TTableWriterOptionsPtr;

class TChunkReaderOptions;
typedef TIntrusivePtr<TChunkReaderOptions> TChunkReaderOptionsPtr;

class TTableReaderConfig;
typedef TIntrusivePtr<TTableReaderConfig> TTableReaderConfigPtr;

class TAsyncTableReader;
typedef TIntrusivePtr<TAsyncTableReader> TAsyncTableReaderPtr;

class TAsyncWriter;
typedef TIntrusivePtr<TAsyncWriter> TAsyncWriterPtr;

class TTableProducer;
class TTableConsumer;

class TTableConsumerConfig;
typedef TIntrusivePtr<TTableConsumerConfig> TTableConsumerConfigPtr;

typedef SmallVector< std::pair<TStringBuf, TStringBuf>, 32 > TRow;
typedef std::vector<Stroka> TKeyColumns;

struct IPartitioner;

typedef NChunkClient::TMultiChunkSequentialReader<TTableChunkReader> TTableChunkSequenceReader;
typedef TIntrusivePtr<TTableChunkSequenceReader> TTableChunkSequenceReaderPtr;

typedef NChunkClient::TMultiChunkSequentialWriter<TTableChunkWriterProvider> TTableChunkSequenceWriter;
typedef TIntrusivePtr<TTableChunkSequenceWriter> TTableChunkSequenceWriterPtr;

////////////////////////////////////////////////////////////////////////////////

} // namespace NTableClient
} // namespace NYT
