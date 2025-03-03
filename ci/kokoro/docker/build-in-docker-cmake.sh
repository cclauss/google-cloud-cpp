#!/usr/bin/env bash
#
# Copyright 2017 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -eu

if [[ $# != 2 ]]; then
  echo "Usage: $(basename "$0") <source-directory> <binary-directory>"
  exit 1
fi

readonly SOURCE_DIR="$1"
readonly BINARY_DIR="$2"

# This script is supposed to run inside a Docker container, see
# ci/kokoro/docker/build.sh for the expected setup.  The /v directory is a
# volume pointing to a (clean-ish) checkout of google-cloud-cpp:
if [[ -z "${PROJECT_ROOT+x}" ]]; then
  readonly PROJECT_ROOT="/v"
fi
source "${PROJECT_ROOT}/ci/colors.sh"

# Run the configure / compile / test cycle inside a docker image.
# This script is designed to work in the context created by the
# ci/Dockerfile.* build scripts.

(cd "${PROJECT_ROOT}" ; ./ci/check-style.sh)

CMAKE_COMMAND="cmake"
if [[ "${SCAN_BUILD}" == "yes" ]]; then
  CMAKE_COMMAND="scan-build --use-cc=${CC} --use-c++=${CXX} cmake"
fi

echo
echo "${COLOR_YELLOW}Starting docker build $(date) with ${NCPU} cores${COLOR_RESET}"
echo

echo "${COLOR_YELLOW}Started CMake config at: $(date)${COLOR_RESET}"
# Extra flags to pass to CMake based on our build configurations.
declare -a cmake_extra_flags
if [[ "${BUILD_TESTING:-}" == "no" ]]; then
  cmake_extra_flags+=( "-DBUILD_TESTING=OFF" )
fi

if [[ "${TEST_INSTALL:-}" == "yes" ]]; then
  cmake_extra_flags+=( "-DGOOGLE_CLOUD_CPP_DEPENDENCY_PROVIDER=package"
      "-DGOOGLE_CLOUD_CPP_TESTING_UTIL_ENABLE_INSTALL=ON" )
fi

if [[ "${SCAN_BUILD:-}" == "yes" ]]; then
  cmake_extra_flags+=( "-DGOOGLE_CLOUD_CPP_DEPENDENCY_PROVIDER=package"
      "-DGOOGLE_CLOUD_CPP_ENABLE_CCACHE=OFF" )
fi

if [[ "${USE_LIBCXX:-}" == "yes" ]]; then
  cmake_extra_flags+=( "-DGOOGLE_CLOUD_CPP_USE_LIBCXX=ON" )
fi

# We use parameter expansion for ${cmake_extra_flags} because set -u doesn't
# like empty arrays on older versions of Bash (which some of our builds use).
# The expression ${parameter+word} will expand word only if parameter is not
# unset. We also disable the shellcheck warning because we want ${CMAKE_FLAGS}
# to expand as separate arguments.
# shellcheck disable=SC2086
${CMAKE_COMMAND} \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    "${cmake_extra_flags[@]+"${cmake_extra_flags[@]}"}" \
    ${CMAKE_FLAGS:-} \
    -H${SOURCE_DIR} \
    -B"${BINARY_DIR}"
echo
echo "${COLOR_YELLOW}Finished CMake config at: $(date)${COLOR_RESET}"

# CMake can generate dependency graphs, which are useful to understand and
# troubleshoot dependencies.
if [[ "${CREATE_GRAPHVIZ:-}" == "yes" ]]; then
  ${CMAKE_COMMAND} \
      --graphviz="${BINARY_DIR}/graphviz/google-cloud-cpp" \
      --build "${BINARY_DIR}"
fi

# If scan-build is enabled, we need to manually compile the dependencies;
# otherwise, the static analyzer finds issues in them, and there is no way to
# ignore them.  When scan-build is not enabled, this is still useful because
# we can fold the output in Travis and make the log more interesting.
echo "${COLOR_YELLOW}Started dependency build at: $(date)${COLOR_RESET}"
echo
cmake --build "${BINARY_DIR}" \
    --target google-cloud-cpp-dependencies -- -j "${NCPU}"
echo
echo "${COLOR_YELLOW}Finished dependency build at: $(date)${COLOR_RESET}"

# If scan-build is enabled we build the smallest subset of things that is
# needed; otherwise, we pick errors from things we do not care about. With
# scan-build disabled we compile everything, to test the build as most
# developers will experience it.
echo "${COLOR_YELLOW}Started build at: $(date)${COLOR_RESET}"
${CMAKE_COMMAND} --build "${BINARY_DIR}" -- -j "${NCPU}"
echo "${COLOR_YELLOW}Finished build at: $(date)${COLOR_RESET}"

# Collect the output from the Clang static analyzer and provide instructions to
# the developers on how to do that locally.
if [[ "${SCAN_BUILD:-}" = "yes" ]]; then
  if [[ -n "$(ls -1d /tmp/scan-build-* 2>/dev/null)" ]]; then
    cp -r /tmp/scan-build-* /v/scan-cmake-out
  fi
  if [[ -r scan-cmake-out/index.html ]]; then
    cat <<_EOF_;

${COLOR_RED}
scan-build detected errors.  Please read the log for details. To
run scan-build locally and examine the HTML output install and configure Docker,
then run:

./ci/kokoro/docker/build.sh scan-build

The HTML output will be copied into the scan-cmake-out subdirectory.
${COLOR_RESET}
_EOF_
    exit 1
  else
    echo
    echo "${COLOR_GREEN}scan-build completed without errors.${COLOR_RESET}"
  fi
fi

if [[ "${BUILD_TESTING:-}" = "yes" ]]; then
  # Run the tests and output any failures.
  echo
  echo "${COLOR_YELLOW}Running unit and integration tests $(date)${COLOR_RESET}"
  echo
  (cd "${BINARY_DIR}" && ctest --output-on-failure)

  # Run the integration tests. Not all projects have them, so just iterate over
  # the ones that do.
  for subdir in google/cloud google/cloud/bigtable google/cloud/storage; do
    echo
    echo "${COLOR_GREEN}Running integration tests for ${subdir}${COLOR_RESET}"
    (cd "${BINARY_DIR}" && "${PROJECT_ROOT}/${subdir}/ci/run_integration_tests.sh")
  done
  echo
  echo "${COLOR_YELLOW}Completed unit and integration tests $(date)${COLOR_RESET}"
  echo
fi

# Test the install rule and that the installation works.
if [[ "${TEST_INSTALL:-}" = "yes" ]]; then
  echo
  echo "${COLOR_YELLOW}Testing install rule.${COLOR_RESET}"
  cmake --build "${BINARY_DIR}" --target install || echo "FAILED"
  echo

  # Checking the ABI requires installation, so this is the first opportunity to
  # run the check.
  (cd "${PROJECT_ROOT}" ; ./ci/kokoro/docker/check-abi.sh "${BINARY_DIR}")

  # Also verify that the install directory does not get unexpected files or
  # directories installed.
  echo
  echo "${COLOR_YELLOW}Verify installed headers created only" \
      " expected directories.${COLOR_RESET}"
  cmake --build "${BINARY_DIR}" --target install -- DESTDIR=/var/tmp/staging
  if comm -23 \
      <(find /var/tmp/staging/usr/local/include/google/cloud -type d | sort) \
      <(echo /var/tmp/staging/usr/local/include/google/cloud ; \
        echo /var/tmp/staging/usr/local/include/google/cloud/bigtable ; \
        echo /var/tmp/staging/usr/local/include/google/cloud/bigtable/internal ; \
        echo /var/tmp/staging/usr/local/include/google/cloud/firestore ; \
        echo /var/tmp/staging/usr/local/include/google/cloud/grpc_utils ; \
        echo /var/tmp/staging/usr/local/include/google/cloud/grpc_utils/internal ; \
        echo /var/tmp/staging/usr/local/include/google/cloud/internal ; \
        echo /var/tmp/staging/usr/local/include/google/cloud/spanner; \
        echo /var/tmp/staging/usr/local/include/google/cloud/storage ; \
        echo /var/tmp/staging/usr/local/include/google/cloud/storage/internal ; \
        echo /var/tmp/staging/usr/local/include/google/cloud/storage/oauth2 ; \
        echo /var/tmp/staging/usr/local/include/google/cloud/storage/testing ; \
        echo /var/tmp/staging/usr/local/include/google/cloud/testing_util ; \
        /bin/true) | grep -q /var/tmp; then
      echo "${COLOR_YELLOW}Installed directories do not match expectation.${COLOR_RESET}"
      echo "${COLOR_RED}Found:"
      find /var/tmp/staging/usr/local/include/google/cloud -type d | sort
      echo "${COLOR_RESET}"
      /bin/false
   fi
fi

# If document generation is enabled, run it now.
if [[ "${GENERATE_DOCS}" == "yes" ]]; then
  echo
  echo "${COLOR_YELLOW}Generating Doxygen documentation at: $(date).${COLOR_RESET}"
  cmake --build "${BINARY_DIR}" --target doxygen-docs
fi
