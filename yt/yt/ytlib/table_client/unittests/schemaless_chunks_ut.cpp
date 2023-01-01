#include <yt/yt/core/test_framework/framework.h>

#include <yt/yt/ytlib/chunk_client/chunk_reader.h>
#include <yt/yt/ytlib/chunk_client/chunk_reader_options.h>
#include <yt/yt/ytlib/chunk_client/chunk_reader_statistics.h>
#include <yt/yt/ytlib/chunk_client/client_block_cache.h>
#include <yt/yt/ytlib/chunk_client/data_slice_descriptor.h>
#include <yt/yt/ytlib/chunk_client/memory_reader.h>
#include <yt/yt/ytlib/chunk_client/memory_writer.h>

#include <yt/yt/ytlib/table_client/cached_versioned_chunk_meta.h>
#include <yt/yt/ytlib/table_client/chunk_state.h>
#include <yt/yt/ytlib/table_client/config.h>
#include <yt/yt/ytlib/table_client/schemaless_multi_chunk_reader.h>
#include <yt/yt/ytlib/table_client/schemaless_chunk_writer.h>

#include <yt/yt/client/table_client/column_sort_schema.h>
#include <yt/yt/client/table_client/unversioned_row.h>
#include <yt/yt/client/table_client/helpers.h>
#include <yt/yt/client/table_client/name_table.h>

#include <yt/yt/client/table_client/unittests/helpers/helpers.h>

#include <yt/yt/core/compression/public.h>

#include <yt/yt/core/ytree/convert.h>

#include <yt/yt/core/yson/string.h>

namespace NYT::NTableClient {
namespace {

using namespace NChunkClient;
using namespace NConcurrency;
using namespace NYTree;
using namespace NYson;

using NChunkClient::TChunkReaderStatistics;
using NChunkClient::NProto::TChunkSpec;

////////////////////////////////////////////////////////////////////////////////

const int RowCount = 50000;
const TStringBuf StringValue = "She sells sea shells on a sea shore";
const TStringBuf AnyValueList = "[one; two; three]";
const TStringBuf AnyValueMap = "{a=b; c=d}";
const std::vector<TString> ColumnNames = {"c0", "c1", "c2", "c3", "c4", "c5", "c6"};

////////////////////////////////////////////////////////////////////////////////

class TSchemalessChunksTest
    : public ::testing::TestWithParam<std::tuple<EOptimizeFor, TTableSchema, TColumnFilter, TLegacyReadRange>>
{
protected:
    IChunkReaderPtr MemoryReader_;
    TNameTablePtr WriteNameTable_;
    TChunkSpec ChunkSpec_;
    TColumnarChunkMetaPtr ChunkMeta_;
    TChunkedMemoryPool Pool_;
    std::vector<TUnversionedRow> Rows_;

    static TUnversionedValue CreateC0(int rowIndex, TNameTablePtr nameTable)
    {
        // Key part 0, Any.
        int id = nameTable->GetId("c0");
        switch (rowIndex / 100000) {
            case 0:
                return MakeUnversionedSentinelValue(EValueType::Null, id);
            case 1:
                return MakeUnversionedInt64Value(-65537, id);
            case 2:
                return MakeUnversionedUint64Value(65537, id);
            case 3:
                return MakeUnversionedDoubleValue(rowIndex, id);
            case 4:
                return MakeUnversionedBooleanValue(true, id);
            case 5:
                return MakeUnversionedStringValue(StringValue, id);
            default:
                YT_ABORT();
        }
    }

    static TUnversionedValue CreateC1(int rowIndex, TNameTablePtr nameTable)
    {
        //  Key part 1, Int64.
        int id = nameTable->GetId("c1");
        const int divider = 10000;
        auto value = rowIndex % (10 * divider);
        if (value < divider) {
            return MakeUnversionedSentinelValue(EValueType::Null, id);
        } else {
            return MakeUnversionedInt64Value(value / divider - 5, id);
        }
    }

    static TUnversionedValue CreateC2(int rowIndex, TNameTablePtr nameTable)
    {
        //  Key part 2, Uint64.
        int id = nameTable->GetId("c2");
        const int divider = 1000;
        auto value = rowIndex % (10 * divider);
        if (value < divider) {
            return MakeUnversionedSentinelValue(EValueType::Null, id);
        } else {
            return MakeUnversionedUint64Value(value / divider, id);
        }
    }

    static TUnversionedValue CreateC3(int rowIndex, TNameTablePtr nameTable)
    {
        // Key part 3, String.
        int id = nameTable->GetId("c3");
        const int divider = 100;
        auto value = rowIndex % (10 * divider);
        if (value < divider) {
            return MakeUnversionedSentinelValue(EValueType::Null, id);
        } else {
            return MakeUnversionedStringValue(StringValue, id);
        }
    }

    static TUnversionedValue CreateC4(int rowIndex, TNameTablePtr nameTable)
    {
        // Key part 4, Boolean.
        int id = nameTable->GetId("c4");
        const int divider = 10;
        auto value = rowIndex % (10 * divider);
        if (value < divider) {
            return MakeUnversionedSentinelValue(EValueType::Null, id);
        } else {
            return MakeUnversionedBooleanValue(value > divider * 5, id);
        }
    }

    static TUnversionedValue CreateC5(int rowIndex, TNameTablePtr nameTable)
    {
        // Key part 5, Double.
        int id = nameTable->GetId("c5");
        const int divider = 1;
        auto value = rowIndex % (10 * divider);
        if (value < divider) {
            return MakeUnversionedSentinelValue(EValueType::Null, id);
        } else {
            return MakeUnversionedDoubleValue(value, id);
        }
    }

    static TUnversionedValue CreateC6(int rowIndex, TNameTablePtr nameTable)
    {
        // Not key, Any.
        int id = nameTable->GetId("c6");
        switch (rowIndex % 8) {
            case 0:
                return MakeUnversionedSentinelValue(EValueType::Null, id);
            case 1:
                return MakeUnversionedInt64Value(-65537, id);
            case 2:
                return MakeUnversionedUint64Value(65537, id);
            case 3:
                return MakeUnversionedDoubleValue(rowIndex, id);
            case 4:
                return MakeUnversionedBooleanValue(true, id);
            case 5:
                return MakeUnversionedStringValue(StringValue, id);
            case 6:
                return MakeUnversionedAnyValue(AnyValueList, id);
            case 7:
                return MakeUnversionedAnyValue(AnyValueMap, id);
            default:
                YT_ABORT();
        }
    }

    std::vector<TUnversionedRow> CreateRows(TNameTablePtr nameTable)
    {
        std::vector<TUnversionedRow> rows;
        for (int rowIndex = 0; rowIndex < RowCount; ++rowIndex) {
            auto row = TMutableUnversionedRow::Allocate(&Pool_, 7);
            row[0] = CreateC0(rowIndex, nameTable);
            row[1] = CreateC1(rowIndex, nameTable);
            row[2] = CreateC2(rowIndex, nameTable);
            row[3] = CreateC3(rowIndex, nameTable);
            row[4] = CreateC4(rowIndex, nameTable);
            row[5] = CreateC5(rowIndex, nameTable);
            row[6] = CreateC6(rowIndex, nameTable);
            rows.push_back(row);
        }
        return rows;
    }

    void SetUp() override
    {
        auto nameTable = New<TNameTable>();
        InitNameTable(nameTable);
        Rows_ = CreateRows(nameTable);

        auto memoryWriter = New<TMemoryWriter>();

        auto config = New<TChunkWriterConfig>();
        config->BlockSize = 256;
        config->Postprocess();

        auto options = New<TChunkWriterOptions>();
        options->OptimizeFor = std::get<0>(GetParam());
        options->Postprocess();

        auto chunkWriter = CreateSchemalessChunkWriter(
            config,
            options,
            New<TTableSchema>(std::get<1>(GetParam())),
            /*nameTable*/ nullptr,
            memoryWriter,
            /*dataSink*/ std::nullopt);

        WriteNameTable_ = chunkWriter->GetNameTable();
        InitNameTable(WriteNameTable_);

        chunkWriter->Write(Rows_);
        EXPECT_TRUE(chunkWriter->Close().Get().IsOK());

        MemoryReader_ = CreateMemoryReader(
            memoryWriter->GetChunkMeta(),
            memoryWriter->GetBlocks());

        ToProto(ChunkSpec_.mutable_chunk_id(), NullChunkId);
        ChunkSpec_.set_table_row_index(42);

        ChunkMeta_ = New<TColumnarChunkMeta>(*memoryWriter->GetChunkMeta());
    }

    static void InitNameTable(TNameTablePtr nameTable, int idShift = 0)
    {
        for (int id = 0; id < std::ssize(ColumnNames); ++id) {
            EXPECT_EQ(id, nameTable->GetIdOrRegisterName(ColumnNames[(id + idShift) % ColumnNames.size()]));
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

TEST_P(TSchemalessChunksTest, WithoutSampling)
{
    auto schema = std::get<1>(GetParam());

    int keyColumnCount = schema.GetKeyColumnCount();

    auto readNameTable = New<TNameTable>();
    InitNameTable(readNameTable, 4);

    auto columnFilter = std::get<2>(GetParam());
    auto expected = CreateFilteredRangedRows(
        Rows_,
        WriteNameTable_,
        readNameTable,
        columnFilter,
        std::get<3>(GetParam()),
        &Pool_,
        keyColumnCount);

    auto chunkState = New<TChunkState>(
        GetNullBlockCache(),
        ChunkSpec_);
    chunkState->TableSchema = New<TTableSchema>(std::get<1>(GetParam()));

    auto legacyReadRange = std::get<3>(GetParam());

    auto lowerReadLimit = ReadLimitFromLegacyReadLimit(legacyReadRange.LowerLimit(), /* isUpper */ false, keyColumnCount);
    auto upperReadLimit = ReadLimitFromLegacyReadLimit(legacyReadRange.UpperLimit(), /* isUpper */ true, keyColumnCount);

    auto chunkReader = CreateSchemalessRangeChunkReader(
        std::move(chunkState),
        ChunkMeta_,
        TChunkReaderConfig::GetDefault(),
        TChunkReaderOptions::GetDefault(),
        MemoryReader_,
        readNameTable,
        /* chunkReadOptions */ {},
        /* sortColumns */ schema.GetSortColumns(),
        /* omittedInaccessibleColumns */ {},
        columnFilter,
        TReadRange(lowerReadLimit, upperReadLimit));

    CheckSchemalessResult(expected, chunkReader, 0);
}

INSTANTIATE_TEST_SUITE_P(Unsorted,
    TSchemalessChunksTest,
    ::testing::Combine(
        ::testing::Values(
            EOptimizeFor::Scan,
            EOptimizeFor::Lookup),
        ::testing::Values(
            ConvertTo<TTableSchema>(TYsonString(TStringBuf("<strict=%false>[]"))),
            ConvertTo<TTableSchema>(TYsonString(TStringBuf("<strict=%false>[{name = c0; type = any}; {name = c1; type = int64}; {name = c2; type = uint64}; ]"))),
            ConvertTo<TTableSchema>(TYsonString(TStringBuf("<strict=%true>[{name = c0; type = any}; {name = c1; type = int64}; "
                "{name = c2; type = uint64}; {name = c3; type = string}; {name = c4; type = boolean}; {name = c5; type = double}; "
                "{name = c6; type = any};]"))),
            ConvertTo<TTableSchema>(TYsonString(TStringBuf("<strict=%false>[{name = c0; type = any}; {name = c1; type = int64}; "
                "{name = c2; type = uint64}; {name = c3; type = string}; {name = c4; type = boolean}; {name = c5; type = double}; {name = c6; type = any};]")))),
        ::testing::Values(TColumnFilter(), TColumnFilter({2, 4})),
        ::testing::Values(
            TLegacyReadRange(),
            TLegacyReadRange(TLegacyReadLimit().SetRowIndex(RowCount / 3), TLegacyReadLimit().SetRowIndex(RowCount / 3)),
            TLegacyReadRange(TLegacyReadLimit().SetRowIndex(RowCount / 3), TLegacyReadLimit().SetRowIndex(2 * RowCount / 3)))));


INSTANTIATE_TEST_SUITE_P(Sorted,
    TSchemalessChunksTest,
    ::testing::Combine(
        ::testing::Values(
            EOptimizeFor::Scan,
            EOptimizeFor::Lookup),
        ::testing::Values(
            ConvertTo<TTableSchema>(TYsonString(TStringBuf("<strict=%false>["
                "{name = c0; type = any; sort_order = ascending};"
                "{name = c1; type = any; sort_order = ascending};"
                "{name = c2; type = any; sort_order = ascending}]"))),
            ConvertTo<TTableSchema>(TYsonString(TStringBuf("<strict=%true>["
                "{name = c0; type = any; sort_order = ascending};"
                "{name = c1; type = int64; sort_order = ascending};"
                "{name = c2; type = uint64; sort_order = ascending};"
                "{name = c3; type = string; sort_order = ascending};"
                "{name = c4; type = boolean; sort_order = ascending};"
                "{name = c5; type = double; sort_order = ascending};"
                "{name = c6; type = any};]")))),
        ::testing::Values(TColumnFilter(), TColumnFilter({0, 5})),
        ::testing::Values(
            TLegacyReadRange(),
            TLegacyReadRange(TLegacyReadLimit().SetLegacyKey(YsonToKey("<type=null>#")), TLegacyReadLimit().SetLegacyKey(YsonToKey("<type=null>#"))),
            TLegacyReadRange(TLegacyReadLimit().SetLegacyKey(YsonToKey("-65537; -1; 1u; <type=null>#")), TLegacyReadLimit()),
            TLegacyReadRange(TLegacyReadLimit().SetLegacyKey(YsonToKey("-65537; -1; 1u; <type=null>#")), TLegacyReadLimit().SetLegacyKey(YsonToKey("350000.1; 1; 1; \"Z\""))))));

// ToDo(psushin):
//  1. Test sampling.
//  2. Test system columns.

////////////////////////////////////////////////////////////////////////////////

class TColumnarReadTest
    : public ::testing::Test
{
protected:
    IChunkReaderPtr MemoryReader_;
    TChunkSpec ChunkSpec_;
    TColumnarChunkMetaPtr ChunkMeta_;
    TChunkStatePtr ChunkState_;
    TSharedRange<TUnversionedRow> Rows_;

    static constexpr int N = 100003;

    const TTableSchemaPtr Schema_ = ConvertTo<TTableSchemaPtr>(TYsonString(TStringBuf(
        "<strict=%true>["
            "{name = c1; type = int64; sort_order = ascending};"
            "{name = c2; type = uint64};"
            "{name = c3; type = string};"
            "{name = c4; type = boolean};"
            "{name = c5; type = double};"
            "{name = c6; type = any};"
        "]")));

    void SetUp() override
    {
        auto memoryWriter = New<TMemoryWriter>();

        auto config = New<TChunkWriterConfig>();
        config->Postprocess();
        config->BlockSize = 256;
        config->Postprocess();

        auto options = New<TChunkWriterOptions>();
        options->OptimizeFor = EOptimizeFor::Scan;
        options->Postprocess();

        auto chunkWriter = CreateSchemalessChunkWriter(
            config,
            options,
            Schema_,
            /*nameTable*/ nullptr,
            memoryWriter,
            /*dataSink*/ std::nullopt);

        TUnversionedRowsBuilder builder;

        for (int i = 0; i < N; ++i) {
            builder.AddRow(
                i % 10,
                i / 10,
                Format("c3_%v", i),
                i % 7 == 0,
                i * i / 2.0,
                TYsonString(Format("{key=%v;value=%v}", i, i + 10)));
        }

        Rows_ = builder.Build();

        chunkWriter->Write(Rows_);
        EXPECT_TRUE(chunkWriter->Close().Get().IsOK());

        MemoryReader_ = CreateMemoryReader(
            memoryWriter->GetChunkMeta(),
            memoryWriter->GetBlocks());

        ToProto(ChunkSpec_.mutable_chunk_id(), NullChunkId);
        ChunkSpec_.set_table_row_index(42);

        ChunkMeta_ = New<TColumnarChunkMeta>(*memoryWriter->GetChunkMeta());

        ChunkState_ = New<TChunkState>(
            GetNullBlockCache(),
            ChunkSpec_);
        ChunkState_->TableSchema = Schema_;
    }

    virtual ISchemalessUnversionedReaderPtr CreateReader(const TColumnFilter& columnFilter)
    {
        return CreateSchemalessRangeChunkReader(
            ChunkState_,
            ChunkMeta_,
            TChunkReaderConfig::GetDefault(),
            TChunkReaderOptions::GetDefault(),
            MemoryReader_,
            TNameTable::FromSchema(*Schema_),
            /* chunkReadOptions */ {},
            /* sortColumns */ {},
            /* omittedInaccessibleColumns */ {},
            columnFilter,
            TReadRange());
    }
};

TEST_F(TColumnarReadTest, UnreadBatch)
{
    auto reader = CreateReader(TColumnFilter{0});
    TRowBatchReadOptions options{
        .MaxRowsPerRead = 10,
        .Columnar = true
    };
    while (auto batch = ReadRowBatch(reader, options)) {
    }
    auto statistics = reader->GetDataStatistics();
    EXPECT_EQ(N, statistics.row_count());
    EXPECT_EQ(N * 9, statistics.data_weight());
}

TEST_F(TColumnarReadTest, ReadJustC1)
{
    auto reader = CreateReader(TColumnFilter{0});
    TRowBatchReadOptions options{
        .MaxRowsPerRead = 10,
        .Columnar = true
    };
    while (auto batch = ReadRowBatch(reader, options)) {
        auto columnarBatch = batch->TryAsColumnar();
        ASSERT_TRUE(columnarBatch.operator bool());
        auto columns = columnarBatch->MaterializeColumns();
        EXPECT_EQ(1u, columns.size());
        EXPECT_EQ(0, columns[0]->Id);
    }
    auto statistics = reader->GetDataStatistics();
    EXPECT_EQ(N, statistics.row_count());
    EXPECT_EQ(N * 9, statistics.data_weight());
}

TEST_F(TColumnarReadTest, ReadAll)
{
    auto reader = CreateReader(TColumnFilter());
    TRowBatchReadOptions options{
        .MaxRowsPerRead = 10,
        .Columnar = true
    };
    while (auto batch = ReadRowBatch(reader, options)) {
        auto columnarBatch = batch->TryAsColumnar();
        ASSERT_TRUE(columnarBatch.operator bool());
        auto columns = columnarBatch->MaterializeColumns();
        EXPECT_EQ(Schema_->GetColumnCount(), std::ssize(columns));
        for (int index = 0; index < std::ssize(columns); ++index) {
            EXPECT_EQ(index, columns[index]->Id);
        }
    }
    auto statistics = reader->GetDataStatistics();
    EXPECT_EQ(N, statistics.row_count());
}

////////////////////////////////////////////////////////////////////////////////

class TSchemalessChunksLookupTest
    : public ::testing::TestWithParam<std::tuple<EOptimizeFor, TTableSchema>>
{
protected:
    TUnversionedValue CreateInt64(int rowIndex, int id)
    {
        if (rowIndex % 20 == 0) {
            return MakeUnversionedSentinelValue(EValueType::Null, id);
        } else {
            return MakeUnversionedInt64Value((rowIndex << 10) ^ rowIndex, id);
        }
    }

    TUnversionedValue CreateUint64(int rowIndex, int id)
    {
        if (rowIndex % 20 == 0) {
            return MakeUnversionedSentinelValue(EValueType::Null, id);
        } else {
            return MakeUnversionedUint64Value((rowIndex << 10) ^ rowIndex, id);
        }
    }

    TUnversionedValue CreateDouble(int rowIndex, int id)
    {
        if (rowIndex % 20 == 0) {
            return MakeUnversionedSentinelValue(EValueType::Null, id);
        } else {
            return MakeUnversionedDoubleValue(static_cast<double>((rowIndex << 10) ^ rowIndex), id);
        }
    }

    TUnversionedValue CreateBoolean(int rowIndex, int id)
    {
        if (rowIndex % 20 == 0) {
            return MakeUnversionedSentinelValue(EValueType::Null, id);
        } else {
            return MakeUnversionedBooleanValue(static_cast<bool>(rowIndex % 3), id);
        }
    }

    TUnversionedValue CreateString(int rowIndex, int id)
    {
        if (rowIndex % 20 == 0) {
            return MakeUnversionedSentinelValue(EValueType::Null, id);
        } else {
            return MakeUnversionedStringValue(StringValue, id);
        }
    }

    TUnversionedValue CreateAny(int rowIndex, int id)
    {
        if (rowIndex % 20 == 0) {
            return MakeUnversionedSentinelValue(EValueType::Null, id);
        } else if (rowIndex % 20 < 11) {
            return MakeUnversionedAnyValue(AnyValueList, id);
        } else {
            return MakeUnversionedAnyValue(AnyValueMap, id);
        }
    }

    TUnversionedValue CreateValue(int rowIndex, int id, const TColumnSchema& columnSchema)
    {
        switch (columnSchema.GetWireType()) {
            case EValueType::Int64:
                return CreateInt64(rowIndex, id);
            case EValueType::Uint64:
                return CreateUint64(rowIndex, id);
            case EValueType::Double:
                return CreateDouble(rowIndex, id);
            case EValueType::Boolean:
                return CreateBoolean(rowIndex, id);
            case EValueType::String:
                return CreateString(rowIndex, id);
            case EValueType::Any:
                return CreateAny(rowIndex, id);
            case EValueType::Null:
            case EValueType::Composite:
            case EValueType::TheBottom:
            case EValueType::Min:
            case EValueType::Max:
                break;
        }
        YT_ABORT();
    }

    TUnversionedRow CreateRow(int rowIndex, TTableSchemaPtr schema, TNameTablePtr nameTable)
    {
        auto row = TMutableUnversionedRow::Allocate(&Pool_, schema->GetColumnCount());
        for (int index = 0; index < std::ssize(schema->Columns()); ++index) {
            const auto& column = schema->Columns()[index];
            row[index] = CreateValue(rowIndex, nameTable->GetIdOrRegisterName(column.Name()), column);
        }
        return row;
    }

    void InitRows(int rowCount, TTableSchemaPtr schema, TNameTablePtr nameTable)
    {
        std::vector<TUnversionedRow> rows;

        for (int index = 0; index < rowCount; ++index) {
            rows.push_back(CreateRow(index, schema, nameTable));
        }

        std::sort(
            rows.begin(),
            rows.end(), [&] (const TUnversionedRow& lhs, const TUnversionedRow& rhs) {
                return CompareRows(lhs, rhs, schema->GetKeyColumnCount()) < 0;
            });
        rows.erase(
            std::unique(
                rows.begin(),
                rows.end(), [&] (const TUnversionedRow& lhs, const TUnversionedRow& rhs) {
                    return CompareRows(lhs, rhs, schema->GetKeyColumnCount()) == 0;
                }),
            rows.end());

        Rows_ = std::move(rows);
    }

    void InitChunk(int rowCount, EOptimizeFor optimizeFor, TTableSchemaPtr schema)
    {
        auto memoryWriter = New<TMemoryWriter>();

        auto config = New<TChunkWriterConfig>();
        config->BlockSize = 2 * 1024;
        config->Postprocess();

        auto options = New<TChunkWriterOptions>();
        options->OptimizeFor = optimizeFor;
        options->ValidateSorted = schema->IsSorted();
        options->ValidateUniqueKeys = schema->IsUniqueKeys();
        options->Postprocess();

        auto chunkWriter = CreateSchemalessChunkWriter(
            config,
            options,
            schema,
            /*nameTable*/ nullptr,
            memoryWriter);

        WriteNameTable_ = chunkWriter->GetNameTable();
        InitRows(rowCount, schema, WriteNameTable_);

        chunkWriter->Write(Rows_);
        EXPECT_TRUE(chunkWriter->Close().Get().IsOK());

        MemoryReader_ = CreateMemoryReader(
            memoryWriter->GetChunkMeta(),
            memoryWriter->GetBlocks());

        ToProto(ChunkSpec_.mutable_chunk_id(), NullChunkId);
        ChunkSpec_.set_table_row_index(42);
    }

    ISchemalessChunkReaderPtr LookupRows(
        TSharedRange<TLegacyKey> keys,
        const TSortColumns& sortColumns)
    {
        auto options = New<TChunkReaderOptions>();
        options->DynamicTable = true;

        auto asyncCachedMeta = MemoryReader_->GetMeta(/*chunkReadOptions*/ {})
            .Apply(BIND(
                &TCachedVersionedChunkMeta::Create,
                /*prepareColumnarMeta*/ false,
                /*memoryTracker*/ nullptr));

        auto chunkMeta = WaitFor(asyncCachedMeta)
            .ValueOrThrow();

        auto chunkState = New<TChunkState>(
            GetNullBlockCache(),
            ChunkSpec_);
        chunkState->TableSchema = Schema_;

        return CreateSchemalessLookupChunkReader(
            std::move(chunkState),
            chunkMeta,
            TChunkReaderConfig::GetDefault(),
            options,
            MemoryReader_,
            WriteNameTable_,
            /* chunkReadOptions */ {},
            sortColumns,
            /* omittedInaccessibleColumns */ {},
            /* columnFilter */ {},
            keys);
    }

    void SetUp() override
    {
        auto optimizeFor = std::get<0>(GetParam());
        Schema_ = New<TTableSchema>(std::get<1>(GetParam()));
        InitChunk(1000, optimizeFor, Schema_);
    }

    TTableSchemaPtr Schema_;
    IChunkReaderPtr MemoryReader_;
    TNameTablePtr WriteNameTable_;
    TChunkSpec ChunkSpec_;

    TChunkedMemoryPool Pool_;
    std::vector<TUnversionedRow> Rows_;
};

TEST_P(TSchemalessChunksLookupTest, Simple)
{
    std::vector<TUnversionedRow> expected;
    std::vector<TUnversionedRow> keys;

    for (int index = 0; index < std::ssize(Rows_); ++index) {
        if (index % 10 != 0) {
            continue;
        }

        auto row = Rows_[index];
        expected.push_back(row);

        auto key = TMutableUnversionedRow::Allocate(&Pool_, Schema_->GetKeyColumnCount());
        for (int valueIndex = 0; valueIndex < Schema_->GetKeyColumnCount(); ++valueIndex) {
            key[valueIndex] = row[valueIndex];
        }
        keys.push_back(key);
    }

    auto reader = LookupRows(MakeSharedRange(keys), Schema_->GetSortColumns());
    CheckSchemalessResult(expected, reader, Schema_->GetKeyColumnCount());
}

TEST_P(TSchemalessChunksLookupTest, WiderKeyColumns)
{
    std::vector<TUnversionedRow> expected;
    std::vector<TUnversionedRow> keys;

    auto sortColumns = Schema_->GetSortColumns();
    sortColumns.push_back({"w1", ESortOrder::Ascending});
    sortColumns.push_back({"w2", ESortOrder::Ascending});

    for (int index = 0; index < std::ssize(Rows_); ++index) {
        if (index % 10 != 0) {
            continue;
        }

        auto row = Rows_[index];
        expected.push_back(row);

        auto key = TMutableUnversionedRow::Allocate(&Pool_, sortColumns.size());
        for (int valueIndex = 0; valueIndex < Schema_->GetKeyColumnCount(); ++valueIndex) {
            key[valueIndex] = row[valueIndex];
        }
        for (int valueIndex = Schema_->GetKeyColumnCount(); valueIndex < static_cast<int>(key.GetCount()); ++valueIndex) {
            key[valueIndex] = MakeUnversionedSentinelValue(EValueType::Null, valueIndex);
        }
        keys.push_back(key);
    }

    auto reader = LookupRows(MakeSharedRange(keys), sortColumns);
    CheckSchemalessResult(expected, reader, Schema_->GetKeyColumnCount());
}

INSTANTIATE_TEST_SUITE_P(Sorted,
    TSchemalessChunksLookupTest,
    ::testing::Combine(
        ::testing::Values(
            EOptimizeFor::Scan,
            EOptimizeFor::Lookup),
        ::testing::Values(
            ConvertTo<TTableSchema>(TYsonString(TStringBuf("<strict=%true;unique_keys=%true>["
                "{name = c1; type = boolean; sort_order = ascending};"
                "{name = c2; type = uint64; sort_order = ascending};"
                "{name = c3; type = int64; sort_order = ascending};"
                "{name = c4; type = double};]"))),
            ConvertTo<TTableSchema>(TYsonString(TStringBuf("<strict=%true;unique_keys=%true>["
                "{name = c1; type = int64; sort_order = ascending};"
                "{name = c2; type = uint64; sort_order = ascending};"
                "{name = c3; type = string; sort_order = ascending};"
                "{name = c4; type = boolean; sort_order = ascending};"
                "{name = c5; type = any};]"))))));

////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NYT::NTableClient
