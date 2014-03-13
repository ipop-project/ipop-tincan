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

#ifndef TINCAN_CONTROLLERACCESS_H_
#define TINCAN_CONTROLLERACCESS_H_
#pragma once

#include "talk/base/socketaddress.h"
#include "talk/p2p/base/basicpacketsocketfactory.h"
#include "talk/base/logging.h"

#include "peersignalsender.h"
#include "xmppnetwork.h"
#include "tincanconnectionmanager.h"

namespace tincan {

class ControllerAccess : public PeerSignalSenderInterface,
                         public sigslot::has_slots<> {
 public:
  ControllerAccess(TinCanConnectionManager& manager, XmppNetwork& network,
         talk_base::BasicPacketSocketFactory* packet_factory,
         thread_opts_t* opts);

  // Inherited from PeerSignalSenderInterface
  virtual void SendToPeer(int overlay_id, const std::string& uid,
                          const std::string& data, const std::string& type);

  // Signal handler for PacketSenderInterface
  virtual void HandlePacket(talk_base::AsyncPacketSocket* socket,
      const char* data, size_t len, const talk_base::SocketAddress& addr);

  virtual void ProcessIPPacket(talk_base::AsyncPacketSocket* socket,
      const char* data, size_t len, const talk_base::SocketAddress& addr);

 private:
  void SendTo(const char* pv, size_t cb,
              const talk_base::SocketAddress& addr);
  void SendState(const std::string& uid, bool get_stats,
                 const talk_base::SocketAddress& addr);

  thread_opts_t* opts_;
  XmppNetwork& network_;
  TinCanConnectionManager& manager_;
  talk_base::SocketAddress remote_addr_;
  talk_base::scoped_ptr<talk_base::AsyncPacketSocket> socket_;
  talk_base::scoped_ptr<talk_base::AsyncPacketSocket> socket6_;
  talk_base::Thread *signal_thread_;
};

}  // namespace tincan

#endif  // TINCAN_CONTROLLERACCESS_H_

