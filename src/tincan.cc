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

#include <cstdio>
#include <iostream>

#if defined(LINUX)
#include <ifaddrs.h>
#elif defined(ANDROID)
#include "talk/base/ifaddrs-android.h"
#endif

#include "talk/base/ssladapter.h"

#include "controlleraccess.h"
#include "tincanconnectionmanager.h"
#include "tincan_utils.h"
#include "xmppnetwork.h"

#define SEGMENT_SIZE 3
#define SEGMENT_OFFSET 4
#define CMP_SIZE 7

namespace tincan {
int kUdpPort = 5800;
string kTapName ("ipop");
}

class SendRunnable : public talk_base::Runnable {
 public:
  SendRunnable(thread_opts_t *opts) : opts_(opts) {}

  virtual void Run(talk_base::Thread *thread) {
    ipop_send_thread(opts_);
  }

 private:
  thread_opts_t *opts_;
};

class RecvRunnable : public talk_base::Runnable {
 public:
  RecvRunnable(thread_opts_t *opts) : opts_(opts) {}

  virtual void Run(talk_base::Thread *thread) {
    ipop_recv_thread(opts_);
  }

 private:
  thread_opts_t *opts_;
};

int get_free_network_ip(char *ip_addr, size_t len) {
#if defined(LINUX) || defined(ANDROID)
  struct ifaddrs* interfaces;
  if (getifaddrs(&interfaces) != 0)  return -1;

  // TODO - we should loop again whenever ip address changes
  char tmp_addr[NI_MAXHOST];
  for (struct ifaddrs* ifa = interfaces; ifa != 0; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr != 0 && ifa->ifa_addr->sa_family == AF_INET) {
      int error = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                              tmp_addr, sizeof(tmp_addr), NULL, 0, 
                              NI_NUMERICHOST);
      if (error == 0) {
        if (strncmp(ip_addr, tmp_addr, CMP_SIZE) == 0) {
          char segment[SEGMENT_SIZE] = { '\0' };
          memcpy(segment, ip_addr + SEGMENT_OFFSET, sizeof(segment) - 1);
          int i = atoi(segment) - 1;
          snprintf(ip_addr + SEGMENT_OFFSET, sizeof(segment), "%d", i);
          ip_addr[CMP_SIZE - 1] = '.';  // snprintf adds extra null
        }
      }
    }
  }
  freeifaddrs(interfaces);
#endif
  return 0;
}

// TODO - Implement some kind of verification mechanism
bool SSLVerificationCallback(void* cert) {
  return true;
}

/* The below method parses the arguments supplied to tincan*/
void parse_args(int argc,char **args) {
  if (argc == 2 && strncmp(args[1], "-v", 2)==0)
    {
      std::cout<<endl
      << "-----tincan version is-----"<< endl
      << tincan::kIpopVerMjr << "." << tincan::kIpopVerMnr << "." 
      << tincan::kIpopVerRev << endl;
      exit(0);
    }
  if (argc == 2 && strncmp(args[1], "-h", 2)==0)
    {
       std::cout<<endl<<"---OPTIONAL---"<<endl
        << "To configure the name of tap device and listener port."<<endl
        << "pass tap-name as first arg and port as second."<<endl
        << "example--sudo sh -c './ipop-tincan looptap 5805 1> out.log 2> err.log &'"<< endl;
        exit(0);
    }
  if (argc == 3)
    {
      tincan::kTapName = args[1];
      tincan::kUdpPort = atoi(args[2]);
    }
  
}

int main(int argc, char **argv) {
  // Parse arguments
  parse_args(argc,argv);
  talk_base::InitializeSSL(SSLVerificationCallback);
  peerlist_init();
  thread_opts_t opts;
#if defined(LINUX) || defined(ANDROID)
  opts.tap = tap_open(tincan::kTapName.c_str(), opts.mac);
  if (opts.tap < 0) return -1;
#elif defined(WIN32)
  opts.win32_tap = open_tap(tincan::kTapName.c_str(), opts.mac);
  if (opts.win32_tap < 0) return -1;
#endif
  opts.translate = 0;
  opts.switchmode = 0;

  talk_base::Thread packet_handling_thread, send_thread, recv_thread;
  talk_base::AutoThread link_setup_thread;
  link_setup_thread.WrapCurrent();

  tincan::PeerSignalSender signal_sender;
  tincan::TinCanConnectionManager manager(&signal_sender, &link_setup_thread,
                                         &packet_handling_thread, &opts);
  tincan::XmppNetwork xmpp(&link_setup_thread);
  xmpp.HandlePeer.connect(&manager,
      &tincan::TinCanConnectionManager::HandlePeer);
  talk_base::BasicPacketSocketFactory packet_factory;
  tincan::ControllerAccess controller(manager, xmpp, &packet_factory, &opts);
  signal_sender.add_service(0, &controller);
  signal_sender.add_service(1, &xmpp);
  opts.send_func = &tincan::TinCanConnectionManager::DoPacketSend;
  opts.recv_func = &tincan::TinCanConnectionManager::DoPacketRecv;

  // Checks to see if network is available, changes IP if not
  char ip_addr[NI_MAXHOST] = { '\0' };
  manager.ipv4().copy(ip_addr, sizeof(ip_addr));
  if (get_free_network_ip(ip_addr, sizeof(ip_addr)) == 0) {
    manager.set_ip(ip_addr);
  }

  // Setup/run threads
  SendRunnable send_runnable(&opts);
  RecvRunnable recv_runnable(&opts);

  send_thread.Start(&send_runnable);
  recv_thread.Start(&recv_runnable);
  packet_handling_thread.Start();
  link_setup_thread.Run();
  
  return 0;
}

