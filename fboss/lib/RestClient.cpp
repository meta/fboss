/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/lib/RestClient.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <sstream>

#include "fboss/agent/FbossError.h"
#include "folly/FileUtil.h"
#include "folly/String.h"
#include "folly/logging/xlog.h"

#include <curl/curl.h>

namespace facebook::fboss {
namespace {
int bindSourceAddress(void* clientp, curl_socket_t fd, curlsocktype purpose) {
  /* CURLSOCKTYPE_ACCEPT is FTP active mode, which CURLOPT_PROTOCOLS below
   * forbids, but do not depend on that staying true. */
  if (purpose != CURLSOCKTYPE_IPCXN) {
    return CURL_SOCKOPT_OK;
  }
  /* Fail closed if the family cannot be determined: skipping here would let
   * the request proceed on a kernel-selected source, which is exactly what
   * the caller asked to prevent. */
  int domain = 0;
  socklen_t domainLen = sizeof(domain);
  if (::getsockopt(fd, SOL_SOCKET, SO_DOMAIN, &domain, &domainLen) != 0) {
    XLOG(ERR) << "Failed to query socket domain, refusing to leave the source "
              << "address unpinned: " << folly::errnoStr(errno);
    return CURL_SOCKOPT_ERROR;
  }
  /* A dual-stack destination can produce an AF_INET socket. Binding a
   * sockaddr_in6 to it fails EAFNOSUPPORT, which would abort the transfer
   * instead of letting curl try the next address. */
  if (domain != AF_INET6) {
    return CURL_SOCKOPT_OK;
  }
  /* curl sets this inside the bindlocal() path we bypass. Without it the
   * ephemeral port is allocated at bind() rather than at connect(). */
  const int on = 1;
  (void)::setsockopt(fd, SOL_IP, IP_BIND_ADDRESS_NO_PORT, &on, sizeof(on));

  const auto* source = static_cast<const folly::IPAddressV6*>(clientp);
  auto addr = source->toSockAddr();
  if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
    XLOG(ERR) << "Failed to bind source " << source->str() << ": "
              << folly::errnoStr(errno);
    return CURL_SOCKOPT_ERROR;
  }
  return CURL_SOCKOPT_OK;
}
} // namespace

RestClient::RestClient(std::string hostname, int port)
    : hostname_(hostname), port_(port) {
  createEndpoint();
}

RestClient::RestClient(folly::IPAddress ipAddress, int port)
    : ipAddress_(ipAddress), port_(port) {
  createEndpoint();
}
RestClient::RestClient(
    folly::IPAddress ipAddress,
    int port,
    std::string interface)
    : ipAddress_(ipAddress), interface_(interface), port_(port) {
  createEndpoint();
}

void RestClient::createEndpoint() {
  if (!hostname_.empty()) {
    endpoint_ = hostname_;
  } else {
    /* check the IP address type */
    if (ipAddress_.isV6()) {
      endpoint_ = "[" + ipAddress_.str() + "]";
    } else {
      endpoint_ = ipAddress_.str();
    }
  }
}

void RestClient::setTimeout(std::chrono::milliseconds timeout) {
  timeout_ = timeout;
}

std::string RestClient::requestWithOutput(
    std::string path,
    std::string postData) {
  CURL* curl;
  CURLcode resp;
  std::stringbuf write_buffer;
  auto endpoint = endpoint_ + path;
  /* for curl errors */
  char error[CURL_ERROR_SIZE];

  /* Reset up front so a throw cannot leave the previous request's status. */
  lastResponseCode_ = std::nullopt;

  curl = curl_easy_init();
  if (curl) {
    /* Set the curl options */
    curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_PORT, port_);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_.count());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, RestClient::writer);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_buffer);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, verifyHostname_ ? 1L : 0L);

    /* CURLOPT_INTERFACE cannot carry an IPv6 zone when given an address, so
     * bind the socket ourselves. Do not set it as well: this callback runs
     * before curl's bindlocal(), which binds the interface's own address
     * whenever SO_BINDTODEVICE is unavailable (it needs CAP_NET_RAW), and
     * that second bind() fails EINVAL. */
    if (sourceAddress_) {
      curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, bindSourceAddress);
      curl_easy_setopt(curl, CURLOPT_SOCKOPTDATA, &sourceAddress_.value());
    } else if (!interface_.empty()) {
      curl_easy_setopt(curl, CURLOPT_INTERFACE, interface_.c_str());
    }

    struct curl_slist* headers = nullptr;
    if (!postData.empty()) {
      curl_easy_setopt(curl, CURLOPT_POST, 1L);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
      headers = curl_slist_append(nullptr, "Content-Type: application/json");
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    if (!cert_.empty() && !key_.empty()) {
      curl_easy_setopt(curl, CURLOPT_SSLCERT, cert_.c_str());
      curl_easy_setopt(curl, CURLOPT_SSLKEY, key_.c_str());
    }

    resp = curl_easy_perform(curl);

    /* Read before cleanup: the handle owns this. Stays 0 when the request
     * never reached a response, which is how a dead peer is told apart from
     * one that answered with an error. */
    long responseCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    lastResponseCode_ =
        responseCode ? std::optional<long>(responseCode) : std::nullopt;

    if (headers) {
      curl_slist_free_all(headers);
    }
    curl_easy_cleanup(curl);
    if (resp == CURLE_OK) {
      return write_buffer.str();
    }
    /* A sockopt-callback failure is reported as CURLE_ABORTED_BY_CALLBACK
     * with an empty error buffer, so name the likely cause instead. */
    if (resp == CURLE_ABORTED_BY_CALLBACK && sourceAddress_) {
      throw FbossError(
          "Error querying api: ",
          endpoint,
          " could not bind source ",
          sourceAddress_->str(),
          ", see preceding log for errno");
    }
    throw FbossError("Error querying api: ", endpoint, " error: ", error);
  }
  throw FbossError("Error initializing curl interface");
}

bool RestClient::request(std::string path) {
  auto ret = requestWithOutput(path);
  std::size_t status = ret.find("done");
  if (status != std::string::npos) {
    return true;
  }
  return false;
}

size_t RestClient::writer(
    char* buffer,
    size_t size,
    size_t entries,
    std::stringbuf* writer_buffer) {
  std::streamsize data_put = writer_buffer->sputn(buffer, size * entries);
  return data_put;
}

void RestClient::setClientCertAndKey(
    std::string_view cert,
    std::string_view key) {
  cert_ = cert;
  key_ = key;
}

void RestClient::setVerifyHostname(bool verify) {
  verifyHostname_ = verify;
}

void RestClient::setSourceAddress(folly::IPAddressV6 source) {
  if (!source.isLinkLocal() || source.getScopeId() == 0) {
    throw FbossError(
        "Source must be a zoned link-local address, e.g. fe80::2%eth0.4088: ",
        source.str());
  }
  if (hostname_.empty() && !ipAddress_.isV6()) {
    throw FbossError(
        "An IPv6 source needs an IPv6 destination: ", ipAddress_.str());
  }
  sourceAddress_ = std::move(source);
}

} // namespace facebook::fboss
