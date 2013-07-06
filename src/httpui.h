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

