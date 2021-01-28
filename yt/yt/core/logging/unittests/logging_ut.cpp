
#include "yt/core/misc/string_builder.h"
#include <yt/core/test_framework/framework.h>

#include <yt/core/logging/appendable_zstd.h>
#include <yt/core/logging/log.h>
#include <yt/core/logging/log_manager.h>
#include <yt/core/logging/writer.h>
#include <yt/core/logging/random_access_gzip.h>

#include <yt/core/json/json_parser.h>

#include <yt/core/tracing/trace_context.h>

#include <yt/core/ytree/fluent.h>

#include <yt/core/ytree/convert.h>

#include <yt/core/misc/range_formatters.h>

#include <library/cpp/streams/zstd/zstd.h>

#include <util/system/fs.h>

#include <util/stream/zlib.h>

#ifdef _unix_
#include <unistd.h>
#endif

namespace NYT::NLogging {
namespace {

using namespace NYTree;
using namespace NYson;
using namespace NJson;

////////////////////////////////////////////////////////////////////////////////

static const TLogger Logger("Test");

class TLoggingTest
    : public ::testing::Test
{
public:
    TLoggingTest()
        : SomeDate("2014-04-24 23:41:09,804")
        , DateLength(SomeDate.length())
    {
        Category.Name = "category";
    }

protected:
    TLoggingCategory Category;
    TString SomeDate;
    int DateLength;

    IMapNodePtr DeserializeJson(const TString& source)
    {
        auto builder = CreateBuilderFromFactory(GetEphemeralNodeFactory());
        builder->BeginTree();
        TStringStream stream(source);
        ParseJson(&stream, builder.get());
        return builder->EndTree()->AsMap();
    }

    void WritePlainTextEvent(ILogWriter* writer)
    {
        TLogEvent event;
        event.MessageFormat = ELogMessageFormat::PlainText;
        event.Category = &Category;
        event.Level = ELogLevel::Debug;
        event.Message = TSharedRef::FromString("message");
        event.ThreadId = 0xba;

        WriteEvent(writer, event);
    }

    void WriteEvent(ILogWriter* writer, const TLogEvent& event)
    {
        writer->Write(event);
        writer->Flush();
    }

    std::vector<TString> ReadFile(
        const TString& fileName,
        bool compressed = false,
        ECompressionMethod compressionMethod = ECompressionMethod::Gzip)
    {
        auto splitLines = [&] (IInputStream *input) {
            TString line;
            std::vector<TString> lines;
            while (input->ReadLine(line)) {
                lines.push_back(line + "\n");
            }
            return lines;
        };

        TUnbufferedFileInput rawInput(fileName);
        if (!compressed) {
            return splitLines(&rawInput);
        } else if (compressionMethod == ECompressionMethod::Gzip) {
            TZLibDecompress input(&rawInput);
            return splitLines(&input);
        } else if (compressionMethod == ECompressionMethod::Zstd) {
            TZstdDecompress input(&rawInput);
            return splitLines(&input);
        } else {
            EXPECT_TRUE(false);
            return {};
        }
    }

    void Configure(const TString& configYson)
    {
        auto configNode = ConvertToNode(TYsonString(configYson));
        auto config = ConvertTo<TLogManagerConfigPtr>(configNode);
        TLogManager::Get()->Configure(config);
    }

    void DoTestCompression(ECompressionMethod method, int compressionLevel)
    {
        NFs::Remove("test.log.gz");

        auto writer = New<TFileLogWriter>(
            std::make_unique<TPlainTextLogFormatter>(),
            "test_writer",
            "test.log.gz",
            /* enableCompression */ true,
            method,
            compressionLevel);
        WritePlainTextEvent(writer.Get());

        writer->Reload();
        WritePlainTextEvent(writer.Get());

        {
            auto lines = ReadFile("test.log.gz", true, method);
            EXPECT_EQ(5, lines.size());
            EXPECT_TRUE(lines[0].find("Logging started") != -1);
            EXPECT_EQ("\tD\tcategory\tmessage\tba\t\t\n", lines[1].substr(DateLength, lines[1].size()));
        }

        NFs::Remove("test.log.gz");
    }
};

#ifdef _unix_

TEST_F(TLoggingTest, ReloadOnSighup)
{
    Cerr << "Removing files" << Endl;

    NFs::Remove("reload-on-sighup.log");
    NFs::Remove("reload-on-sighup.log.1");

    Cerr << "Configuring logging" << Endl;

    Configure(R"({
        rules = [
            {
                "min_level" = "info";
                "writers" = [ "info" ];
            };
        ];
        "writers" = {
            "info" = {
                "file_name" = "reload-on-sighup.log";
                "type" = "file";
            };
        };
    })");

    Cerr << "Waiting for message 1" << Endl;

    WaitForPredicate([&] {
        YT_LOG_INFO("Message1");
        return NFs::Exists("reload-on-sighup.log");
    });

    Cerr << "Renaming logfile" << Endl;

    NFs::Rename("reload-on-sighup.log", "reload-on-sighup.log.1");

    Cerr << "Sending SIGHUP" << Endl;

    ::kill(::getpid(), SIGHUP);

    Cerr << "Waiting for message 2" << Endl;

    WaitForPredicate([&] {
        YT_LOG_INFO("Message2");
        return NFs::Exists("reload-on-sighup.log");
    });

    Cerr << "Success" << Endl;
}

#endif

TEST_F(TLoggingTest, FileWriter)
{
    NFs::Remove("test.log");

    auto writer = New<TFileLogWriter>(std::make_unique<TPlainTextLogFormatter>(), "test_writer", "test.log", false);
    WritePlainTextEvent(writer.Get());

    {
        auto lines = ReadFile("test.log");
        EXPECT_EQ(2, lines.size());
        EXPECT_TRUE(lines[0].find("Logging started") != -1);
        EXPECT_EQ("\tD\tcategory\tmessage\tba\t\t\n", lines[1].substr(DateLength, lines[1].size()));
    }

    writer->Reload();
    WritePlainTextEvent(writer.Get());

    {
        auto lines = ReadFile("test.log");
        EXPECT_EQ(5, lines.size());
        EXPECT_TRUE(lines[0].find("Logging started") != -1);
        EXPECT_EQ("\tD\tcategory\tmessage\tba\t\t\n", lines[1].substr(DateLength));
        EXPECT_EQ("\n", lines[2]);
        EXPECT_TRUE(lines[3].find("Logging started") != -1);
        EXPECT_EQ("\tD\tcategory\tmessage\tba\t\t\n", lines[4].substr(DateLength));
    }

    NFs::Remove("test.log");
}

TEST_F(TLoggingTest, Compression)
{
    // No compression.
    DoTestCompression(ECompressionMethod::Gzip, /* compressionLevel */ 0);

    // Default compression.
    DoTestCompression(ECompressionMethod::Gzip, /* compressionLevel */ 6);

    // Maximum compression.
    DoTestCompression(ECompressionMethod::Gzip, /* compressionLevel */ 9);
}

TEST_F(TLoggingTest, CompressionZstd)
{
    // Default compression.
    DoTestCompression(ECompressionMethod::Zstd, /* compressionLevel */ 0);

    // Fast compression (--fast=<...>).
    DoTestCompression(ECompressionMethod::Zstd, /* compressionLevel */ -2);

    // Fast compression.
    DoTestCompression(ECompressionMethod::Zstd, /* compressionLevel */ 1);

    // Maximum compression.
    DoTestCompression(ECompressionMethod::Zstd, /* compressionLevel */ 22);
}

TEST_F(TLoggingTest, StreamWriter)
{
    TStringStream stringOutput;
    auto writer = New<TStreamLogWriter>(&stringOutput, std::make_unique<TPlainTextLogFormatter>(), "test_writer");

    WritePlainTextEvent(writer.Get());

    EXPECT_EQ(
       "\tD\tcategory\tmessage\tba\t\t\n",
       stringOutput.Str().substr(DateLength));
}

TEST_F(TLoggingTest, Rule)
{
    auto rule = New<TRuleConfig>();
    rule->Load(ConvertToNode(TYsonString(TStringBuf(
        R"({
            exclude_categories = [ bus ];
            min_level = info;
            writers = [ some_writer ];
        })"))));

    EXPECT_TRUE(rule->IsApplicable("some_service", ELogMessageFormat::PlainText));
    EXPECT_FALSE(rule->IsApplicable("bus", ELogMessageFormat::PlainText));
    EXPECT_FALSE(rule->IsApplicable("bus", ELogLevel::Debug, ELogMessageFormat::PlainText));
    EXPECT_FALSE(rule->IsApplicable("some_service", ELogLevel::Debug, ELogMessageFormat::PlainText));
    EXPECT_TRUE(rule->IsApplicable("some_service", ELogLevel::Warning, ELogMessageFormat::PlainText));
    EXPECT_TRUE(rule->IsApplicable("some_service", ELogLevel::Info, ELogMessageFormat::PlainText));
}

TEST_F(TLoggingTest, LogManager)
{
    NFs::Remove("test.log");
    NFs::Remove("test.error.log");

    Configure(R"({
        rules = [
            {
                "min_level" = "info";
                "writers" = [ "info" ];
            };
            {
                "min_level" = "error";
                "writers" = [ "error" ];
            };
        ];
        "writers" = {
            "error" = {
                "file_name" = "test.error.log";
                "type" = "file";
            };
            "info" = {
                "file_name" = "test.log";
                "type" = "file";
            };
        };
    })");

    YT_LOG_DEBUG("Debug message");
    YT_LOG_INFO("Info message");
    YT_LOG_ERROR("Error message");

    TLogManager::Get()->Synchronize();

    auto infoLog = ReadFile("test.log");
    auto errorLog = ReadFile("test.error.log");

    EXPECT_EQ(3, infoLog.size());
    EXPECT_EQ(2, errorLog.size());

    NFs::Remove("test.log");
    NFs::Remove("test.error.log");
}

TEST_F(TLoggingTest, StructuredJsonLogging)
{
    NFs::Remove("test.log");

    TLogEvent event;
    event.MessageFormat = ELogMessageFormat::Structured;
    event.Category = &Category;
    event.Level = ELogLevel::Debug;
    event.StructuredMessage = NYTree::BuildYsonStringFluently<EYsonType::MapFragment>()
        .Item("message")
        .Value("test_message")
        .Finish();

    auto writer = New<TFileLogWriter>(std::make_unique<TJsonLogFormatter>(THashMap<TString, INodePtr>{}), "test_writer", "test.log");
    WriteEvent(writer.Get(), event);
    TLogManager::Get()->Synchronize();

    auto log = ReadFile("test.log");

    auto logStartedJson = DeserializeJson(log[0]);
    EXPECT_EQ(logStartedJson->GetChildOrThrow("message")->AsString()->GetValue(), "Logging started");
    EXPECT_EQ(logStartedJson->GetChildOrThrow("level")->AsString()->GetValue(), "info");
    EXPECT_EQ(logStartedJson->GetChildOrThrow("category")->AsString()->GetValue(), "Logging");

    auto contentJson = DeserializeJson(log[1]);
    EXPECT_EQ(contentJson->GetChildOrThrow("message")->AsString()->GetValue(), "test_message");
    EXPECT_EQ(contentJson->GetChildOrThrow("level")->AsString()->GetValue(), "debug");
    EXPECT_EQ(contentJson->GetChildOrThrow("category")->AsString()->GetValue(), "category");

    NFs::Remove("test.log");
}

////////////////////////////////////////////////////////////////////////////////

class TAppendableZstdFileTest
    : public ::testing::Test
{
protected:
    void WriteTestFile(const TString &filename, i64 addBytes, bool writeTruncateMessage)
    {
        NFs::Remove(filename);

        {
            TFile rawFile(filename, OpenAlways|RdWr|CloseOnExec);
            TAppendableZstdFile file(&rawFile, DefaultZstdCompressionLevel, writeTruncateMessage);
            file << "foo\n";
            file.Flush();
            file << "bar\n";
            file.Finish();

            rawFile.Resize(rawFile.GetLength() + addBytes);
        }
        {
            TFile rawFile(filename, OpenAlways|RdWr|CloseOnExec);
            TAppendableZstdFile file(&rawFile, DefaultZstdCompressionLevel, writeTruncateMessage);
            file << "zog\n";
            file.Flush();
        }
    }
};

TEST_F(TAppendableZstdFileTest, Write)
{
    WriteTestFile("test.txt.zst", 0, false);

    TUnbufferedFileInput file("test.txt.zst");
    TZstdDecompress decompress(&file);
    EXPECT_EQ("foo\nbar\nzog\n", decompress.ReadAll());

    NFs::Remove("test.txt.zst");
}

TEST_F(TAppendableZstdFileTest, RepairSmall)
{
    WriteTestFile("test.txt.zst", -1, false);

    TUnbufferedFileInput file("test.txt.zst");
    TZstdDecompress decompress(&file);
    EXPECT_EQ("foo\nzog\n", decompress.ReadAll());

    NFs::Remove("test.txt.zst");
}

TEST_F(TAppendableZstdFileTest, RepairLarge)
{
    WriteTestFile("test.txt.zst", 10_MB, true);

    TUnbufferedFileInput file("test.txt.zst");
    TZstdDecompress decompress(&file);

    TStringBuilder expected;
    expected.AppendFormat("foo\nbar\nTruncated %v bytes due to zstd repair.\nzog\n", 10_MB);
    EXPECT_EQ(expected.Flush(), decompress.ReadAll());

    NFs::Remove("test.txt.zst");
}

TEST(TRandomAccessGZipTest, Write)
{
    NFs::Remove("test.txt.gz");

    {
        TFile rawFile("test.txt.gz", OpenAlways|RdWr|CloseOnExec);
        TRandomAccessGZipFile file(&rawFile);
        file << "foo\n";
        file.Flush();
        file << "bar\n";
        file.Finish();
    }
    {
        TFile rawFile("test.txt.gz", OpenAlways|RdWr|CloseOnExec);
        TRandomAccessGZipFile file(&rawFile);
        file << "zog\n";
        file.Finish();
    }

    auto input = TUnbufferedFileInput("test.txt.gz");
    TZLibDecompress decompress(&input);
    EXPECT_EQ("foo\nbar\nzog\n", decompress.ReadAll());

    NFs::Remove("test.txt.gz");
}

TEST(TRandomAccessGZipTest, RepairIncompleteBlocks)
{
    NFs::Remove("test.txt.gz");
    {
        TFile rawFile("test.txt.gz", OpenAlways|RdWr|CloseOnExec);
        TRandomAccessGZipFile file(&rawFile);
        file << "foo\n";
        file.Flush();
        file << "bar\n";
        file.Finish();
    }

    i64 fullSize;
    {
        TFile file("test.txt.gz", OpenAlways|RdWr);
        fullSize = file.GetLength();
        file.Resize(fullSize - 1);
    }

    {
        TFile rawFile("test.txt.gz", OpenAlways | RdWr | CloseOnExec);
        TRandomAccessGZipFile file(&rawFile);
    }

    {
        TFile file("test.txt.gz", OpenAlways|RdWr);
        EXPECT_LE(file.GetLength(), fullSize - 1);
    }

    NFs::Remove("test.txt.gz");
}

// This test is for manual check of YT_LOG_FATAL
TEST_F(TLoggingTest, DISABLED_LogFatal)
{
    NFs::Remove("test.log");
    NFs::Remove("test.error.log");

    Configure(R"({
        rules = [
            {
                "min_level" = "info";
                "writers" = [ "info" ];
            };
        ];
        "writers" = {
            "info" = {
                "file_name" = "test.log";
                "type" = "file";
            };
        };
    })");

    YT_LOG_INFO("Info message");

    Sleep(TDuration::MilliSeconds(100));

    YT_LOG_INFO("Info message");
    YT_LOG_FATAL("FATAL");

    NFs::Remove("test.log");
    NFs::Remove("test.error.log");
}

TEST_F(TLoggingTest, RequestSuppression)
{
    NFs::Remove("test.log");

    Configure(R"({
        rules = [
            {
                "min_level" = "info";
                "writers" = [ "info" ];
            };
        ];
        "writers" = {
            "info" = {
                "file_name" = "test.log";
                "type" = "file";
            };
        };
        "request_suppression_timeout" = 100;
    })");

    {
        auto requestId = NTracing::TRequestId::Create();
        auto traceContext = NTracing::CreateRootTraceContext("Test", requestId);
        NTracing::TTraceContextGuard guard(traceContext);

        YT_LOG_INFO("Traced message");

        TLogManager::Get()->SuppressRequest(requestId);
    }

    YT_LOG_INFO("Info message");

    TLogManager::Get()->Synchronize();

    auto lines = ReadFile("test.log");

    EXPECT_EQ(2, lines.size());
    EXPECT_TRUE(lines[0].find("Logging started") != -1);
    EXPECT_TRUE(lines[1].find("Info message") != -1);

    NFs::Remove("test.log");
}

////////////////////////////////////////////////////////////////////////////////

class TLoggingTagsTest
    : public ::testing::TestWithParam<std::tuple<bool, bool, bool, TString>>
{ };

TEST_P(TLoggingTagsTest, All)
{
    auto hasMessageTag = std::get<0>(GetParam());
    auto hasLoggerTag = std::get<1>(GetParam());
    auto hasTraceContext = std::get<2>(GetParam());
    auto expected = std::get<3>(GetParam());

    auto traceContext = hasTraceContext
        ? NTracing::CreateRootTraceContext("Test", /* requestId */ {}, "TraceContextTag")
        : NTracing::TTraceContextPtr();

    auto logger = TLogger("Test");
    if (hasLoggerTag) {
        logger = logger.WithTag("LoggerTag");
    }

    if (hasMessageTag) {
        EXPECT_EQ(
            expected,
            ToString(NLogging::NDetail::BuildLogMessage(
                traceContext.Get(),
                logger,
                "Log message (Value: %v)",
                123)));
    } else {
        EXPECT_EQ(
            expected,
            ToString(NLogging::NDetail::BuildLogMessage(
                traceContext.Get(),
                logger,
                "Log message")));
    }
}

INSTANTIATE_TEST_SUITE_P(ValueParametrized, TLoggingTagsTest,
    ::testing::Values(
        std::make_tuple(false, false, false, "Log message"),
        std::make_tuple(false, false,  true, "Log message (TraceContextTag)"),
        std::make_tuple(false,  true, false, "Log message (LoggerTag)"),
        std::make_tuple(false,  true,  true, "Log message (LoggerTag, TraceContextTag)"),
        std::make_tuple( true, false, false, "Log message (Value: 123)"),
        std::make_tuple( true, false,  true, "Log message (Value: 123, TraceContextTag)"),
        std::make_tuple( true,  true, false, "Log message (Value: 123, LoggerTag)"),
        std::make_tuple( true,  true,  true, "Log message (Value: 123, LoggerTag, TraceContextTag)")));

////////////////////////////////////////////////////////////////////////////////

class TLongMessagesTest
    : public TLoggingTest
{
protected:
    static constexpr int N = 500;
    std::vector<TString> Chunks_;

    TLongMessagesTest()
    {
        for (int i = 0; i < N; ++i) {
            Chunks_.push_back(Format("PayloadPayloadPayloadPayloadPayload%v", i));
        }
    }

    void ConfigureForLongMessages()
    {
        NFs::Remove("test.log");

        Configure(R"({
            rules = [
                {
                    "min_level" = "info";
                    "max_level" = "info";
                    "writers" = [ "info" ];
                };
            ];
            "writers" = {
                "info" = {
                    "file_name" = "test.log";
                    "type" = "file";
                };
            };
        })");
    }

    void LogLongMessages()
    {
        for (int i = 0; i < N; ++i) {
            YT_LOG_INFO("%v", MakeRange(Chunks_.data(), Chunks_.data() + i));
        }
    }

    void CheckLongMessages()
    {
        TLogManager::Get()->Synchronize();

        auto infoLog = ReadFile("test.log");
        EXPECT_EQ(N + 1, infoLog.size());
        for (int i = 0; i < N; ++i) {
            auto expected = Format("%v", MakeRange(Chunks_.data(), Chunks_.data() + i));
            auto actual = infoLog[i + 1];
            EXPECT_NE(TString::npos, actual.find(expected));
        }

        NFs::Remove("test.log");
    }
};

TEST_F(TLongMessagesTest, WithPerThreadCache)
{
    ConfigureForLongMessages();
    LogLongMessages();
    CheckLongMessages();
}

TEST_F(TLongMessagesTest, WithoutPerThreadCache)
{
    ConfigureForLongMessages();
    using TThis = typename std::remove_reference<decltype(*this)>::type;
    TThread thread([] (void* opaque) -> void* {
        auto this_ = static_cast<TThis*>(opaque);
        NLogging::NDetail::TMessageStringBuilder::DisablePerThreadCache();
        this_->LogLongMessages();
        return nullptr;
    }, this);
    thread.Start();
    thread.Join();
    CheckLongMessages();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NYT::NLogging
