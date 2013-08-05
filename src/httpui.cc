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

#include "talk/base/stream.h"
#include "talk/base/json.h"
#include "talk/base/host.h"

#include "httpui.h"

namespace sjingle {

static const char kLocalHost[] = "127.0.0.1";
static const int kHttpPort = 5800;
static const int kBufferSize = 1024;
static const char kJsonMimeType[] = "application/json";

HttpUI::HttpUI(SvpnConnectionManager& manager, XmppNetwork& network,
               talk_base::BasicPacketSocketFactory* packet_factory) 
               : http_server_(),
                 manager_(manager),
                 network_(network) {
  http_server_.SignalHttpRequest.connect(this, &HttpUI::OnHttpRequest);
  socket_.reset(packet_factory->CreateUdpSocket(talk_base::SocketAddress(
                                                kLocalHost, 0), 0, 0));
  int error = http_server_.Listen(talk_base::SocketAddress(kLocalHost,
                                                           kHttpPort));
  ASSERT(error == 0);
}

void HttpUI::OnHttpRequest(talk_base::HttpServer* server,
                           talk_base::HttpServerTransaction* transaction) {
  size_t read;
  char data[kBufferSize];
  std::string result;
  transaction->request.document->GetSize(&read);
  if (read > 0) {
    transaction->request.document->SetPosition(0);
    transaction->request.document->Read(data, sizeof(data), &read, 0);
    std::string message(data, 0, read);
    LOG_F(INFO) << message;
    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(message, root)) {
      result = "json parsing failed\n";
    }

    std::string method = root["m"].asString();
    if (method.compare("register_service") == 0) {
      std::string user = root["u"].asString();
      std::string pass = root["p"].asString();
      std::string host = root["h"].asString();
      network_.set_status(manager_.fingerprint());
      network_.Login(user, pass, manager_.uid(), host);
      result = "{}";
    }
    else if (method.compare("create_link") == 0) {
      int nid = root["nid"].asInt();
      std::string uid = root["uid"].asString();
      std::string fpr = root["fpr"].asString();
      std::string stun = root["stun"].asString();
      std::string turn = root["turn"].asString();
      bool sec = root["sec"].asBool();
      manager_.CreateTransport(uid, fpr, nid, stun, turn, sec);
      result = "{}";
    }
    else if (method.compare("set_local_ip") == 0) {
      std::string uid = root["uid"].asString();
      std::string ip4 = root["ip4"].asString();
      std::string ip6 = root["ip6"].asString();
      int ip4_mask = root["ip4_mask"].asInt();
      int ip6_mask = root["ip6_mask"].asInt();
      manager_.Setup(uid, ip4, ip4_mask, ip6, ip6_mask);
      result = "{}";
    }
    else if (method.compare("set_remote_ip") == 0) {
      std::string uid = root["uid"].asString();
      std::string ip4 = root["ip4"].asString();
      std::string ip6 = root["ip6"].asString();
      manager_.AddIP(uid, ip4, ip6);
      result = "{}";
    }
    else if (method.compare("trim_link") == 0) {
      std::string uid = root["uid"].asString();
      manager_.DestroyTransport(uid);
      result = "{}";
    }
    else if (method.compare("set_callback") == 0) {
      std::string address = root["addr"].asString();
      remote_addr_.FromString(address);
      result = "{}";
    }
    else if (method.compare("ping") == 0) {
      int nid = root["nid"].asInt();
      std::string uid = root["uid"].asString();
      std::string fpr = root["fpr"].asString();
      network_.SendToPeer(nid, uid, fpr);
      result = "{}";
    }
  }
  if (result.empty()) result = manager_.GetState();
  talk_base::MemoryStream* stream = 
      new talk_base::MemoryStream(result.c_str(), result.size());
  transaction->response.set_success(kJsonMimeType, stream);
  server->Respond(transaction);
}

void HttpUI::Send(const char* msg, int len) {
  int sent = socket_->SendTo(msg, len, remote_addr_);
  ASSERT(sent == len);
}

}  // namespace sjingle

