
#ifndef SJINGLE_HTTPUI_H_
#define SJINGLE_HTTPUI_H_
#pragma once

#include "talk/base/httpserver.h"
#include "talk/base/httpcommon.h"
#include "talk/base/socketaddress.h"

#include "svpnconnectionmanager.h"

namespace sjingle {

class HttpUI : public sigslot::has_slots<> {
 public:
  HttpUI(SvpnConnectionManager& manager, XmppNetwork& network);

  // signal handlers for HttpServer
  void OnHttpRequest(talk_base::HttpServer* server, 
                     talk_base::HttpServerTransaction* transaction);

 private:
  void HandleRequest();

  talk_base::HttpListenServer http_server_;
  SvpnConnectionManager& manager_;
  XmppNetwork& network_;

};

}  // namespace sjingle

#endif  // SJINGLE_HTTPUI_H_

