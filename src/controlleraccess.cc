/*
 * ipop-tincan
 * Copyright 2013, University of Florida
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "talk/base/json.h"
#include "controlleraccess.h"
#include "tincan_utils.h"

namespace tincan {

static const char kLocalHost[] = "127.0.0.1";
static const char kLocalHost6[] = "::1";
static const int kUdpPort = 5800;
static const int kBufferSize = 1024;
static std::map<std::string, int> rpc_calls;

enum {
  REGISTER_SVC = 1,
  CREATE_LINK = 2,
  SET_LOCAL_IP = 3,
  SET_REMOTE_IP = 4,
  TRIM_LINK = 5,
  SET_CB_ENDPOINT = 6,
  GET_STATE = 7,
  SET_LOGGING = 8,
  SET_TRANSLATION = 9,
  SET_SWITCHMODE = 10,
  SET_TRIMPOLICY = 11,
  ECHO_REQUEST = 12,
  ECHO_REPLY = 13,
  SET_NETWORK_IGNORE_LIST = 14,
};

static void init_map() {
  rpc_calls["register_svc"] = REGISTER_SVC;
  rpc_calls["create_link"] = CREATE_LINK;
  rpc_calls["set_local_ip"] = SET_LOCAL_IP;
  rpc_calls["set_remote_ip"] = SET_REMOTE_IP;
  rpc_calls["trim_link"] = TRIM_LINK;
  rpc_calls["set_cb_endpoint"] = SET_CB_ENDPOINT;
  rpc_calls["get_state"] = GET_STATE;
  rpc_calls["set_logging"] = SET_LOGGING;
  rpc_calls["set_translation"] = SET_TRANSLATION;
  rpc_calls["set_switchmode"] = SET_SWITCHMODE;
  rpc_calls["set_trimpolicy"] = SET_TRIMPOLICY;
  rpc_calls["echo_request"] = ECHO_REQUEST;
  rpc_calls["echo_reply"] = ECHO_REPLY;
  rpc_calls["set_network_ignore_list"] = SET_NETWORK_IGNORE_LIST;
}

ControllerAccess::ControllerAccess(
    TinCanConnectionManager& manager, XmppNetwork& network,
    talk_base::BasicPacketSocketFactory* packet_factory,
    thread_opts_t* opts)
    : manager_(manager),
      network_(network),
      packet_options_(talk_base::DSCP_DEFAULT),
      opts_(opts) {
  signal_thread_ = talk_base::Thread::Current();
  socket_.reset(packet_factory->CreateUdpSocket(
      talk_base::SocketAddress(kLocalHost, kUdpPort), 0, 0));
  socket_->SignalReadPacket.connect(this, &ControllerAccess::HandlePacket);
  socket6_.reset(packet_factory->CreateUdpSocket(
      talk_base::SocketAddress(kLocalHost6, kUdpPort), 0, 0));
  socket6_->SignalReadPacket.connect(this, &ControllerAccess::HandlePacket);
  manager_.set_forward_socket(socket6_.get());
  init_map();
}

void ControllerAccess::ProcessIPPacket(talk_base::AsyncPacketSocket* socket,
    const char* data, size_t len, const talk_base::SocketAddress& addr) {
  ASSERT(signal_thread_->Current());
  manager_.SendToTap(data + kTincanHeaderSize, len - kTincanHeaderSize);
}

void ControllerAccess::SendTo(const char* pv, size_t cb,
                              const talk_base::SocketAddress& addr) {
  ASSERT(signal_thread_->Current());
  talk_base::scoped_ptr<char[]> msg(new char[cb + kTincanHeaderSize]);
  *(msg.get() + kTincanVerOffset) = kIpopVer;
  *(msg.get() + kTincanMsgTypeOffset) = kTincanControl;
  memcpy(msg.get() + kTincanHeaderSize, pv, cb);
  if (addr.family() == AF_INET) {
    socket_->SendTo(msg.get(), cb + kTincanHeaderSize, addr,
                    packet_options_);
  }
  else if (addr.family() == AF_INET6)  {
    socket6_->SendTo(msg.get(), cb + kTincanHeaderSize, addr,
                     packet_options_);
  }
}

void ControllerAccess::SendToPeer(int overlay_id, const std::string& uid,
                                  const std::string& data,
                                  const std::string& type) {
  ASSERT(signal_thread_->Current());
  Json::Value json(Json::objectValue);
  json["uid"] = uid;
  json["data"] = data;
  json["type"] = type;
  std::string msg = json.toStyledString();
  SendTo(msg.c_str(), msg.size(), remote_addr_);
  LOG_TS(INFO) << "uid:" << uid << " data:" << data << " type:" << type;
}

void ControllerAccess::SendState(const std::string& uid, bool get_stats,
                                 const talk_base::SocketAddress& addr) {
  ASSERT(signal_thread_->Current());
  Json::Value state;
  if (uid != "") {
    std::map<std::string, uint32> friends;
    friends[uid] = talk_base::Time();
    state = manager_.GetState(friends, get_stats);
  }
  else {
    state = manager_.GetState(network_.friends(), get_stats);
  }
  Json::Value local_state;
  local_state["_uid"] = manager_.uid();
  local_state["_ip4"] = manager_.ipv4();
  local_state["_ip6"] = manager_.ipv6();
  local_state["_fpr"] = manager_.fingerprint();
  local_state["type"] = "local_state";
  std::ostringstream mac;
  int i;
  for (i=0; i<6; ++i) {
    if (i != 0) mac << ':';
    mac.width(2); 
    mac.fill('0'); 
    mac << std::hex << ((int) *(opts_->mac+i) & 0xff);
  }
  local_state["_mac"] = mac.str();
  std::string msg = local_state.toStyledString();
  SendTo(msg.c_str(), msg.size(), addr);

  for (Json::ValueIterator it = state.begin(); it != state.end(); it++) {
    Json::Value peer = *it;
    peer["type"] = "peer_state";
    msg = peer.toStyledString();
    SendTo(msg.c_str(), msg.size(), addr);
  }
}

void ControllerAccess::HandlePacket(talk_base::AsyncPacketSocket* socket,
    const char* data, size_t len, const talk_base::SocketAddress& addr,
    const talk_base::PacketTime& ptime) {
  ASSERT(signal_thread_->Current());
  if (data[0] != kIpopVer) {
    LOG_TS(LS_ERROR) << "IPOP version mismatch tincan:" << kIpopVer 
                     << " controller:" << data[0];
  }
  if (data[1] == kTincanPacket) return ProcessIPPacket(socket, data, len, addr);
  if (data[1] == kICCControl || data[1] == kICCPacket) {
    /* ICC message is received from controller. Remove IPOP version and type
       field and pass to TinCan Connection manager */
    manager_.HandlePacket(0, data+2, len-2, addr);
    return;
  }
  if (data[1] != kTincanControl) {
    LOG_TS(LS_ERROR) << "Unknown message type"; 
  }
  std::string result;
  std::string message(data, 2, len);
  Json::Reader reader;
  Json::Value root;
  if (!reader.parse(message, root)) {
    result = "{\"error\":\"json parsing failed\"}";
  }
  else {
    LOG_TS(LS_VERBOSE) << "JSONRPC " << message;
  }

  // TODO - input sanitazation for security purposes
  std::string method = root["m"].asString();

  switch (rpc_calls[method]) {
    case REGISTER_SVC: {
        std::string user = root["username"].asString();
        std::string pass = root["password"].asString();
        std::string host = root["host"].asString();
        bool res = network_.Login(user, pass, manager_.uid(), host);
      }
      break;
    case CREATE_LINK: {
        int overlay_id = root["overlay_id"].asInt();
        std::string uid = root["uid"].asString();
        std::string fpr = root["fpr"].asString();
        std::string stun = root["stun"].asString();
        std::string turn = root["turn"].asString();
        std::string turn_user = root["turn_user"].asString();
        std::string turn_pass = root["turn_pass"].asString();
        std::string cas = root["cas"].asString();
        bool sec = root["sec"].asBool();
        bool res = manager_.CreateTransport(uid, fpr, overlay_id, stun, turn,
                                            turn_user, turn_pass, sec);
        if (!cas.empty()) {
          manager_.CreateConnections(uid, cas);
        }
      }
      break;
    case SET_LOCAL_IP: {
        std::string uid = root["uid"].asString();
        std::string ip4 = root["ip4"].asString();
        std::string ip6 = root["ip6"].asString();
        int ip4_mask = root["ip4_mask"].asInt();
        int ip6_mask = root["ip6_mask"].asInt();
        int subnet_mask = root["subnet_mask"].asInt();
        int switchmode = root["switchmode"].asInt();
        manager_.Setup(uid, ip4, ip4_mask, ip6, ip6_mask, subnet_mask,
                       switchmode);
      }
      break;
    case SET_REMOTE_IP: {
        std::string uid = root["uid"].asString();
        std::string ip4 = root["ip4"].asString();
        std::string ip6 = root["ip6"].asString();
        bool res = manager_.AddIPMapping(uid, ip4, ip6);
      }
      break;
    case TRIM_LINK: {
        std::string uid = root["uid"].asString();
        manager_.DestroyTransport(uid);
      }
      break;
    case SET_CB_ENDPOINT: {
        std::string ip = root["ip"].asString();
        int port = root["port"].asInt();
        // sometimes python sends wrong ip address based on platform
        if (ip.compare("::") == 0) {
          remote_addr_ = addr;
        }
        else { 
          remote_addr_.SetIP(ip);
          remote_addr_.SetPort(port);
        }
        manager_.set_forward_addr(remote_addr_);
      }
      break;
    case GET_STATE: {
        std::string uid = root["uid"].asString();
        bool get_stats = root["stats"].asBool();
        SendState(uid, get_stats, addr);
      }
      break;
    case SET_LOGGING: {
        int logging = root["logging"].asInt();
        if (logging == 0) {
          talk_base::LogMessage::LogToDebug(talk_base::LS_ERROR + 1);
        }
        else if (logging == 1) {
          talk_base::LogMessage::LogToDebug(talk_base::LS_ERROR);
        }
        else if (logging == 2) {
          talk_base::LogMessage::LogToDebug(talk_base::LS_INFO);
        }
        else if (logging == 3) {
          talk_base::LogMessage::LogToDebug(talk_base::LS_SENSITIVE);
        }
      }
      break;
    case SET_TRANSLATION: {
        int translate = root["translate"].asInt();
        opts_->translate = translate;
      }
      break;
    case SET_SWITCHMODE: {
        int switchmode = root["switchmode"].asInt();
        opts_->switchmode = switchmode;
      }
      break;
    case SET_TRIMPOLICY: {
        bool trim = root["trim_enabled"].asBool();
        manager_.set_trim_connection(trim);
      }
      break;
    case ECHO_REQUEST: {
        std::string msg = root["msg"].asString();
        Json::Value local_state;
        local_state["type"] = "echo_request";
        local_state["msg"] = msg;
        std::string req = local_state.toStyledString();
        SendTo(req.c_str(), req.size(), addr);
      }
      break;
    case ECHO_REPLY: {
      }
      break;
    case SET_NETWORK_IGNORE_LIST: {
      int count = root["network_ignore_list"].size();
      Json::Value network_ignore_list = root["network_ignore_list"];
      if (network_ignore_list.isArray() != 1) {
        LOG_TS(LERROR) << "Unproperrly styled json network_ignore_list";
        break;
      }
      LOG_TS(INFO) << "Listed network device is ignored for TinCan connection"
                   << network_ignore_list.toStyledString();  
      std::vector<std::string> ignore_list(count);
      for (int i=0;i<count;i++) {
        ignore_list[i] = network_ignore_list[i].asString();
      }
      manager_.set_network_ignore_list(ignore_list);
      }
      break;
    default: {
        int overlay_id = root["overlay_id"].asInt();
        std::string uid = root["uid"].asString();
        std::string fpr = root["data"].asString();
        network_.SendToPeer(overlay_id, uid, fpr, method);
      }
      break;
  }
 
  if (result.empty()) return;
  SendTo(result.c_str(), result.size(), addr);
}

}  // namespace tincan

