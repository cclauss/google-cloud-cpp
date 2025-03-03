// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "google/cloud/internal/build_info.h"
#include "google/cloud/internal/format_time_point.h"
#include "google/cloud/internal/getenv.h"
#include "google/cloud/internal/random.h"
#include "google/cloud/internal/throw_delegate.h"
#include "google/cloud/storage/benchmarks/benchmark_utils.h"
#include "google/cloud/storage/client.h"
#include <fstream>
#include <future>
#include <iomanip>
#include <sstream>

namespace {
namespace gcs = google::cloud::storage;
namespace gcs_bm = google::cloud::storage_benchmarks;

char const kDescription[] = R"""(
A thoughput benchmark for the Google Cloud Storage C++ client library.

This program benchmarks the Google Cloud Storage (GCS) C++ client library when
used to upload and download files. The program creates a file of a prescribed
size, and then repeatedly uploads that file to a GCS object, and then downloads
the GCS object to a separate file. The program reports the time taken to perform
the each operation, as well as the effective bandwidth (in Gbps and MiB/s). The
program deletes the target GCS object after each iteration.

To perform this benchmark the program creates a new regional bucket, in a region
configured via the command line. Other test parameters, such as the project id,
the file size, and the buffer sizes are configurable via the command line too.

The bucket name, the local file names, and the object names are all randomly
generated, so multiple instances of the program can run simultaneously. The
output of this program is an annotated CSV file, that can be analyzed by an
external script. The annotation lines start with a '#', analysis scripts should
skip these lines.
)""";

struct Options {
  std::string project_id;
  std::string region;
  std::chrono::seconds duration = std::chrono::seconds(60);
  std::int64_t file_size = 100 * gcs_bm::kMiB;
  std::int64_t download_buffer_size = 16 * gcs_bm::kMiB;
  std::int64_t upload_buffer_size = 16 * gcs_bm::kMiB;
};

Options ParseArgs(int& argc, char* argv[]);

}  // namespace

int main(int argc, char* argv[]) try {
  Options options = ParseArgs(argc, argv);

  google::cloud::StatusOr<gcs::ClientOptions> client_options =
      gcs::ClientOptions::CreateDefaultClientOptions();
  if (!client_options) {
    std::cerr << "Could not create ClientOptions, status="
              << client_options.status() << "\n";
    return 1;
  }
  client_options->SetUploadBufferSize(options.upload_buffer_size);
  client_options->SetDownloadBufferSize(options.download_buffer_size);
  client_options->set_project_id(options.project_id);

  gcs::Client client(*std::move(client_options));

  google::cloud::internal::DefaultPRNG generator =
      google::cloud::internal::MakeDefaultPRNG();

  auto bucket_name =
      gcs_bm::MakeRandomBucketName(generator, "gcs-file-transfer-");
  auto meta =
      client
          .CreateBucket(bucket_name,
                        gcs::BucketMetadata()
                            .set_storage_class(gcs::storage_class::Regional())
                            .set_location(options.region),
                        gcs::PredefinedAcl("private"),
                        gcs::PredefinedDefaultObjectAcl("projectPrivate"),
                        gcs::Projection("full"))
          .value();
  std::cout << "# Running test on bucket: " << meta.name() << "\n";
  std::string notes = google::cloud::storage::version_string() + ";" +
                      google::cloud::internal::compiler() + ";" +
                      google::cloud::internal::compiler_flags();
  std::transform(notes.begin(), notes.end(), notes.begin(),
                 [](char c) { return c == '\n' ? ';' : c; });
  std::cout << "# Start time: "
            << google::cloud::internal::FormatRfc3339(
                   std::chrono::system_clock::now())
            << "\n# Region: " << options.region
            << "\n# Duration: " << options.duration.count() << "s"
            << "\n# File Size: " << options.file_size
            << "\n# File Size (MiB): " << options.file_size / gcs_bm::kMiB
            << "\n# Download buffer size (KiB): "
            << options.download_buffer_size / gcs_bm::kKiB
            << "\n# Upload buffer size (KiB): "
            << options.upload_buffer_size / gcs_bm::kKiB
            << "\n# Build info: " << notes << "\n";

  std::cout << "# Creating file to upload ..." << std::flush;
  auto filename = gcs_bm::MakeRandomFileName(generator);
  std::ofstream(filename, std::ios::binary)
      << gcs_bm::MakeRandomData(generator, options.file_size);
  std::cout << " DONE\n"
            << "# File: " << filename << "\n";

  auto deadline = std::chrono::system_clock::now() + options.duration;
  for (auto now = std::chrono::system_clock::now(); now < deadline;
       now = std::chrono::system_clock::now()) {
    auto object_name = gcs_bm::MakeRandomObjectName(generator);

    auto upload_start = std::chrono::steady_clock::now();
    auto object_metadata =
        client.UploadFile(filename, bucket_name, object_name);
    auto upload_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - upload_start);
    double gbps = options.file_size * 8.0 / upload_elapsed.count();
    auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(upload_elapsed);
    auto MiBs =
        (double(options.file_size) / gcs_bm::kMiB) / (ms.count() / 1000.0);
    std::cout << "FileUpload," << options.file_size << ','
              << upload_elapsed.count() << ',' << gbps << ',' << ms.count()
              << ',' << MiBs << ',' << object_metadata.status().code() << "\n";
    if (!object_metadata) {
      std::cout << "# Error in FileUpload: " << object_metadata.status()
                << "\n";
      continue;
    }

    auto destination_filename = gcs_bm::MakeRandomFileName(generator);
    auto download_start = std::chrono::steady_clock::now();
    client.DownloadToFile(object_metadata->bucket(), object_metadata->name(),
                          destination_filename);
    auto download_elapsed =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - download_start);
    gbps = options.file_size * 8.0 / download_elapsed.count();
    ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(download_elapsed);
    MiBs = (double(options.file_size) / gcs_bm::kMiB) / (ms.count() / 1000.0);
    std::cout << "FileDownload," << options.file_size << ','
              << download_elapsed.count() << ',' << gbps << ',' << ms.count()
              << ',' << MiBs << ',' << object_metadata.status().code() << "\n";

    (void)client.DeleteObject(object_metadata->bucket(),
                              object_metadata->name(),
                              gcs::Generation(object_metadata->generation()));
    std::remove(destination_filename.c_str());
  }

  std::remove(filename.c_str());

  std::cout << "# Deleting " << bucket_name << "\n";
  auto status = client.DeleteBucket(bucket_name);
  if (!status.ok()) {
    google::cloud::internal::ThrowStatus(status);
  }

  return 0;
} catch (std::exception const& ex) {
  std::cerr << "Standard exception raised: " << ex.what() << "\n";
  return 1;
}

namespace {

Options ParseArgs(int& argc, char* argv[]) {
  Options options;

  bool wants_help = false;
  bool wants_description = false;
  std::vector<gcs_bm::OptionDescriptor> descriptors{
      {"--help", "print the usage message",
       [&wants_help](std::string const&) { wants_help = true; }},
      {"--description", "print a description of the benchmark",
       [&wants_description](std::string const&) { wants_description = true; }},
      {"--project-id", "the GCP project to create the bucket",
       [&options](std::string const& val) { options.project_id = val; }},
      {"--duration", "how long should the benchmark run (in seconds).",
       [&options](std::string const& val) {
         options.duration = gcs_bm::ParseDuration(val);
       }},
      {"--file-size", "the size of the file to upload",
       [&options](std::string const& val) {
         options.file_size = gcs_bm::ParseSize(val);
       }},
      {"--upload-buffer-size", "configure gcs::Client upload buffer size",
       [&options](std::string const& val) {
         options.upload_buffer_size = gcs_bm::ParseSize(val);
       }},
      {"--download-buffer-size", "configure gcs::Client download buffer size",
       [&options](std::string const& val) {
         options.download_buffer_size = gcs_bm::ParseSize(val);
       }},
      {"--region", "The GCS region used for the benchmark",
       [&options](std::string const& val) { options.region = val; }},
  };
  auto usage = gcs_bm::BuildUsage(descriptors, argv[0]);

  auto unparsed = gcs_bm::OptionsParse(descriptors, {argv, argv + argc});
  if (wants_help) {
    std::cout << usage << "\n";
  }

  if (wants_description) {
    std::cout << kDescription << "\n";
  }

  if (unparsed.size() > 2) {
    std::ostringstream os;
    os << "Unknown arguments or options\n" << usage << "\n";
    throw std::runtime_error(std::move(os).str());
  }
  if (unparsed.size() == 2) {
    options.region = unparsed[1];
  }
  if (options.region.empty()) {
    std::ostringstream os;
    os << "Missing value for --region option" << usage << "\n";
    throw std::runtime_error(std::move(os).str());
  }

  return options;
}

}  // namespace
