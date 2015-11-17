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

#ifndef TINCANXMPPSOCKET_H_
#define TINCANXMPPSOCKET_H_

#include "talk/base/asyncsocket.h"
#include "talk/base/bytebuffer.h"
#include "talk/base/sigslot.h"
#include "talk/xmpp/asyncsocket.h"
#include "talk/xmpp/xmppengine.h"

// The below define selects the SSLStreamAdapter implementation for
// SSL, as opposed to the SSLAdapter socket adapter.
// #define USE_SSLSTREAM 

namespace talk_base {
  class StreamInterface;
  class SocketAddress;
};
extern talk_base::AsyncSocket* cricket_socket_;

namespace tincan {

class TinCanXmppSocket : public buzz::AsyncSocket, public sigslot::has_slots<> {
public:
  TinCanXmppSocket(buzz::TlsOptions tls);
  ~TinCanXmppSocket();

  virtual buzz::AsyncSocket::State state();
  virtual buzz::AsyncSocket::Error error();
  virtual int GetError();

  virtual bool Connect(const talk_base::SocketAddress& addr);
  virtual bool Read(char * data, size_t len, size_t* len_read);
  virtual bool Write(const char * data, size_t len);
  virtual bool Close();
  virtual bool StartTls(const std::string & domainname);

  sigslot::signal1<int> SignalCloseEvent;

private:
  void CreateCricketSocket(int family);
  void OnReadEvent(talk_base::AsyncSocket * socket);
  void OnWriteEvent(talk_base::AsyncSocket * socket);
  void OnConnectEvent(talk_base::AsyncSocket * socket);
  void OnCloseEvent(talk_base::AsyncSocket * socket, int error);

  talk_base::AsyncSocket * cricket_socket_;
  buzz::AsyncSocket::State state_;
  talk_base::ByteBuffer buffer_;
  buzz::TlsOptions tls_;
};

}  // namespace buzz

#endif // TINCANXMPPSOCKET_H_

