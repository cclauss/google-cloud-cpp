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

#include "google/cloud/storage/oauth2/service_account_credentials.h"
#include "google/cloud/storage/internal/openssl_util.h"
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h>
#include <fstream>

namespace google {
namespace cloud {
namespace storage {
inline namespace STORAGE_CLIENT_NS {
namespace oauth2 {

StatusOr<ServiceAccountCredentialsInfo> ParseServiceAccountCredentials(
    std::string const& content, std::string const& source,
    std::string const& default_token_uri) {
  namespace nl = storage::internal::nl;
  nl::json credentials = nl::json::parse(content, nullptr, false);
  if (credentials.is_discarded()) {
    return Status(StatusCode::kInvalidArgument,
                  "Invalid ServiceAccountCredentials,"
                  "parsing failed on data loaded from " +
                      source);
  }
  char const private_key_id_key[] = "private_key_id";
  char const private_key_key[] = "private_key";
  char const token_uri_key[] = "token_uri";
  char const client_email_key[] = "client_email";
  for (auto const& key :
       {private_key_id_key, private_key_key, client_email_key}) {
    if (credentials.count(key) == 0) {
      return Status(StatusCode::kInvalidArgument,
                    "Invalid ServiceAccountCredentials, the " +
                        std::string(key) +
                        " field is missing on data loaded from " + source);
    }
    if (credentials.value(key, "").empty()) {
      return Status(StatusCode::kInvalidArgument,
                    "Invalid ServiceAccountCredentials, the " +
                        std::string(key) +
                        " field is empty on data loaded from " + source);
    }
  }
  // The token_uri field may be missing, but may not be empty:
  if (credentials.count(token_uri_key) != 0 &&
      credentials.value(token_uri_key, "").empty()) {
    return Status(StatusCode::kInvalidArgument,
                  "Invalid ServiceAccountCredentials, the " +
                      std::string(token_uri_key) +
                      " field is empty on data loaded from " + source);
  }
  return ServiceAccountCredentialsInfo{
      credentials.value(client_email_key, ""),
      credentials.value(private_key_id_key, ""),
      credentials.value(private_key_key, ""),
      // Some credential formats (e.g. gcloud's ADC file) don't contain a
      // "token_uri" attribute in the JSON object.  In this case, we try using
      // the default value.
      credentials.value(token_uri_key, default_token_uri),
      /*scopes*/ {},
      /*subject*/ {}};
}

StatusOr<ServiceAccountCredentialsInfo> ParseServiceAccountP12File(
    std::string const& source, std::string const& default_token_uri) {
  OpenSSL_add_all_algorithms();

  PKCS12* p12_raw = [](std::string const& source) {
    FILE* fp = std::fopen(source.c_str(), "rb");
    if (fp == nullptr) {
      return static_cast<PKCS12*>(nullptr);
    }
    auto result = d2i_PKCS12_fp(fp, nullptr);
    fclose(fp);
    return result;
  }(source);

  std::unique_ptr<PKCS12, decltype(&PKCS12_free)> p12(p12_raw, &PKCS12_free);

  auto capture_openssl_errors = []() {
    std::string msg;
    while (auto code = ERR_get_error()) {
      // OpenSSL guarantees that 256 bytes is enough:
      //   https://www.openssl.org/docs/man1.1.1/man3/ERR_error_string_n.html
      //   https://www.openssl.org/docs/man1.0.2/man3/ERR_error_string_n.html
      // we could not find a macro or constant to replace the 256 literal.
      char buf[256];
      ERR_error_string_n(code, buf, sizeof(buf));
      msg += buf;
    }
    return msg;
  };

  if (p12 == nullptr) {
    std::string msg = "Cannot open PKCS#12 file (" + source + "): ";
    msg += capture_openssl_errors();
    return Status(StatusCode::kInvalidArgument, msg);
  }

  EVP_PKEY* pkey_raw;
  X509* cert_raw;
  if (PKCS12_parse(p12.get(), "notasecret", &pkey_raw, &cert_raw, nullptr) !=
      1) {
    std::string msg = "Cannot parse PKCS#12 file (" + source + "): ";
    msg += capture_openssl_errors();
    return Status(StatusCode::kInvalidArgument, msg);
  }

  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> pkey(pkey_raw,
                                                           &EVP_PKEY_free);
  std::unique_ptr<X509, decltype(&X509_free)> cert(cert_raw, &X509_free);

  if (pkey_raw == nullptr) {
    return Status(StatusCode::kInvalidArgument,
                  "No private key found in PKCS#12 file (" + source + ")");
  }
  if (cert_raw == nullptr) {
    return Status(StatusCode::kInvalidArgument,
                  "No private key found in PKCS#12 file (" + source + ")");
  }

  // This is automatically deleted by `cert`.
  X509_NAME* name = X509_get_subject_name(cert.get());

  std::string service_account_id = [&name]() -> std::string {
    auto openssl_free = [](void* addr) { OPENSSL_free(addr); };
    std::unique_ptr<char, decltype(openssl_free)> oneline(
        X509_NAME_oneline(name, nullptr, 0), openssl_free);
    // We expect the name to be simply CN/ followed by a (small) number of
    // digits.
    if (strncmp("/CN=", oneline.get(), 4) != 0) {
      return "";
    }
    return oneline.get() + 4;
  }();

  if (service_account_id.find_first_not_of("0123456789") != std::string::npos ||
      service_account_id.empty()) {
    return Status(
        StatusCode::kInvalidArgument,
        "Invalid PKCS#12 file (" + source +
            "): service account id missing or not not formatted correctly");
  }

  std::unique_ptr<BIO, decltype(&BIO_free)> mem_io(BIO_new(BIO_s_mem()),
                                                   &BIO_free);

  if (PEM_write_bio_PKCS8PrivateKey(mem_io.get(), pkey.get(), nullptr, nullptr,
                                    0, nullptr, nullptr) == 0) {
    std::string msg =
        "Cannot print private key in PKCS#12 file (" + source + "): ";
    msg += capture_openssl_errors();
    return Status(StatusCode::kUnknown, msg);
  }

  // This buffer belongs to the BIO chain and is freed upon its destruction.
  BUF_MEM* buf_mem;
  BIO_get_mem_ptr(mem_io.get(), &buf_mem);

  std::string private_key(buf_mem->data, buf_mem->length);

  return ServiceAccountCredentialsInfo{std::move(service_account_id),
                                       "--unknown--",
                                       std::move(private_key),
                                       default_token_uri,
                                       /*scopes*/ {},
                                       /*subject*/ {}};
}

}  // namespace oauth2
}  // namespace STORAGE_CLIENT_NS
}  // namespace storage
}  // namespace cloud
}  // namespace google
