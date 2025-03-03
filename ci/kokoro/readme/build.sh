#!/usr/bin/env bash
#
# Copyright 2019 Google LLC
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

if [[ $# -eq 1 ]]; then
  export DISTRO="${1}"
elif [[ -n "${KOKORO_JOB_NAME:-}" ]]; then
  # Kokoro injects the KOKORO_JOB_NAME environment variable, the value of this
  # variable is cloud-cpp/spanner/<config-file-name-without-cfg> (or more
  # generally <path/to/config-file-without-cfg>). By convention we name these
  # files `$foo.cfg` for continuous builds and `$foo-presubmit.cfg` for
  # presubmit builds. Here we extract the value of "foo" and use it as the build
  # name.
  DISTRO="$(basename "${KOKORO_JOB_NAME}" "-presubmit")"
  export DISTRO
else
 echo "Aborting build as the distribution name is not defined."
 echo "If you are invoking this script via the command line use:"
 echo "    $0 <distro-name>"
 echo
 echo "If this script is invoked by Kokoro, the CI system is expected to set"
 echo "the KOKORO_JOB_NAME environment variable."
 exit 1
fi

echo "================================================================"
echo "Change working directory to project root $(date)."
cd "$(dirname "$0")/../../.."

echo "================================================================"
echo "Load Google Container Registry configuration parameters $(date)."

if [[ -f "${KOKORO_GFILE_DIR:-}/gcr-configuration.sh" ]]; then
  source "${KOKORO_GFILE_DIR:-}/gcr-configuration.sh"
fi

echo "================================================================"
echo "Setup Google Container Registry access $(date)."
if [[ -f "${KOKORO_GFILE_DIR:-}/gcr-service-account.json" ]]; then
  gcloud auth activate-service-account --key-file \
    "${KOKORO_GFILE_DIR}/gcr-service-account.json"
fi
gcloud auth configure-docker

readonly DEV_IMAGE="gcr.io/${PROJECT_ID}/google-cloud-cpp/test-readme-${DISTRO}"
echo "================================================================"
echo "Download existing image (if available) for ${DISTRO} $(date)."
has_cache="false"
if docker pull "${DEV_IMAGE}:latest"; then
  echo "Existing image successfully downloaded."
  has_cache="true"
fi

echo "================================================================"
echo "Build base image with minimal development tools for ${DISTRO} $(date)."
update_cache="false"

devtools_flags=(
  # Only build up to the stage that installs the minimal development tools, but
  # does not compile any of our code.
  "--target" "devtools"
  # Create the image with the same tag as the cache we are using, so we can
  # upload it.
  "-t" "${DEV_IMAGE}:latest"
  "-f" "ci/kokoro/readme/Dockerfile.${DISTRO}"
)

if "${has_cache}"; then
  devtools_flags+=("--cache-from=${DEV_IMAGE}:latest")
fi

if [[ -z "${KOKORO_GITHUB_PULL_REQUEST_NUMBER:-}" ]]; then
  devtools_flags+=("--no-cache")
fi

echo "Running docker build with " "${devtools_flags[@]}"
if docker build "${devtools_flags[@]}" ci; then
   update_cache="true"
fi

if "${update_cache}" && [[ -z "${KOKORO_GITHUB_PULL_REQUEST_NUMBER:-}" ]]; then
  echo "================================================================"
  echo "Uploading updated base image for ${DISTRO} $(date)."
  # Do not stop the build on a failure to update the cache.
  docker push "${DEV_IMAGE}:latest" || true
fi

echo "================================================================"
echo "Run validation script for README instructions on ${DISTRO}."
docker build \
  "--cache-from=${DEV_IMAGE}:latest" \
  "--target=readme" \
  -f "ci/kokoro/readme/Dockerfile.${DISTRO}" .
echo "================================================================"
