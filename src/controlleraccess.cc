/*
 * svpn-jingle
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
#include "talk/base/host.h"

#include "controlleraccess.h"

namespace sjingle {

static const char kLocalHost[] = "127.0.0.1";
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
  GET_STATE,
  PING,
};

void init_map() {
  rpc_calls["register_svc"] = REGISTER_SVC;
  rpc_calls["create_link"] = CREATE_LINK;
  rpc_calls["set_local_ip"] = SET_LOCAL_IP;
  rpc_calls["set_remote_ip"] = SET_REMOTE_IP;
  rpc_calls["trim_link"] = TRIM_LINK;
  rpc_calls["set_callback"] = SET_CALLBACK;
  rpc_calls["get_state"] = GET_STATE;
  rpc_calls["ping"] = PING;
}

ControllerAccess::ControllerAccess(
    SvpnConnectionManager& manager, XmppNetwork& network,
    talk_base::BasicPacketSocketFactory* packet_factory)
    : manager_(manager),
      network_(network) {
  socket_.reset(packet_factory->CreateUdpSocket(
      talk_base::SocketAddress(kLocalHost, kUdpPort), 0, 0));
  socket_->SignalReadPacket.connect(this, &ControllerAccess::HandlePacket);
  init_map();
}

void ControllerAccess::HandlePacket(talk_base::AsyncPacketSocket* socket,
    const char* data, size_t len, const talk_base::SocketAddress& addr) {
  std::string result;
  std::string message(data, 0, len);
  LOG_F(INFO) << message;
  Json::Reader reader;
  Json::Value root;
  if (!reader.parse(message, root)) {
    result = "json parsing failed\n";
  }

  // TODO - input sanitazation for security purposes
  std::string method = root["m"].asString();
  switch (rpc_calls[method]) {
    case REGISTER_SVC: {
        std::string user = root["u"].asString();
        std::string pass = root["p"].asString();
        std::string host = root["h"].asString();
        network_.set_status(manager_.fingerprint());
        network_.Login(user, pass, manager_.uid(), host);
      }
      break;
    case CREATE_LINK: {
        int nid = root["nid"].asInt();
        std::string uid = root["uid"].asString();
        std::string fpr = root["fpr"].asString();
        std::string stun = root["stun"].asString();
        std::string turn = root["turn"].asString();
        bool sec = root["sec"].asBool();
        manager_.CreateTransport(uid, fpr, nid, stun, turn, sec);
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
        manager_.AddIP(uid, ip4, ip6);
      }
      break;
    case TRIM_LINK: {
        std::string uid = root["uid"].asString();
        manager_.DestroyTransport(uid);
      }
      break;
    case SET_CALLBACK: {
        std::string address = root["addr"].asString();
        remote_addr_.FromString(address);
      }
      break;
    case PING: {
        int nid = root["nid"].asInt();
        std::string uid = root["uid"].asString();
        std::string fpr = root["fpr"].asString();
        network_.SendToPeer(nid, uid, fpr);
      }
      break;
    case GET_STATE: {
        result = manager_.GetState();
      }
      break;
    default: {
      }
      break;
  }
 
  if (result.empty()) result = "{}";
  socket_->SendTo(result.c_str(), result.size(), addr);
}

void ControllerAccess::SendToPeer(int nid, const std::string& uid,
                                  const std::string& data) {
  Json::Value json(Json::objectValue);
  json["uid"] = uid;
  json["data"] = data;
  std::string msg = json.toStyledString();
  socket_->SendTo(msg.c_str(), msg.size(), remote_addr_);
}

}  // namespace sjingle

