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

#ifndef SJINGLE_CONTROLLERACCESS_H_
#define SJINGLE_CONTROLLERACCESS_H_
#pragma once

#include "talk/base/socketaddress.h"
#include "talk/p2p/base/basicpacketsocketfactory.h"
#include "talk/base/logging.h"

#include "socialsender.h"
#include "xmppnetwork.h"
#include "svpnconnectionmanager.h"

namespace sjingle {

class ControllerAccess : public SocialSenderInterface,
                         public sigslot::has_slots<> {
 public:
  ControllerAccess(SvpnConnectionManager& manager, XmppNetwork& network,
         talk_base::BasicPacketSocketFactory* packet_factory,
         struct threadqueue* controller_queue_);

  // Inherited from SocialSenderInterface
  virtual void SendToPeer(int nid, const std::string& uid,
                          const std::string& data);

  // Signal handler for PacketSenderInterface
  virtual void HandlePacket(talk_base::AsyncPacketSocket* socket,
      const char* data, size_t len, const talk_base::SocketAddress& addr);

  virtual void ProcessIPPacket(talk_base::AsyncPacketSocket* socket,
      const char* data, size_t len, const talk_base::SocketAddress& addr);

 private:
  XmppNetwork& network_;
  SvpnConnectionManager& manager_;
  talk_base::SocketAddress remote_addr_;
  talk_base::scoped_ptr<talk_base::AsyncPacketSocket> socket_;
  struct threadqueue* controller_queue_;
};

}  // namespace sjingle

#endif  // SJINGLE_CONTROLLERACCESS_H_

