#include "stdafx.h"
#include "channel_writer.h"
#include "value.h"

#include <ytlib/misc/serialize.h>

namespace NYT {
namespace NTableClient {

///////////////////////////////////////////////////////////////////////////////

TChannelWriter::TChannelWriter(
    int bufferIndex,
    int fixedColumnCount,
    bool writeRangeSizes)
    : BufferIndex_(bufferIndex)
    , HeapIndex_(bufferIndex)
    , FixedColumns(fixedColumnCount)
    , IsColumnUsed(fixedColumnCount)
    , CurrentRowCount(0)
    , WriteRangeSizes(writeRangeSizes)
    , RangeOffset(0)
    , CurrentSize(0)
{ }

int TChannelWriter::WriteFixed(int columnIndex, const TStringBuf& value)
{
    auto currentSize = CurrentSize;
    auto& columnOutput = FixedColumns[columnIndex];
    CurrentSize += TValue(value).Save(&columnOutput);
    IsColumnUsed[columnIndex] = true;
    return CurrentSize - currentSize;
}

int TChannelWriter::WriteRange(const TStringBuf& name, const TStringBuf& value)
{
    auto currentSize = CurrentSize;
    CurrentSize += TValue(value).Save(&RangeColumns);
    CurrentSize += WriteVarInt32(&RangeColumns, static_cast<i32>(name.length()));
    CurrentSize += name.length();
    RangeColumns.Write(name);
    return CurrentSize - currentSize;
}

int TChannelWriter::WriteRange(int chunkColumnIndex, const TStringBuf& value)
{
    auto currentSize = CurrentSize;
    YASSERT(chunkColumnIndex > 0);
    CurrentSize += TValue(value).Save(&RangeColumns);
    CurrentSize += WriteVarInt32(&RangeColumns, -(chunkColumnIndex + 1));
    return CurrentSize - currentSize;
}

int TChannelWriter::EndRow()
{
    auto currentSize = CurrentSize;
    for (int columnIdx = 0; columnIdx < IsColumnUsed.size(); ++columnIdx) {
        if (IsColumnUsed[columnIdx]) {
            // Clean flags
            IsColumnUsed[columnIdx] = false;
        } else {
            auto& columnData = FixedColumns[columnIdx];
            CurrentSize += TValue().Save(&columnData);
        }
    }

    // End of the row
    CurrentSize += TValue().Save(&RangeColumns);

    if (WriteRangeSizes) {
        CurrentSize += WriteVarUInt64(&RangeSizes, RangeColumns.GetSize() - RangeOffset);
        RangeOffset = RangeColumns.GetSize();
    }

    ++ CurrentRowCount;
    return CurrentSize - currentSize;
}

size_t TChannelWriter::GetCurrentSize() const
{
    return CurrentSize;
}

std::vector<TSharedRef> TChannelWriter::FlushBlock()
{
    TBlobOutput sizeOutput(8 * (FixedColumns.size() + 1));

    FOREACH (const auto& column, FixedColumns) {
        WriteVarUInt64(&sizeOutput, column.GetSize());
    }
    WriteVarUInt64(&sizeOutput, RangeColumns.GetSize());

    std::vector<TSharedRef> result;
    result.reserve(FixedColumns.size() + 3);
    result.push_back(sizeOutput.Flush());

    FOREACH (auto& column, FixedColumns) {
        auto capacity = column.GetBlob()->capacity();
        result.push_back(column.Flush());
        column.Reserve(capacity);
    }

    {
        auto capacity = RangeColumns.GetBlob()->capacity();
        result.push_back(RangeColumns.Flush());
        RangeColumns.Reserve(capacity);
    }

    if (WriteRangeSizes) {
        auto capacity = RangeSizes.GetBlob()->capacity();
        result.push_back(RangeSizes.Flush());
        RangeSizes.Reserve(capacity);
        RangeOffset = 0;
    }

    CurrentSize = 0;
    CurrentRowCount = 0;

    return result;
}

i64 TChannelWriter::GetCurrentRowCount() const
{
    return CurrentRowCount;
}

///////////////////////////////////////////////////////////////////////////////

} // namespace NTableClient
} // namespace NYT
