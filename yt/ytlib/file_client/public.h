﻿#pragma once

#include <core/misc/common.h>

namespace NYT {
namespace NFileClient {

////////////////////////////////////////////////////////////////////////////////

class TFileChunkOutput;

class TFileChunkReader;
typedef TIntrusivePtr<TFileChunkReader> TFileChunkReaderPtr;

class TFileChunkReaderProvider;
typedef TIntrusivePtr<TFileChunkReaderProvider> TFileChunkReaderProviderPtr;

class TFileChunkWriter;
typedef TIntrusivePtr<TFileChunkWriter> TFileChunkWriterPtr;

class TFileChunkWriterProvider;
typedef TIntrusivePtr<TFileChunkWriterProvider> TFileChunkWriterProviderPtr;

class TSyncWriter;
typedef TIntrusivePtr<TSyncWriter> TSyncWriterPtr;

class TAsyncWriter;
typedef TIntrusivePtr<TAsyncWriter> TAsyncWriterPtr;

class TSyncReader;
typedef TIntrusivePtr<TSyncReader> TSyncReaderPtr;

class TAsyncReader;
typedef TIntrusivePtr<TAsyncReader> TAsyncReaderPtr;

class TFileChunkWriterConfig;
typedef TIntrusivePtr<TFileChunkWriterConfig> TFileChunkWriterConfigPtr;

class TFileWriterConfig;
typedef TIntrusivePtr<TFileWriterConfig> TFileWriterConfigPtr;

class TFileReaderConfig;
typedef TIntrusivePtr<TFileReaderConfig> TFileReaderConfigPtr;

////////////////////////////////////////////////////////////////////////////////

} // namespace NFileClient
} // namespace NYT
