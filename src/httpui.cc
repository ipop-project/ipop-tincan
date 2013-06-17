
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
  http_server_.Listen(talk_base::SocketAddress(kLocalHost, kHttpPort));
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

