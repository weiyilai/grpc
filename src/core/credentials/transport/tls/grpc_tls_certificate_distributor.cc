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

#include "src/core/credentials/transport/tls/grpc_tls_certificate_distributor.h"

#include <grpc/credentials.h>
#include <grpc/grpc_security.h>
#include <grpc/support/port_platform.h>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "src/core/credentials/transport/tls/spiffe_utils.h"
#include "src/core/tsi/ssl_transport_security.h"

bool grpc_tls_certificate_distributor::CertificateInfo::AreRootsEmpty() {
  return IsRootCertInfoEmpty(roots.get());
}

void grpc_tls_certificate_distributor::SetKeyMaterials(
    const std::string& cert_name, std::shared_ptr<RootCertInfo> roots,
    std::optional<grpc_core::PemKeyCertPairList> pem_key_cert_pairs) {
  CHECK(roots != nullptr || pem_key_cert_pairs.has_value());
  grpc_core::MutexLock lock(&mu_);
  auto& cert_info = certificate_info_map_[cert_name];
  if (roots != nullptr) {
    // Successful credential updates will clear any pre-existing error.
    cert_info.SetRootError(absl::OkStatus());
    for (auto* watcher_ptr : cert_info.root_cert_watchers) {
      CHECK_NE(watcher_ptr, nullptr);
      const auto watcher_it = watchers_.find(watcher_ptr);
      CHECK(watcher_it != watchers_.end());
      CHECK(watcher_it->second.root_cert_name.has_value());
      std::optional<grpc_core::PemKeyCertPairList> pem_key_cert_pairs_to_report;
      if (pem_key_cert_pairs.has_value() &&
          watcher_it->second.identity_cert_name == cert_name) {
        pem_key_cert_pairs_to_report = pem_key_cert_pairs;
      } else if (watcher_it->second.identity_cert_name.has_value()) {
        auto& identity_cert_info =
            certificate_info_map_[*watcher_it->second.identity_cert_name];
        if (!identity_cert_info.pem_key_cert_pairs.empty()) {
          pem_key_cert_pairs_to_report = identity_cert_info.pem_key_cert_pairs;
        }
      }
      watcher_ptr->OnCertificatesChanged(
          roots, std::move(pem_key_cert_pairs_to_report));
    }
    cert_info.roots = roots;
  }
  if (pem_key_cert_pairs.has_value()) {
    // Successful credential updates will clear any pre-existing error.
    cert_info.SetIdentityError(absl::OkStatus());
    for (const auto watcher_ptr : cert_info.identity_cert_watchers) {
      CHECK_NE(watcher_ptr, nullptr);
      const auto watcher_it = watchers_.find(watcher_ptr);
      CHECK(watcher_it != watchers_.end());
      CHECK(watcher_it->second.identity_cert_name.has_value());
      std::shared_ptr<RootCertInfo> roots_to_report;
      if (roots != nullptr && watcher_it->second.root_cert_name == cert_name) {
        // In this case, We've already sent the credential updates at the time
        // when checking pem_root_certs, so we will skip here.
        continue;
      } else if (watcher_it->second.root_cert_name.has_value()) {
        auto& root_cert_info =
            certificate_info_map_[*watcher_it->second.root_cert_name];
        if (!root_cert_info.AreRootsEmpty()) {
          roots_to_report = root_cert_info.roots;
        }
      }
      watcher_ptr->OnCertificatesChanged(std::move(roots_to_report),
                                         pem_key_cert_pairs);
    }
    cert_info.pem_key_cert_pairs = std::move(*pem_key_cert_pairs);
  }
}

bool grpc_tls_certificate_distributor::HasRootCerts(
    const std::string& root_cert_name) {
  grpc_core::MutexLock lock(&mu_);
  const auto it = certificate_info_map_.find(root_cert_name);
  return it != certificate_info_map_.end() && !it->second.AreRootsEmpty();
};

bool grpc_tls_certificate_distributor::HasKeyCertPairs(
    const std::string& identity_cert_name) {
  grpc_core::MutexLock lock(&mu_);
  const auto it = certificate_info_map_.find(identity_cert_name);
  return it != certificate_info_map_.end() &&
         !it->second.pem_key_cert_pairs.empty();
};

void grpc_tls_certificate_distributor::SetErrorForCert(
    const std::string& cert_name,
    std::optional<grpc_error_handle> root_cert_error,
    std::optional<grpc_error_handle> identity_cert_error) {
  CHECK(root_cert_error.has_value() || identity_cert_error.has_value());
  grpc_core::MutexLock lock(&mu_);
  CertificateInfo& cert_info = certificate_info_map_[cert_name];
  if (root_cert_error.has_value()) {
    for (auto* watcher_ptr : cert_info.root_cert_watchers) {
      CHECK_NE(watcher_ptr, nullptr);
      const auto watcher_it = watchers_.find(watcher_ptr);
      CHECK(watcher_it != watchers_.end());
      // identity_cert_error_to_report is the error of the identity cert this
      // watcher is watching, if there is any.
      grpc_error_handle identity_cert_error_to_report;
      if (identity_cert_error.has_value() &&
          watcher_it->second.identity_cert_name == cert_name) {
        identity_cert_error_to_report = *identity_cert_error;
      } else if (watcher_it->second.identity_cert_name.has_value()) {
        auto& identity_cert_info =
            certificate_info_map_[*watcher_it->second.identity_cert_name];
        identity_cert_error_to_report = identity_cert_info.identity_cert_error;
      }
      watcher_ptr->OnError(*root_cert_error, identity_cert_error_to_report);
    }
    cert_info.SetRootError(*root_cert_error);
  }
  if (identity_cert_error.has_value()) {
    for (auto* watcher_ptr : cert_info.identity_cert_watchers) {
      CHECK_NE(watcher_ptr, nullptr);
      const auto watcher_it = watchers_.find(watcher_ptr);
      CHECK(watcher_it != watchers_.end());
      // root_error_to_report is the error of the roots this watcher
      // is watching, if there is any.
      grpc_error_handle root_error_to_report;
      if (root_cert_error.has_value() &&
          watcher_it->second.root_cert_name == cert_name) {
        // In this case, We've already sent the error updates at the time when
        // checking root_cert_error, so we will skip here.
        continue;
      } else if (watcher_it->second.root_cert_name.has_value()) {
        auto& root_cert_info =
            certificate_info_map_[*watcher_it->second.root_cert_name];
        root_error_to_report = root_cert_info.root_cert_error;
      }
      watcher_ptr->OnError(root_error_to_report, *identity_cert_error);
    }
    cert_info.SetIdentityError(*identity_cert_error);
  }
};

void grpc_tls_certificate_distributor::SetError(grpc_error_handle error) {
  CHECK(!error.ok());
  grpc_core::MutexLock lock(&mu_);
  for (const auto& watcher : watchers_) {
    const auto watcher_ptr = watcher.first;
    CHECK_NE(watcher_ptr, nullptr);
    const auto& watcher_info = watcher.second;
    watcher_ptr->OnError(
        watcher_info.root_cert_name.has_value() ? error : absl::OkStatus(),
        watcher_info.identity_cert_name.has_value() ? error : absl::OkStatus());
  }
  for (auto& cert_info_entry : certificate_info_map_) {
    auto& cert_info = cert_info_entry.second;
    cert_info.SetRootError(error);
    cert_info.SetIdentityError(error);
  }
};

void grpc_tls_certificate_distributor::WatchTlsCertificates(
    std::unique_ptr<TlsCertificatesWatcherInterface> watcher,
    std::optional<std::string> root_cert_name,
    std::optional<std::string> identity_cert_name) {
  bool start_watching_root_cert = false;
  bool already_watching_identity_for_root_cert = false;
  bool start_watching_identity_cert = false;
  bool already_watching_root_for_identity_cert = false;
  CHECK(root_cert_name.has_value() || identity_cert_name.has_value());
  TlsCertificatesWatcherInterface* watcher_ptr = watcher.get();
  CHECK_NE(watcher_ptr, nullptr);
  // Update watchers_ and certificate_info_map_.
  {
    grpc_core::MutexLock lock(&mu_);
    const auto watcher_it = watchers_.find(watcher_ptr);
    // The caller needs to cancel the watcher first if it wants to re-register
    // the watcher.
    CHECK(watcher_it == watchers_.end());
    watchers_[watcher_ptr] = {std::move(watcher), root_cert_name,
                              identity_cert_name};
    std::shared_ptr<RootCertInfo> updated_roots;
    std::optional<grpc_core::PemKeyCertPairList> updated_identity_pairs;
    grpc_error_handle root_error;
    grpc_error_handle identity_error;
    if (root_cert_name.has_value()) {
      CertificateInfo& cert_info = certificate_info_map_[*root_cert_name];
      start_watching_root_cert = cert_info.root_cert_watchers.empty();
      already_watching_identity_for_root_cert =
          !cert_info.identity_cert_watchers.empty();
      cert_info.root_cert_watchers.insert(watcher_ptr);
      root_error = cert_info.root_cert_error;
      // Empty credentials will be treated as no updates.
      if (!cert_info.AreRootsEmpty()) {
        updated_roots = cert_info.roots;
      }
    }
    if (identity_cert_name.has_value()) {
      CertificateInfo& cert_info = certificate_info_map_[*identity_cert_name];
      start_watching_identity_cert = cert_info.identity_cert_watchers.empty();
      already_watching_root_for_identity_cert =
          !cert_info.root_cert_watchers.empty();
      cert_info.identity_cert_watchers.insert(watcher_ptr);
      identity_error = cert_info.identity_cert_error;
      // Empty credentials will be treated as no updates.
      if (!cert_info.pem_key_cert_pairs.empty()) {
        updated_identity_pairs = cert_info.pem_key_cert_pairs;
      }
    }
    // Notify this watcher if the certs it is watching already had some
    // contents. Note that an *_cert_error in cert_info only indicates error
    // occurred while trying to fetch the latest cert, but the updated_*_certs
    // should always be valid. So we will send the updates regardless of
    // *_cert_error.
    if (updated_roots != nullptr || updated_identity_pairs.has_value()) {
      watcher_ptr->OnCertificatesChanged(updated_roots,
                                         std::move(updated_identity_pairs));
    }
    // Notify this watcher if the certs it is watching already had some
    // errors.
    if (!root_error.ok() || !identity_error.ok()) {
      watcher_ptr->OnError(root_error, identity_error);
    }
  }
  // Invoke watch status callback if needed.
  {
    grpc_core::MutexLock lock(&callback_mu_);
    if (watch_status_callback_ != nullptr) {
      if (root_cert_name == identity_cert_name &&
          (start_watching_root_cert || start_watching_identity_cert)) {
        watch_status_callback_(*root_cert_name, start_watching_root_cert,
                               start_watching_identity_cert);
      } else {
        if (start_watching_root_cert) {
          watch_status_callback_(*root_cert_name, true,
                                 already_watching_identity_for_root_cert);
        }
        if (start_watching_identity_cert) {
          watch_status_callback_(*identity_cert_name,
                                 already_watching_root_for_identity_cert, true);
        }
      }
    }
  }
};

void grpc_tls_certificate_distributor::CancelTlsCertificatesWatch(
    TlsCertificatesWatcherInterface* watcher) {
  std::optional<std::string> root_cert_name;
  std::optional<std::string> identity_cert_name;
  bool stop_watching_root_cert = false;
  bool already_watching_identity_for_root_cert = false;
  bool stop_watching_identity_cert = false;
  bool already_watching_root_for_identity_cert = false;
  // Update watchers_ and certificate_info_map_.
  {
    grpc_core::MutexLock lock(&mu_);
    auto it = watchers_.find(watcher);
    if (it == watchers_.end()) return;
    WatcherInfo& watcher_info = it->second;
    root_cert_name = std::move(watcher_info.root_cert_name);
    identity_cert_name = std::move(watcher_info.identity_cert_name);
    watchers_.erase(it);
    if (root_cert_name.has_value()) {
      auto it = certificate_info_map_.find(*root_cert_name);
      CHECK(it != certificate_info_map_.end());
      CertificateInfo& cert_info = it->second;
      cert_info.root_cert_watchers.erase(watcher);
      stop_watching_root_cert = cert_info.root_cert_watchers.empty();
      already_watching_identity_for_root_cert =
          !cert_info.identity_cert_watchers.empty();
      if (stop_watching_root_cert && !already_watching_identity_for_root_cert) {
        certificate_info_map_.erase(it);
      }
    }
    if (identity_cert_name.has_value()) {
      auto it = certificate_info_map_.find(*identity_cert_name);
      CHECK(it != certificate_info_map_.end());
      CertificateInfo& cert_info = it->second;
      cert_info.identity_cert_watchers.erase(watcher);
      stop_watching_identity_cert = cert_info.identity_cert_watchers.empty();
      already_watching_root_for_identity_cert =
          !cert_info.root_cert_watchers.empty();
      if (stop_watching_identity_cert &&
          !already_watching_root_for_identity_cert) {
        certificate_info_map_.erase(it);
      }
    }
  }
  // Invoke watch status callback if needed.
  {
    grpc_core::MutexLock lock(&callback_mu_);
    if (watch_status_callback_ != nullptr) {
      if (root_cert_name == identity_cert_name &&
          (stop_watching_root_cert || stop_watching_identity_cert)) {
        watch_status_callback_(*root_cert_name, !stop_watching_root_cert,
                               !stop_watching_identity_cert);
      } else {
        if (stop_watching_root_cert) {
          watch_status_callback_(*root_cert_name, false,
                                 already_watching_identity_for_root_cert);
        }
        if (stop_watching_identity_cert) {
          watch_status_callback_(*identity_cert_name,
                                 already_watching_root_for_identity_cert,
                                 false);
        }
      }
    }
  }
};

/// -- Wrapper APIs declared in grpc_security.h -- *

grpc_tls_identity_pairs* grpc_tls_identity_pairs_create() {
  return new grpc_tls_identity_pairs();
}

void grpc_tls_identity_pairs_add_pair(grpc_tls_identity_pairs* pairs,
                                      const char* private_key,
                                      const char* cert_chain) {
  CHECK_NE(pairs, nullptr);
  CHECK_NE(private_key, nullptr);
  CHECK_NE(cert_chain, nullptr);
  pairs->pem_key_cert_pairs.emplace_back(private_key, cert_chain);
}

void grpc_tls_identity_pairs_destroy(grpc_tls_identity_pairs* pairs) {
  CHECK_NE(pairs, nullptr);
  delete pairs;
}
