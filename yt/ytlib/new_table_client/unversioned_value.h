#include "row_base.h"

#include <util/system/defaults.h>

#include <core/misc/farm_hash.h>

namespace NYT {
namespace NVersionedTableClient {

////////////////////////////////////////////////////////////////////////////////

// NB: Wire protocol readers/writer rely on this fixed layout.
union TUnversionedValueData
{
    //! |Int64| value.
    i64 Int64;
    //! |Uint64| value.
    ui64 Uint64;
    //! |Double| value.
    double Double;
    //! |Boolean| value.
    bool Boolean;
    //! String value for |String| type or YSON-encoded value for |Any| type.
    const char* String;
};

static_assert(
    sizeof(TUnversionedValueData) == 8,
    "TUnversionedValueData has to be exactly 8 bytes.");

// NB: Wire protocol readers/writer rely on this fixed layout.
struct TUnversionedValue
{
    //! Column id obtained from a name table.
    ui16 Id;
    //! Column type.
    EValueType Type;
    //! Length of a variable-sized value (only meaningful for |String| and |Any| types).
    ui32 Length;

    TUnversionedValueData Data;
};

//! Computes hash for a given TUnversionedValue.
ui64 GetHash(const TUnversionedValue& value);

//! Computes FarmHash forever-fixed fingerprint for a given TUnversionedValue.
TFingerprint GetFarmFingerprint(const TUnversionedValue& value);

//! Computes FarmHash forever-fixed fingerprint for a given set of values.
TFingerprint GetFarmFingerprint(const TUnversionedValue* begin, const TUnversionedValue* end);

////////////////////////////////////////////////////////////////////////////////

} // namespace NVersionedTableClient
} // namespace NYT

