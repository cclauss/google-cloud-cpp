// Copyright 2018 Google LLC
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

#include "google/cloud/storage/version.h"
#include "google/cloud/internal/build_info.h"
#include <sstream>

namespace google {
namespace cloud {
namespace storage {
inline namespace STORAGE_CLIENT_NS {
// NOLINTNEXTLINE(readability-identifier-naming)
std::string version_string() {
  static std::string const kVersion = [] {
    std::ostringstream os;
    os << "v" << version_major() << "." << version_minor() << "."
       << version_patch();
    if (!google::cloud::internal::is_release()) {
      os << "+" << google::cloud::internal::build_metadata();
    }
    return os.str();
  }();
  return kVersion;
}

// NOLINTNEXTLINE(readability-identifier-naming)
std::string x_goog_api_client() {
  static std::string const kXGoogApiClient = [] {
    std::ostringstream os;
    os << "gl-cpp/" << google::cloud::internal::language_version() << " gccl/"
       << version_string();
    return os.str();
  }();
  return kXGoogApiClient;
}
}  // namespace STORAGE_CLIENT_NS
}  // namespace storage
}  // namespace cloud
}  // namespace google
