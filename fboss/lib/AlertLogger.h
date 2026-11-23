/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <fmt/format.h>
#include <folly/logging/xlog.h>

#include <ostream>
#include <string>

namespace facebook::fboss {

/*
 * Utilities for logging of hi-signal events
 * with event-type and additional metadata params over XLOG
 *
 * example invocations:
 *  XLOG(ERR) << PlatformAlert() << logMsg;
 *
 *  XLOG(INFO) << PortAlert() << logMsg <<  PortParam(port->getName());
 *
 * example outputs:
 *  ... FBOSS_ALERT(PLATFORM): Exception talking to qsfp_ service: Channel is
 * !good()
 *  ... FBOSS_ALERT(SERVICE): SwitchRunState changed from UNINITIALIZED to
 * INITIALIZED
 *  ... FBOSS_ALERT(PORT): LinkState: Port eth1/31/1 Up <port:eth1/31/1>
 */

// Alert tag with type
struct AlertTag {
  explicit AlertTag(std::string prefix, std::string sub_type = "");

  friend std::ostream& operator<<(std::ostream& out, const AlertTag& tag) {
    // Note: outputs a blank space at end
    if (!tag.sub_type_.empty()) {
      return (out << tag.prefix_ << "(" << tag.sub_type_ << "): ");
    } else {
      return (out << tag.prefix_ << ": ");
    }
  }

  const std::string str(void) const {
    // Note: outputs a blank space at end
    if (!sub_type_.empty()) {
      return fmt::format("{}({}): ", prefix_, sub_type_);
    } else {
      return fmt::format("{}: ", prefix_);
    }
  }

  const std::string prefix_;
  const std::string sub_type_;
};

class MiscAlert : public AlertTag {
 public:
  MiscAlert();
};
class AsicAlert : public AlertTag {
 public:
  AsicAlert();
};
class ServiceAlert : public AlertTag {
 public:
  ServiceAlert();
};
class PlatformAlert : public AlertTag {
 public:
  PlatformAlert();
};
class BmcAlert : public AlertTag {
 public:
  BmcAlert();
};
class KernelAlert : public AlertTag {
 public:
  KernelAlert();
};
class PortAlert : public AlertTag {
 public:
  PortAlert();
};
class RouteAlert : public AlertTag {
 public:
  RouteAlert();
};
class BGPAlert : public AlertTag {
 public:
  BGPAlert();
};
class MKAAlert : public AlertTag {
 public:
  MKAAlert();
};
class LinkSnapshotAlert : public AlertTag {
 public:
  LinkSnapshotAlert();
};
class TransceiverValidationAlert : public AlertTag {
 public:
  TransceiverValidationAlert();
};
class LinkAlert : public AlertTag {
 public:
  LinkAlert();
};
class FirmwareUpgradeAlert : public AlertTag {
 public:
  FirmwareUpgradeAlert();
};

// Alert parameter types
struct AlertParam {
  explicit AlertParam(std::string type, std::string value);

  friend std::ostream& operator<<(std::ostream& out, const AlertParam& param) {
    // Note: outputs a blank space at front
    return (out << " <" << param.type_ << ":" << param.value_ << ">");
  }

  const std::string str() const {
    // Note: outputs a blank space at front
    return fmt::format(" <{}:{}>", type_, value_);
  }

  const std::string type_;
  const std::string value_;
};

class PortParam : public AlertParam {
 public:
  explicit PortParam(std::string value);
};
class TransceiverParam : public AlertParam {
 public:
  explicit TransceiverParam(int value);
};
class VlanParam : public AlertParam {
 public:
  explicit VlanParam(std::string value);
};
class Ipv4Param : public AlertParam {
 public:
  explicit Ipv4Param(std::string value);
};
class Ipv6Param : public AlertParam {
 public:
  explicit Ipv6Param(std::string value);
};
class MacParam : public AlertParam {
 public:
  explicit MacParam(std::string value);
};
class LinkSnapshotParam : public AlertParam {
 public:
  explicit LinkSnapshotParam(std::string value);
};

} // namespace facebook::fboss
