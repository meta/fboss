/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/TunIntf.h"

#include "fboss/agent/AgentFeatures.h"

extern "C" {
#include <fcntl.h>
#include <libnetlink.h>
#include <linux/if.h>
#include <linux/if_link.h>
#include <linux/if_tun.h>
#include <linux/rtnetlink.h>
#include <netlink/route/link.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
}

#include <folly/io/async/EventBase.h>
#include <folly/io/async/EventHandler.h>
#include <folly/logging/xlog.h>
#include "fboss/agent/NlError.h"
#include "fboss/agent/RxPacket.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SysError.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/packet/EthHdr.h"
namespace facebook::fboss {

namespace {

const std::string kTunDev = "/dev/net/tun";

// Max packets to be processed which are received from host
const int kMaxSentOneTime = 16;

// Definition of `iplink_req` as it is not well defined in any header files
struct iplink_req {
  struct nlmsghdr n;
  struct ifinfomsg i;
  char buf[1024];
};

/**
 * Some flags not available in current kernel includes. These has been made
 * available since kernel-3.17
 * This should go away when recent kernel header includes are available
 */
#ifndef IFLA_INET6_ADDR_GEN_MODE
#define IFLA_INET6_ADDR_GEN_MODE 8
#endif
#ifndef IN6_ADDR_GEN_MODE_NONE
#define IN6_ADDR_GEN_MODE_NONE 1
#endif

} // anonymous namespace

TunIntf::TunIntf(
    SwSwitch* sw,
    folly::EventBase* evb,
    InterfaceID ifID,
    cfg::InterfaceType type,
    int ifIndex,
    int mtu)
    : folly::EventHandler(evb),
      sw_(sw),
      name_(utility::createTunIntfName(ifID)),
      ifID_(ifID),
      type_(type),
      ifIndex_(ifIndex),
      mtu_(mtu) {
  DCHECK(sw) << "NULL pointer to SwSwitch.";
  DCHECK(evb) << "NULL pointer to EventBase";

  openFD();
  SCOPE_FAIL {
    closeFD();
  };

  // XXX: Disabling mode on existing interface so that we end up removing
  // automatically allocated v6 link local address on next release. from
  // next release onwards we will not need it
  disableIPv6AddrGenMode(ifIndex_);

  XLOG(DBG2) << "Added interface " << name_ << " with fd " << fd_ << " @ index "
             << ifIndex_ << ", " << "DOWN";
}

TunIntf::TunIntf(
    SwSwitch* sw,
    folly::EventBase* evb,
    InterfaceID ifID,
    cfg::InterfaceType type,
    bool status,
    const Interface::Addresses& addr,
    int mtu)
    : folly::EventHandler(evb),
      sw_(sw),
      name_(utility::createTunIntfName(ifID)),
      ifID_(ifID),
      type_(type),
      status_(status),
      addrs_(addr),
      mtu_(mtu) {
  DCHECK(sw) << "NULL pointer to SwSwitch.";
  DCHECK(evb) << "NULL pointer to EventBase";

  // Open Tun interface FD for socket-IO
  openFD();
  SCOPE_FAIL {
    closeFD();
  };

  // Make the Tun interface persistent, so that the network sessions from the
  // application (i.e. BGP)  will not be reset if controller restarts
  auto ret = ioctl(fd_, TUNSETPERSIST, 1);
  sysCheckError(ret, "Failed to set persist interface ", name_);

  // TODO: if needed, we can adjust send buffer size, TUNSETSNDBUF
  auto sock = nl_socket_alloc();
  if (!sock) {
    throw SysError(errno, "failed to open libnl socket");
  }
  SCOPE_EXIT {
    nl_socket_free(sock);
  };

  // Connect netlink socket.
  ret = nl_connect(sock, NETLINK_ROUTE);
  sysCheckError(ret, "failed to connect", nl_geterror(ret));
  SCOPE_EXIT {
    nl_close(sock);
  };

  // Allocate cache
  nl_cache* cache = nullptr;
  ret = rtnl_link_alloc_cache(sock, AF_UNSPEC, &cache);
  sysCheckError(ret, "failed to get all links: error: ", nl_geterror(ret));
  SCOPE_EXIT {
    nl_cache_free(cache);
  };

  // Extract ifIndex
  ifIndex_ = rtnl_link_name2i(cache, name_.c_str());
  if (ifIndex_ <= 0) {
    FbossError("Got invalid value ", ifIndex_, " for Tun interface ", name_);
  }

  // Disable v6 link-local address assignment on Tun interface
  disableIPv6AddrGenMode(ifIndex_);

  XLOG(DBG2) << "Created interface " << name_ << " with fd " << fd_
             << " @ index " << ifIndex_ << ", " << (status ? "UP" : "DOWN");
}

TunIntf::~TunIntf() {
  stop();

  // We must have a valid fd to TunIntf
  CHECK_NE(fd_, -1);

  // Delete interface if need be
  if (toDelete_) {
    auto ret = ioctl(fd_, TUNSETPERSIST, 0);
    sysLogError(ret, "Failed to unset persist interface ", name_);
  }

  // Close FD. This will delete the interface if TUNSETPERSIST is not on
  closeFD();
  XLOG(DBG2) << (toDelete_ ? "Delete" : "Detach") << " interface " << name_;
}

void TunIntf::stop() {
  unregisterHandler();
}

void TunIntf::start() {
  if (fd_ != -1 && !isHandlerRegistered()) {
    XLOG(DBG2) << "Starting listening on " << name_ << ": fd " << fd_;
    changeHandlerFD(folly::NetworkSocket::fromFd(fd_));
    registerHandler(folly::EventHandler::READ | folly::EventHandler::PERSIST);
  }
}

void TunIntf::openFD() {
  fd_ = open(kTunDev.c_str(), O_RDWR);
  sysCheckError(fd_, "Cannot open ", kTunDev.c_str());
  SCOPE_FAIL {
    closeFD();
  };

  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  // Flags: IFF_TUN   - TUN device (no Ethernet headers)
  //        IFF_NO_PI - Do not provide packet information
  ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
  bzero(ifr.ifr_name, sizeof(ifr.ifr_name));
  size_t len = std::min(name_.size(), sizeof(ifr.ifr_name));
  memmove(ifr.ifr_name, name_.c_str(), len);
  auto ret = ioctl(fd_, TUNSETIFF, (void*)&ifr);
  sysCheckError(ret, "Failed to create/attach interface ", name_);

  // Set configured MTU
  setMtu(mtu_);

  // make fd non-blocking
  auto flags = fcntl(fd_, F_GETFL);
  sysCheckError(flags, "Failed to get flags from fd ", fd_);
  flags |= O_NONBLOCK;
  ret = fcntl(fd_, F_SETFL, flags);
  sysCheckError(
      ret, "Failed to set non-blocking flags ", flags, " to fd ", fd_);
  flags = fcntl(fd_, F_GETFD);
  sysCheckError(flags, "Failed to get flags from fd ", fd_);
  flags |= FD_CLOEXEC;
  ret = fcntl(fd_, F_SETFD, flags);
  sysCheckError(
      ret, "Failed to set close-on-exec flags ", flags, " to fd ", fd_);

  XLOG(DBG2) << "Create/attach to tun interface " << name_ << " @ fd " << fd_;
}

void TunIntf::closeFD() noexcept {
  auto ret = close(fd_);
  sysLogError(ret, "Failed to close fd ", fd_, " for interface ", name_);
  if (ret == 0) {
    XLOG(DBG2) << "Closed fd " << fd_ << " for interface " << name_;
    fd_ = -1;
  }
}

void TunIntf::addAddress(const folly::IPAddress& addr, uint8_t mask) {
  auto ret = addrs_.emplace(addr, mask);
  if (!ret.second) {
    throw FbossError(
        "Duplication interface address ",
        addr,
        "/",
        static_cast<int>(mask),
        " for interface ",
        name_,
        " @ index",
        ifIndex_);
  }
  XLOG(DBG3) << "Added address " << addr.str() << "/" << static_cast<int>(mask)
             << " to interface " << name_ << " @ index " << ifIndex_;
}

void TunIntf::setMtu(int mtu) {
  mtu_ = mtu;
  auto sock = socket(PF_INET, SOCK_DGRAM, 0);
  sysCheckError(sock, "Failed to open socket");
  SCOPE_EXIT {
    close(sock);
  };

  struct ifreq ifr;
  size_t len = std::min(name_.size(), sizeof(ifr.ifr_name));
  memset(&ifr, 0, sizeof(ifr));
  memmove(ifr.ifr_name, name_.c_str(), len);
  ifr.ifr_mtu = mtu_;
  auto ret = ioctl(sock, SIOCSIFMTU, (void*)&ifr);
  sysCheckError(
      ret,
      "Failed to set MTU ",
      ifr.ifr_mtu,
      " to fd ",
      fd_,
      " errno = ",
      errno);
  XLOG(DBG3) << "Set tun " << name_ << " MTU to " << mtu;
}

void TunIntf::disableIPv6AddrGenMode(int ifIndex) {
  struct iplink_req req;
  bzero(&req, sizeof(req));
  req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
  req.n.nlmsg_flags = NLM_F_REQUEST;
  req.n.nlmsg_type = RTM_NEWLINK;
  req.i.ifi_family = AF_UNSPEC;
  req.i.ifi_index = ifIndex;

  struct rtattr *afs, *afs6;
  afs = addattr_nest(&req.n, sizeof(req), IFLA_AF_SPEC);
  afs6 = addattr_nest(&req.n, sizeof(req), AF_INET6);
  addattr8(
      &req.n, sizeof(req), IFLA_INET6_ADDR_GEN_MODE, IN6_ADDR_GEN_MODE_NONE);
  addattr_nest_end(&req.n, afs6);
  addattr_nest_end(&req.n, afs);

  int err{0};
  struct rtnl_handle rth;
  err = rtnl_open(&rth, 0);
  if (err != 0) {
    throw NlError(err, "can't open rtnetlink.");
  }
  SCOPE_EXIT {
    rtnl_close(&rth);
  };

#ifdef RTNL_HANDLE_F_STRICT_CHK
  // Only the newer libnetlink (tp2's iproute2 7.3.0) defines this flag, and its
  // rtnl_talk() takes no length. The open-source build's 4.12.0 still does.
  err = rtnl_talk(&rth, &req.n, nullptr);
#else
  err = rtnl_talk(&rth, &req.n, nullptr, 0);
#endif
  if (err != 0) {
    // We are not throwing NlError here because ADDR_GEN_MODE is not supported
    // in kernel 3.10 which we are using in production. In that kernel we don't
    // have problem about duplicate link local addresses.
    XLOG(ERR) << folly::exceptionStr(NlError(err, "rtnl_talk failure"));
    return;
  }

  return;
}

void TunIntf::handlerReady(uint16_t /*events*/) noexcept {
  CHECK(fd_ != -1);

  // Don't process packets during shutdown. There is a race where
  // handlerReady may already be executing (or queued) on the EventBase
  // thread when SwSwitch::stop() calls tunMgr_->stopProcessing().
  // Continuing to process packets in the EXITING state can crash because
  // SwSwitch members (ipv6_, arp_, etc.) may already be destroyed.
  if (sw_->isExiting()) {
    return;
  }

  // Since this is L3 packet size, we should also reserve some space for L2
  // header, which is 18 bytes (including one vlan tag)
  int sent = 0;
  int dropped = 0;
  uint64_t bytes = 0;
  bool fdFail = false;
  try {
    while (sent + dropped < kMaxSentOneTime) {
      std::unique_ptr<TxPacket> pkt;
      size_t txPacketCount = TxPacket::getPacketCounter()->load();
      if (txPacketCount >= FLAGS_max_tx_packets) {
        XLOG_EVERY_MS(ERR, 5000) << "Reached max tx packets threshold "
                                 << txPacketCount << ". Dropping packets.";
        ++dropped;
        sw_->stats()
            ->updateTxBufferLimitExceededDrops(); // Increment the counter
        continue;
      }

      auto txVlan = sw_->getVlanIDForTx(ifID_);
      pkt = sw_->allocateL3TxPacket(mtu_, txVlan.has_value());
      auto buf = pkt->buf();
      int ret = 0;
      do {
        ret = read(fd_, buf->writableTail(), buf->tailroom());
      } while (ret == -1 && errno == EINTR);
      if (ret < 0) {
        if (errno != EAGAIN) {
          sysLogError(ret, "Failed to read on ", fd_);
          // Cannot continue read on this fd
          fdFail = true;
        }
        break;
      } else if (ret == 0) {
        // Nothing to read. It shall not happen as the fd is non-blocking.
        // Just add this case to be safe. Adding DCHECK for sanity checking
        // in debug mode.
        DCHECK(false) << "Unexpected event. Nothing to read.";
#ifdef NDEBUG
        // The preceding DCHECK is fatal in debug builds; in release builds it
        // compiles out, so keep the loop-exit break there.
        break;
#endif
      } else if (ret > buf->tailroom()) {
        // The pkt is larger than the buffer. We don't have complete packet.
        // It shall not happen unless the MTU is mis-match. Drop the packet.
        XLOG(ERR) << "Too large packet (" << ret << " > " << buf->tailroom()
                  << ") received from host. Drop the packet.";
        ++dropped;
      } else {
        bytes += ret;
        buf->append(ret);
        sw_->sendL3Packet(std::move(pkt), ifID_);
        ++sent;
      }
    } // while
  } catch (const std::exception& ex) {
    XLOG_EVERY_MS(ERR, 1000) << "Hit some error when forwarding packets :"
                             << folly::exceptionStr(ex);
  }

  if (fdFail) {
    unregisterHandler();
  }

  XLOG(DBG4) << "Forwarded " << sent << " packets (" << bytes
             << " bytes) from host @ fd " << fd_ << " for interface " << name_;
  if (dropped) {
    XLOG(DBG3) << "Dropped " << dropped << " packets from host @ fd " << fd_
               << " for interface " << name_;
  }
}

bool TunIntf::sendPacketToHost(std::unique_ptr<RxPacket> pkt) {
  CHECK(fd_ != -1);
  const int l2Len = pkt->getSrcVlanIf().has_value() ? EthHdr::SIZE
                                                    : EthHdr::UNTAGGED_PKT_SIZE;

  auto buf = pkt->buf();
  if (buf->length() <= l2Len) {
    XLOG(ERR) << "Received a too small packet with length " << buf->length();
    return false;
  }

  // skip L2 header
  buf->trimStart(l2Len);

  int ret = 0;
  do {
    ret = write(fd_, buf->data(), buf->length());
  } while (ret == -1 && errno == EINTR);
  if (ret < 0) {
    sysLogError(ret, "Failed to send packet to host from Interface ", ifID_);
    return false;
  } else if (ret < buf->length()) {
    XLOG(ERR) << "Failed to send full packet to host from Interface " << ifID_
              << ". " << ret << " bytes sent instead of " << buf->length();
    return false;
  }

  XLOG(DBG4) << "Send packet (" << ret << " bytes) to host from Interface "
             << ifID_;
  return true;
}

} // namespace facebook::fboss
