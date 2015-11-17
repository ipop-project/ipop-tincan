/*
 * ipop-tincan
 * Copyright 2015, University of Florida
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
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
      const char* data, size_t len, const talk_base::SocketAddress& addr,
      const talk_base::PacketTime& ptime);

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
  talk_base::PacketOptions packet_options_;
};

}  // namespace tincan

#endif  // TINCAN_CONTROLLERACCESS_H_

