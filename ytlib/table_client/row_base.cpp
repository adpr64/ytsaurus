#include "row_base.h"

#include <yt/core/misc/error.h>
#include <yt/core/misc/small_vector.h>

namespace NYT {
namespace NTableClient {

////////////////////////////////////////////////////////////////////////////////

TColumnFilter::TColumnFilter()
    : All(true)
{ }

TColumnFilter::TColumnFilter(const std::initializer_list<int>& indexes)
    : All(false)
    , Indexes(indexes.begin(), indexes.end())
{ }

TColumnFilter::TColumnFilter(int schemaColumnCount)
    : All(false)
{
    for (int i = 0; i < schemaColumnCount; ++i) {
        Indexes.push_back(i);
    }
}

bool TColumnFilter::Contains(int index) const
{
    if (All) {
        return true;
    }

    return std::find(Indexes.begin(), Indexes.end(), index) != Indexes.end();
}

////////////////////////////////////////////////////////////////////////////////

void ValidateDataValueType(EValueType type)
{
    if (type != EValueType::Int64 &&
        type != EValueType::Uint64 &&
        type != EValueType::Double &&
        type != EValueType::Boolean &&
        type != EValueType::String &&
        type != EValueType::Any &&
        type != EValueType::Null)
    {
        THROW_ERROR_EXCEPTION("Invalid data value type %Qlv", type);
    }
}

void ValidateKeyValueType(EValueType type)
{
    // TODO(babenko): handle any
    if (type != EValueType::Int64 &&
        type != EValueType::Uint64 &&
        type != EValueType::Double &&
        type != EValueType::Boolean &&
        type != EValueType::String &&
        type != EValueType::Null &&
        type != EValueType::Min &&
        type != EValueType::Max)
    {
        THROW_ERROR_EXCEPTION("Invalid key value type %Qlv; only scalar types are allowed for the key columns", type);
    }
}

void ValidateSchemaValueType(EValueType type)
{
    // TODO(babenko): handle any
    if (type != EValueType::Int64 &&
        type != EValueType::Uint64 &&
        type != EValueType::Double &&
        type != EValueType::Boolean &&
        type != EValueType::String &&
        type != EValueType::Any)
    {
        THROW_ERROR_EXCEPTION("Invalid schema value type %Qlv", type);
    }
}

void ValidateColumnFilter(const TColumnFilter& columnFilter, int schemaColumnCount)
{
    if (columnFilter.All)
        return;

    SmallVector<bool, TypicalColumnCount> flags;
    flags.resize(schemaColumnCount);

    for (int index : columnFilter.Indexes) {
        if (index < 0 || index >= schemaColumnCount) {
            THROW_ERROR_EXCEPTION("Column filter contains invalid index: actual %v, expected in range [0, %v]",
                index,
                schemaColumnCount - 1);
        }
        if (flags[index]) {
            THROW_ERROR_EXCEPTION("Column filter contains duplicate index %v",
                index);
        }
        flags[index] = true;
    }
}

////////////////////////////////////////////////////////////////////////////////

TString ToString(const TColumnFilter& columnFilter)
{
    if (columnFilter.All) {
        return TString("{All}");
    } else {
        return Format("{%v}", columnFilter.Indexes);
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTableClient
} // namespace NYT
