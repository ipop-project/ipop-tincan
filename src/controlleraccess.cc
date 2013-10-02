/*
 * tincan-jingle
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

namespace tincan {

static const char kLocalHost[] = "127.0.0.1";
static const char kLocalHost6[] = "::1";
static const int kUdpPort = 5800;
static const int kBufferSize = 1024;
static std::map<std::string, int> rpc_calls;

enum {
  REGISTER_SVC,
  CREATE_LINK,
  SET_LOCAL_IP,
  SET_REMOTE_IP,
  TRIM_LINK,
  SET_CALLBACK,
  SEND_MSG,
  GET_STATE,
};

static void init_map() {
  rpc_calls["register_svc"] = REGISTER_SVC;
  rpc_calls["create_link"] = CREATE_LINK;
  rpc_calls["set_local_ip"] = SET_LOCAL_IP;
  rpc_calls["set_remote_ip"] = SET_REMOTE_IP;
  rpc_calls["trim_link"] = TRIM_LINK;
  rpc_calls["set_callback"] = SET_CALLBACK;
  rpc_calls["send_msg"] = SEND_MSG;
  rpc_calls["get_state"] = GET_STATE;
}

ControllerAccess::ControllerAccess(
    TinCanConnectionManager& manager, XmppNetwork& network,
    talk_base::BasicPacketSocketFactory* packet_factory,
    struct threadqueue* controller_queue)
    : manager_(manager),
      network_(network),
      controller_queue_(controller_queue) {
  socket_.reset(packet_factory->CreateUdpSocket(
      talk_base::SocketAddress(kLocalHost, kUdpPort), 0, 0));
  socket_->SignalReadPacket.connect(this, &ControllerAccess::HandlePacket);
  socket6_.reset(packet_factory->CreateUdpSocket(
      talk_base::SocketAddress(kLocalHost6, kUdpPort), 0, 0));
  socket6_->SignalReadPacket.connect(this, &ControllerAccess::HandlePacket);
  manager_.set_forward_socket(socket_.get());
  init_map();
}

void ControllerAccess::ProcessIPPacket(talk_base::AsyncPacketSocket* socket,
    const char* data, size_t len, const talk_base::SocketAddress& addr) {
  int count = thread_queue_bput(controller_queue_, data, len);
  TinCanConnectionManager::HandleQueueSignal(0);
}

void ControllerAccess::SendTo(const char* pv, size_t cb,
                              const talk_base::SocketAddress& addr) {
  if (addr.family() == AF_INET) {
    socket_->SendTo(pv, cb, addr);
  }
  else if (addr.family() == AF_INET6)  {
    socket6_->SendTo(pv, cb, addr);
  }
}

void ControllerAccess::SendToPeer(int nid, const std::string& uid,
                                  const std::string& data) {
  Json::Value json(Json::objectValue);
  json["uid"] = uid;
  json["data"] = data;
  std::string msg = json.toStyledString();
  SendTo(msg.c_str(), msg.size(), remote_addr_);
  LOG_F(INFO) << uid << " " << data;
}

void ControllerAccess::SendState(const talk_base::SocketAddress& addr) {
  Json::Value state = manager_.GetState();
  Json::Value local_state;
  local_state["_uid"] = manager_.uid();
  local_state["_ip4"] = manager_.ipv4();
  local_state["_ip6"] = manager_.ipv6();
  local_state["_fpr"] = manager_.fingerprint();
  std::string msg = local_state.toStyledString();
  SendTo(msg.c_str(), msg.size(), addr);

  for (Json::ValueIterator it = state.begin(); it != state.end(); it++) {
    Json::Value peer = *it;
    msg = peer.toStyledString();
    SendTo(msg.c_str(), msg.size(), addr);
  }
}

void ControllerAccess::HandlePacket(talk_base::AsyncPacketSocket* socket,
    const char* data, size_t len, const talk_base::SocketAddress& addr) {
  if (data[0] != '{') return ProcessIPPacket(socket, data, len, addr);
  std::string result;
  std::string message(data, 0, len);
  LOG_F(INFO) << message;
  Json::Reader reader;
  Json::Value root;
  if (!reader.parse(message, root)) {
    result = "{\"error\":\"json parsing failed\"}";
  }

  // TODO - input sanitazation for security purposes
  std::string method = root["m"].asString();
  switch (rpc_calls[method]) {
    case REGISTER_SVC: {
        std::string user = root["username"].asString();
        std::string pass = root["password"].asString();
        std::string host = root["host"].asString();
        network_.set_status(manager_.fingerprint());
        bool res = network_.Login(user, pass, manager_.uid(), host);
      }
      break;
    case CREATE_LINK: {
        int nid = root["nid"].asInt();
        std::string uid = root["uid"].asString();
        std::string fpr = root["fpr"].asString();
        std::string stun = root["stun"].asString();
        std::string turn = root["turn"].asString();
        std::string turn_user = root["turn_user"].asString();
        std::string turn_pass = root["turn_pass"].asString();
        std::string cas = root["cas"].asString();
        bool sec = root["sec"].asBool();
        bool res = manager_.CreateTransport(uid, fpr, nid, stun, turn,
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
        manager_.Setup(uid, ip4, ip4_mask, ip6, ip6_mask);
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
    case SET_CALLBACK: {
        std::string ip = root["ip"].asString();
        int port = root["port"].asInt();
        remote_addr_.SetIP(ip);
        remote_addr_.SetPort(port);
        manager_.set_forward_addr(remote_addr_);
      }
      break;
    case SEND_MSG: {
        int nid = root["nid"].asInt();
        std::string uid = root["uid"].asString();
        std::string fpr = root["data"].asString();
        network_.SendToPeer(nid, uid, fpr);
      }
      break;
    case GET_STATE: {
        SendState(addr);
      }
      break;
    default: {
      }
      break;
  }
 
  if (result.empty()) return;
  SendTo(result.c_str(), result.size(), addr);
}

}  // namespace tincan

