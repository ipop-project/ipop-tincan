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

HttpUI::HttpUI(SvpnConnectionManager& manager, XmppNetwork& network) 
    : http_server_(),
      manager_(manager),
      network_(network) {
  http_server_.SignalHttpRequest.connect(this, &HttpUI::OnHttpRequest);
  int error = http_server_.Listen(talk_base::SocketAddress(kLocalHost,
                                                           kHttpPort));
  ASSERT(error == 0);
}

void HttpUI::OnHttpRequest(talk_base::HttpServer* server,
                           talk_base::HttpServerTransaction* transaction) {
  size_t read;
  char data[kBufferSize];
  std::string state;
  transaction->request.document->GetSize(&read);
  if (read > 0) {
    transaction->request.document->SetPosition(0);
    transaction->request.document->Read(data, sizeof(data), &read, 0);
    std::string message(data, 0, read);
    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(message, root)) {
      state = "json parsing failed\n";
    }

    std::string method = root["m"].asString();
    if (method.compare("login") == 0) {
      std::string user = root["u"].asString();
      std::string pass = root["p"].asString();
      std::string host = root["h"].asString();
      network_.Login(user, pass, manager_.uid(), host);
    }
  }
  if (state.empty()) state = manager_.GetState();
  talk_base::MemoryStream* stream = 
      new talk_base::MemoryStream(state.c_str(), state.size());
  transaction->response.set_success(kJsonMimeType, stream);
  server->Respond(transaction);
}

}  // namespace sjingle

