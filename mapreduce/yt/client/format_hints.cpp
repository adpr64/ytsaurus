#include "format_hints.h"

#include <mapreduce/yt/common/helpers.h>
#include <mapreduce/yt/interface/operation.h>

namespace NYT {
namespace NDetail {

////////////////////////////////////////////////////////////////////////////////

static void ApplyEnableTypeConversion(TFormat* format, const TFormatHints& formatHints)
{
    if (formatHints.EnableAllToStringConversion_) {
        format->Config.Attributes()["enable_all_to_string_conversion"] = *formatHints.EnableAllToStringConversion_;
    }
    if (formatHints.EnableStringToAllConversion_) {
        format->Config.Attributes()["enable_string_to_all_conversion"] = *formatHints.EnableStringToAllConversion_;
    }
    if (formatHints.EnableIntegralTypeConversion_) {
        format->Config.Attributes()["enable_integral_type_conversion"] = *formatHints.EnableIntegralTypeConversion_;
    }
    if (formatHints.EnableIntegralToDoubleConversion_) {
        format->Config.Attributes()["enable_integral_to_double_conversion"] = *formatHints.EnableIntegralToDoubleConversion_;
    }
    if (formatHints.EnableTypeConversion_) {
        format->Config.Attributes()["enable_type_conversion"] = *formatHints.EnableTypeConversion_;
    }
}

template <>
void ApplyFormatHints<TNode>(TFormat* format, const TMaybe<TFormatHints>& formatHints)
{
    Y_VERIFY(format);
    if (!formatHints) {
        return;
    }

    ApplyEnableTypeConversion(format, *formatHints);

    if (formatHints->SkipNullValuesForTNode_) {
        Y_ENSURE_EX(
            format->Config.AsString() == "yson",
            TApiUsageError() << "SkipNullForTNode option must be used with yson format, actual format: " << format->Config.AsString());
        format->Config.Attributes()["skip_null_values"] = formatHints->SkipNullValuesForTNode_;
    }

}

template <>
void ApplyFormatHints<TYaMRRow>(TFormat* format, const TMaybe<TFormatHints>& formatHints)
{
    Y_VERIFY(format);
    if (!formatHints) {
        return;
    }

    ythrow TApiUsageError() << "Yamr format currently has no supported format hints";
}

template <>
void ApplyFormatHints<::google::protobuf::Message>(TFormat* format, const TMaybe<TFormatHints>& formatHints)
{
    Y_VERIFY(format);
    if (!formatHints) {
        return;
    }

    ythrow TApiUsageError() << "Protobuf format currently has no supported format hints";
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NDetail
} // namespace NYT
