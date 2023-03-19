//
//
// Copyright 2020 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#ifndef GRPC_CORE_LIB_JSON_JSON_UTIL_H
#define GRPC_CORE_LIB_JSON_JSON_UTIL_H

#include <grpc/support/port_platform.h>

#include "y_absl/strings/numbers.h"
#include "y_absl/strings/str_cat.h"

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/json/json.h"

namespace grpc_core {

// Parses a JSON field of the form generated for a google.proto.Duration
// proto message, as per:
//   https://developers.google.com/protocol-buffers/docs/proto3#json
// Returns true on success, false otherwise.
bool ParseDurationFromJson(const Json& field, grpc_millis* duration);

//
// Helper functions for extracting types from JSON.
// Return true on success, false otherwise. If an error is encountered during
// parsing, a descriptive error is appended to \a error_list.
//
template <typename NumericType>
bool ExtractJsonNumber(const Json& json, y_absl::string_view field_name,
                       NumericType* output,
                       std::vector<grpc_error_handle>* error_list) {
  static_assert(std::is_integral<NumericType>::value, "Integral required");
  if (json.type() != Json::Type::NUMBER) {
    error_list->push_back(GRPC_ERROR_CREATE_FROM_CPP_STRING(
        y_absl::StrCat("field:", field_name, " error:type should be NUMBER")));
    return false;
  }
  if (!y_absl::SimpleAtoi(json.string_value(), output)) {
    error_list->push_back(GRPC_ERROR_CREATE_FROM_CPP_STRING(
        y_absl::StrCat("field:", field_name, " error:failed to parse.")));
    return false;
  }
  return true;
}

bool ExtractJsonBool(const Json& json, y_absl::string_view field_name,
                     bool* output, std::vector<grpc_error_handle>* error_list);

// OutputType can be TString or y_absl::string_view.
template <typename OutputType>
bool ExtractJsonString(const Json& json, y_absl::string_view field_name,
                       OutputType* output,
                       std::vector<grpc_error_handle>* error_list) {
  if (json.type() != Json::Type::STRING) {
    *output = "";
    error_list->push_back(GRPC_ERROR_CREATE_FROM_CPP_STRING(
        y_absl::StrCat("field:", field_name, " error:type should be STRING")));
    return false;
  }
  *output = json.string_value();
  return true;
}

bool ExtractJsonArray(const Json& json, y_absl::string_view field_name,
                      const Json::Array** output,
                      std::vector<grpc_error_handle>* error_list);

bool ExtractJsonObject(const Json& json, y_absl::string_view field_name,
                       const Json::Object** output,
                       std::vector<grpc_error_handle>* error_list);

// Wrappers for automatically choosing one of the above functions based
// on output parameter type.
template <typename NumericType>
inline bool ExtractJsonType(const Json& json, y_absl::string_view field_name,
                            NumericType* output,
                            std::vector<grpc_error_handle>* error_list) {
  return ExtractJsonNumber(json, field_name, output, error_list);
}
inline bool ExtractJsonType(const Json& json, y_absl::string_view field_name,
                            bool* output,
                            std::vector<grpc_error_handle>* error_list) {
  return ExtractJsonBool(json, field_name, output, error_list);
}
inline bool ExtractJsonType(const Json& json, y_absl::string_view field_name,
                            TString* output,
                            std::vector<grpc_error_handle>* error_list) {
  return ExtractJsonString(json, field_name, output, error_list);
}
inline bool ExtractJsonType(const Json& json, y_absl::string_view field_name,
                            y_absl::string_view* output,
                            std::vector<grpc_error_handle>* error_list) {
  return ExtractJsonString(json, field_name, output, error_list);
}
inline bool ExtractJsonType(const Json& json, y_absl::string_view field_name,
                            const Json::Array** output,
                            std::vector<grpc_error_handle>* error_list) {
  return ExtractJsonArray(json, field_name, output, error_list);
}
inline bool ExtractJsonType(const Json& json, y_absl::string_view field_name,
                            const Json::Object** output,
                            std::vector<grpc_error_handle>* error_list) {
  return ExtractJsonObject(json, field_name, output, error_list);
}

// Extracts a field from a JSON object, automatically selecting the type
// of parsing based on the output parameter type.
// If the field is not present, returns false, and if required is true,
// adds an error to error_list.
// Upon any other error, adds an error to error_list and returns false.
template <typename T>
bool ParseJsonObjectField(const Json::Object& object,
                          y_absl::string_view field_name, T* output,
                          std::vector<grpc_error_handle>* error_list,
                          bool required = true) {
  // TODO(roth): Once we can use C++14 heterogenous lookups, stop
  // creating a TString here.
  auto it = object.find(TString(field_name));
  if (it == object.end()) {
    if (required) {
      error_list->push_back(GRPC_ERROR_CREATE_FROM_CPP_STRING(
          y_absl::StrCat("field:", field_name, " error:does not exist.")));
    }
    return false;
  }
  auto& child_object_json = it->second;
  return ExtractJsonType(child_object_json, field_name, output, error_list);
}

// Alternative to ParseJsonObjectField() for duration-value fields.
bool ParseJsonObjectFieldAsDuration(const Json::Object& object,
                                    y_absl::string_view field_name,
                                    grpc_millis* output,
                                    std::vector<grpc_error_handle>* error_list,
                                    bool required = true);

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_JSON_JSON_UTIL_H
