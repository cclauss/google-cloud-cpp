// Copyright 2017 Google Inc.
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

#ifndef GOOGLE_CLOUD_CPP_GOOGLE_CLOUD_INTERNAL_BUILD_INFO_H_
#define GOOGLE_CLOUD_CPP_GOOGLE_CLOUD_INTERNAL_BUILD_INFO_H_

#include "google/cloud/version.h"

namespace google {
namespace cloud {
inline namespace GOOGLE_CLOUD_CPP_NS {
namespace internal {
/// The compiler version.
std::string compiler();

/// The language version for user-agent header.
std::string language_version();

/// The compiler flags.
std::string compiler_flags();

/// True if this is a release, false for the development branches.
bool is_release();

/// Build metadata injected by the build system.
std::string build_metadata();

}  // namespace internal
}  // namespace GOOGLE_CLOUD_CPP_NS
}  // namespace cloud
}  // namespace google

#endif  // GOOGLE_CLOUD_CPP_GOOGLE_CLOUD_INTERNAL_BUILD_INFO_H_
