
#include <stdlib.h>
#include <iostream>

#include "talk/base/physicalsocketserver.h"
#include "talk/base/thread.h"
#include "talk/base/network.h"
#include "talk/p2p/client/basicportallocator.h"
#include "talk/p2p/base/p2ptransport.h"
#include "talk/base/socketaddress.h"
#include "talk/p2p/base/candidate.h"
#include "talk/p2p/base/constants.h"
#include "talk/base/logging.h"
#include "talk/p2p/base/p2ptransportchannel.h"

static const uint32 kCandidatePriority = 2130706432U;
static const uint32 kCandidateGeneration = 2;
static const uint32 kCandidateGeneration3 = 3;
static const char kCandidateFoundation1[] = "a0+B/1";
static const char kIceUfrag1[] = "TESTICEUFRAG0001";
static const char kIcePwd1[] = "TESTICEPWD00000000000001";

int port;

class TestRun : public talk_base::Runnable {
 public:
  virtual void Run(talk_base::Thread* thread) {
    thread->Run();
  }
};

class InputThread : public talk_base::Runnable, public sigslot::has_slots<>,
    talk_base::MessageHandler {
 public:
  InputThread(cricket::Transport* transport, talk_base::Thread* worker_thread)
      : transport_(transport), worker_thread_(worker_thread), set_(0) {}

  virtual void OnReadPacket(cricket::TransportChannel* connection,
                            const char* data, size_t len, int flags) {
    std::cout << "RCV << " << data << std::endl;
  }

  virtual void Run(talk_base::Thread* thread) {
    while (true) {
    std::cin >> input_;
    std::cout << "READABLE " << transport_->any_channels_readable() << std::endl;
    if (transport_->any_channels_readable() && set_ == 0) {
      cricket::P2PTransportChannel* channel =
          static_cast<cricket::P2PTransportChannel*>(
          transport_->GetChannel(cricket::ICE_CANDIDATE_COMPONENT_DEFAULT));
      channel->SignalReadPacket.connect(this, &InputThread::OnReadPacket);
      set_ = 1;
    }
    worker_thread_->Send(this, 0, NULL);
    }
  }

  virtual void OnMessage(talk_base::Message *msg) {
    std::cout << "WRITABLE " << transport_->any_channels_writable() << std::endl;
    std::cout << "SENT >> " << input_ << std::endl;
    if (transport_->any_channels_writable()) {
      cricket::P2PTransportChannel* channel =
          static_cast<cricket::P2PTransportChannel*>(
          transport_->GetChannel(cricket::ICE_CANDIDATE_COMPONENT_DEFAULT));
      channel->SendPacket(input_.c_str(), sizeof(input_.c_str()), 0);
    }
  }

 private:
  cricket::Transport* transport_;
  talk_base::Thread* worker_thread_;
  std::string input_;
  int set_;
};

class Test : public sigslot::has_slots<> {

 public:
  virtual void OnCandidatesReady(cricket::Transport* transport,
                                 const cricket::Candidates& candidates) {
    std::cout << "DEBUG port " << candidates[0].ToString() << std::endl; 
    std::cout << "DEBUG port " << candidates[0].address().port() << std::endl; 
  }


  virtual void OnAllocationDone(cricket::Transport* transport) {
    std::cout << "DEBUG allocationdone" << std::endl;
  cricket::Candidates candidates2;
  talk_base::SocketAddress address2("192.168.1.156", port);

  cricket::Candidate candidate2(
      "", cricket::ICE_CANDIDATE_COMPONENT_DEFAULT, "udp", address2, 
      kCandidatePriority, kIceUfrag1, kIcePwd1, cricket::LOCAL_PORT_TYPE,
      "", 0, kCandidateFoundation1);

  candidates2.push_back(candidate2);
    transport->OnRemoteCandidates(candidates2);
  }

  virtual void OnConnecting(cricket::Transport* transport) {
    std::cout << "DEBUG connecting" << std::endl;
  }

  virtual void OnRequestSignaling(cricket::Transport* transport) {
    std::cout << "DEBUG request signaling" << std::endl;
    transport->OnSignalingReady();
  }

};

int main(int argc, char **argv) {

  talk_base::LogMessage::LogToDebug(talk_base::LS_INFO);
  int role = 1;
  port = atoi(argv[1]);

  std::string content_name("svpn-sjingle");
  talk_base::PhysicalSocketServer socket_server;
  talk_base::Thread worker_thread(&socket_server);
  worker_thread.Start(new TestRun);

  talk_base::BasicNetworkManager manager;
  cricket::BasicPortAllocator allocator(&manager);
  allocator.set_flags(cricket::PORTALLOCATOR_ENABLE_SHARED_UFRAG);
  cricket::P2PTransport transport(talk_base::Thread::Current(),
                                  &worker_thread,
                                  content_name, &allocator);

  if (role == 1) {
    transport.SetTiebreaker(111111);
    transport.SetRole(cricket::ROLE_CONTROLLING);
  }
  else {
    transport.SetTiebreaker(222222);
    transport.SetRole(cricket::ROLE_CONTROLLED);
  }

  cricket::Candidates candidates;
  cricket::TransportDescription local_desc(
      cricket::NS_JINGLE_ICE_UDP, std::vector<std::string>(),
      kIceUfrag1, kIcePwd1, cricket::ICEMODE_FULL, NULL, candidates);
  transport.SetLocalTransportDescription(local_desc, cricket::CA_OFFER);

  cricket::TransportDescription remote_desc(
      cricket::NS_JINGLE_ICE_UDP, std::vector<std::string>(),
      kIceUfrag1, kIcePwd1, cricket::ICEMODE_FULL, NULL, candidates);

  transport.SetRemoteTransportDescription(remote_desc, cricket::CA_ANSWER);

  Test test;
  transport.SignalRequestSignaling.connect(&test, &Test::OnRequestSignaling);
  transport.SignalConnecting.connect(&test, &Test::OnConnecting);
  transport.SignalCandidatesAllocationDone.connect(&test,
&Test::OnAllocationDone);
  transport.SignalCandidatesReady.connect(&test, &Test::OnCandidatesReady);


  cricket::TransportChannelImpl* channel = transport.CreateChannel(
      cricket::ICE_CANDIDATE_COMPONENT_DEFAULT);

  talk_base::Thread input(&socket_server);
  input.Start(new InputThread(&transport, &worker_thread));

  transport.ConnectChannels();
  talk_base::Thread::Current()->Run();
}
