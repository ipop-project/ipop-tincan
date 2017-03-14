/*
* ipop-project
* Copyright 2016, University of Florida
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
#include "virtual_link.h"
#pragma warning( push )
#pragma warning(disable:4996)
#include "webrtc/base/stringencode.h"
#pragma warning( pop )
#include "tincan_exception.h"
namespace tincan
{
using namespace rtc;
VirtualLink::VirtualLink(
  unique_ptr<VlinkDescriptor> vlink_desc,
  unique_ptr<PeerDescriptor> peer_desc) :
  vlink_desc_(move(vlink_desc)),
  peer_desc_(move(peer_desc)),
  tiebreaker_(rtc::CreateRandomId64()),
  ice_conn_role_(cricket::CONNECTIONROLE_ACTPASS),
  packet_options_(DSCP_DEFAULT),
  cas_ready_(false)
{
  content_name_.append(vlink_desc_->name).append("_").append(
    peer_desc_->mac_address);
}

VirtualLink::~VirtualLink()
{
  //LOG(TC_DBG) << "vlink dtor=" << this;
}

string VirtualLink::Name()
{
  return content_name_;
}

void
VirtualLink::Initialize(
  const string & local_uid,
  BasicNetworkManager & network_manager,
  scoped_ptr<SSLIdentity>sslid,
  SSLFingerprint const & local_fingerprint)
{
  SetupTransport(network_manager, move(sslid));
  SetupICE(local_uid, local_fingerprint);
  if(vlink_desc_->sec_enabled)
  {
    //cricket::TransportChannelImpl* channel_ =
      //static_cast<cricket::DtlsTransportChannelWrapper*>(
        transport_->CreateChannel(cricket::ICE_CANDIDATE_COMPONENT_DEFAULT);
  }
  else
  {
    //cricket::TransportChannelImpl * channel_ =
    //  static_cast<cricket::P2PTransportChannel*>(
        transport_->CreateChannel(cricket::ICE_CANDIDATE_COMPONENT_DEFAULT);
  }
  RegisterLinkEventHandlers();
  transport_->ConnectChannels();
  transport_->MaybeStartGathering();
}

/* Parses the string delimited list of candidates and adds
them to the P2P transport thereby creating ICE connections
*/
void
VirtualLink::AddRemoteCandidates(
  const string & candidates)
{
  std::istringstream iss(candidates);
  cricket::Candidates cas_vec;
  do {
    std::string candidate_str;
    iss >> candidate_str;
    std::vector<std::string> fields;
    size_t len = rtc::split(candidate_str, ':', &fields);
    rtc::SocketAddress sa;
    if(len >= 10) {
      sa.FromString(fields[2].append(":").append(fields[3]));
      cricket::Candidate candidate(
      atoi(fields[0].c_str()),  //component
      fields[1],                //protocol
      sa,                       //socket address
      atoi(fields[4].c_str()),  //priority
      fields[5],                //username
      fields[6],                //password
      fields[7],                //type
      atoi(fields[8].c_str()), //generation
      fields[9]);              //foundation
      cas_vec.push_back(candidate);
    }
  } while(iss);
  string err;
  bool rv = transport_->AddRemoteCandidates(cas_vec, &err);
  if(!rv)
    throw TCEXCEPT(string("Failed to add remote candidates - ").append(err).c_str());
  return;
}

void
VirtualLink::SetupTransport(
  BasicNetworkManager & network_manager,
  scoped_ptr<SSLIdentity>sslid)
{
  rtc::SocketAddress stun_addr;
  stun_addr.FromString(vlink_desc_->stun_addr);
  port_allocator_.reset(new cricket::BasicPortAllocator(
    &network_manager, &packet_factory_, { stun_addr }));
  port_allocator_->set_flags(kFlags);

  SetupTURN(vlink_desc_->turn_addr, vlink_desc_->turn_user, vlink_desc_->turn_pass);

  if(vlink_desc_->sec_enabled) {
    scoped_refptr<RTCCertificate> cert(RTCCertificate::Create(move(sslid)));
    //Note:DtlsTransportChannelWrapper expects to be created on its worker thread
    transport_ = make_unique<DtlsP2PTransport>(
      content_name_, port_allocator_.get(), cert);
    LOG_F(LS_INFO) << "Using DTLS Transport";
  }
  else {
    transport_ = make_unique<cricket::P2PTransport>(
      content_name_, port_allocator_.get());
    LOG_F(LS_INFO) << "Using PLAINTEXT Transport";
  }
}

void
VirtualLink::OnReadPacket(
  cricket::TransportChannel * channel,
  const char * data,
  size_t len,
  const rtc::PacketTime & ptime,
  int flags)
{
  //TapFrame *frame = new TapFrame((uint8_t*)data, (uint32_t)len);
  SignalMessageReceived((uint8_t*)data, *(uint32_t*)&len, *this);
}

void
VirtualLink::OnSentPacket(
  cricket::TransportChannel * channel,
  const rtc::SentPacket & packet)
{
  //nothing to do atm ...
}

void VirtualLink::OnCandidateGathered(
  cricket::TransportChannelImpl* ch,
  const cricket::Candidate& cnd)
{
  //if((cnd.protocol() == cricket::UDP_PROTOCOL_NAME)
  //  && (cnd.type() == cricket::STUN_PORT_TYPE))
  {
    std::ostringstream oss;
    oss << cnd.component()
      << kCandidateDelim << cnd.protocol()
      << kCandidateDelim << cnd.address().ToString()
      << kCandidateDelim << cnd.priority()
      << kCandidateDelim << cnd.username()
      << kCandidateDelim << cnd.password()
      << kCandidateDelim << cnd.type()
      << kCandidateDelim << cnd.generation()
      << kCandidateDelim << cnd.foundation();
    local_candidates_.push_back(oss.str());
  }
}

void VirtualLink::OnGatheringState(
  cricket::TransportChannelImpl* channel)
{
  if(local_candidates_.empty())
    return;
  SignalLocalCasReady(Candidates());
}

void VirtualLink::OnWriteableState(
  cricket::TransportChannel* channel)
{
  if(channel->writable())
  {
    LOG(TC_DBG) << "link connected to: " << peer_desc_->mac_address;
    SignalLinkReady(*this);
  }
  else
  {
    LOG(TC_DBG) << "link NOT writeable: " << peer_desc_->mac_address;
    SignalLinkBroken(*this);
  }
}

void
VirtualLink::RegisterLinkEventHandlers()
{
  cricket::P2PTransportChannel * channel =
    static_cast<cricket::P2PTransportChannel*>(
      transport_->GetChannel(cricket::ICE_CANDIDATE_COMPONENT_DEFAULT));

  channel->SignalReadPacket.connect(this,
    &VirtualLink::OnReadPacket);

  channel->SignalSentPacket.connect(this,
    &VirtualLink::OnSentPacket);

  channel->SignalCandidateGathered.connect(this,
    &VirtualLink::OnCandidateGathered);

  channel->SignalGatheringState.connect(this,
    &VirtualLink::OnGatheringState);

  channel->SignalWritableState.connect(this, &VirtualLink::OnWriteableState);
}

void VirtualLink::Transmit(TapFrame & frame)
{
  cricket::TransportChannelImpl* channel =
    transport_->GetChannel(cricket::ICE_CANDIDATE_COMPONENT_DEFAULT);

  int status = channel->SendPacket((const char*)frame.BufferToTransfer(),
    frame.BytesToTransfer(), packet_options_, 0);
  if(status < 0)
    LOG_F(LS_INFO) << "Vlink send failed";
}

string VirtualLink::Candidates()
{
  std::string cnds;
  std::unique_lock<std::mutex> lk(cas_mutex_);
  for(auto cnd : local_candidates_)
    cnds.append(cnd).append(" ");
  return cnds;
}

void
VirtualLink::PeerCandidates(
  const string & peer_cas)
{
  peer_desc_->cas = peer_cas;
}

void
VirtualLink::GetStats(Json::Value & stats)
{
  cricket::ConnectionInfos infos;
  cricket::TransportChannelImpl* channel =
    transport_->GetChannel(cricket::ICE_CANDIDATE_COMPONENT_DEFAULT);
  channel->GetStats(&infos);
  for(auto info : infos)
  {
    Json::Value stat(Json::objectValue);
    stat["local_addr"] = info.local_candidate.address().ToString();
    stat["rem_addr"] = info.remote_candidate.address().ToString();
    stat["local_type"] = info.local_candidate.type();
    stat["rem_type"] = info.remote_candidate.type();
    stat["best_conn"] = info.best_connection;
    stat["writable"] = info.writable;
    stat["timeout"] = info.timeout;
    stat["new_conn"] = info.new_connection;
    stat["rtt"] = (Json::UInt64)info.rtt;
    stat["sent_total_bytes"] = (Json::UInt64)info.sent_total_bytes;
    stat["sent_bytes_second"] = (Json::UInt64)info.sent_bytes_second;
    stat["recv_total_bytes"] = (Json::UInt64)info.recv_total_bytes;
    stat["recv_bytes_second"] = (Json::UInt64)info.recv_bytes_second;
    stats.append(stat);
  }
}

void
VirtualLink::SetupICE(
  const string & local_uid,
  SSLFingerprint const & local_fingerprint)
{
  transport_->SetIceTiebreaker(tiebreaker_);
  
  size_t pos = peer_desc_->fingerprint.find(' ');
  string alg, fp;
  if(pos != string::npos)
  {
    alg = peer_desc_->fingerprint.substr(0, pos);
    fp = peer_desc_->fingerprint.substr(++pos);
    remote_fingerprint_.reset(
      rtc::SSLFingerprint::CreateFromRfc4572(alg, fp));
  }
  cricket::ConnectionRole remote_conn_role = cricket::CONNECTIONROLE_ACTIVE;
  ice_conn_role_ = cricket::CONNECTIONROLE_ACTPASS;
  if(local_uid.compare(peer_desc_->uid) < 0) {
    ice_conn_role_ = cricket::CONNECTIONROLE_ACTIVE;
    remote_conn_role = cricket::CONNECTIONROLE_ACTPASS;
  }

  local_description_.reset(new cricket::TransportDescription(
    std::vector<std::string>(),
    kIceUfrag,
    kIcePwd,
    cricket::ICEMODE_FULL,
    ice_conn_role_,
    & local_fingerprint));

  remote_description_.reset(new cricket::TransportDescription(
    std::vector<std::string>(),
    kIceUfrag,
    kIcePwd,
    cricket::ICEMODE_FULL,
    remote_conn_role,
    remote_fingerprint_.get()));

  if(local_uid.compare(peer_desc_->uid) < 0)
  {
    transport_->SetIceRole(cricket::ICEROLE_CONTROLLING);
    //when controlling the remote description must be set first.
    transport_->SetRemoteTransportDescription(
      *remote_description_.get(), cricket::CA_OFFER, NULL);
    transport_->SetLocalTransportDescription(
      *local_description_.get(), cricket::CA_ANSWER, NULL);
  }
  else if(local_uid.compare(peer_desc_->uid) > 0)
  {
    transport_->SetIceRole(cricket::ICEROLE_CONTROLLED);
    transport_->SetLocalTransportDescription(
      *local_description_.get(), cricket::CA_OFFER, NULL);
    transport_->SetRemoteTransportDescription(
      *remote_description_.get(), cricket::CA_ANSWER, NULL);
  }
  else
  {
    throw TCEXCEPT("The Setup Ice operation failed as the peer UIDs are equal. A node will not create a vlink to itself");
  }
}

void
VirtualLink::SetupTURN(
  const string & turn_server,
  const string & username,
  const std::string & password)
{
  if(turn_server.empty()) {
    LOG_F(LS_INFO) << "No TURN Server address provided";
    return;
  }
  if(!turn_server.empty() && (username.empty() || password.empty()))
  {
    LOG_F(LS_WARNING) << "TURN credentials were not provided";
    return;
  }
  SocketAddress turn_addr;
  turn_addr.FromString(turn_server);
  cricket::RelayServerConfig relay_config_udp(cricket::RELAY_TURN);
  relay_config_udp.ports.push_back(cricket::ProtocolAddress(
    turn_addr, cricket::PROTO_UDP));
  relay_config_udp.credentials.username = username;
  relay_config_udp.credentials.password = password;
  port_allocator_->AddTurnServer(relay_config_udp);
  //TODO - TCP relaying
}

void
VirtualLink::StartConnections()
{
  if(peer_desc_->cas.length() == 0)
    throw TCEXCEPT("The vlink connection cannot be started as no connection"
      " candidates were specified in the vlink descriptor");
  AddRemoteCandidates(peer_desc_->cas);
}
void VirtualLink::Disconnect()
{
  transport_->DestroyAllChannels();
}

bool VirtualLink::IsReady()
{
  cricket::TransportChannelImpl* channel =
    transport_->GetChannel(cricket::ICE_CANDIDATE_COMPONENT_DEFAULT);
  return channel->writable();
}
} // end namespace tincan
