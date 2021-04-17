#include "uav.h"
#include "ns3/udp-server.h"
#include "ns3/udp-client.h"
#include "ns3/udp-trace-client.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"
#include "ns3/names.h"
#include "ns3/log.h"
#include "ns3/ipv4-address.h"
#include "ns3/address-utils.h"
#include "ns3/nstime.h"
#include "ns3/inet-socket-address.h"
#include "ns3/socket.h"
#include "ns3/udp-socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/mobility-model.h"
#include "ns3/waypoint-mobility-model.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("UAV");

NS_OBJECT_ENSURE_REGISTERED (UAV);

TypeId
UAV::GetTypeId (void)
{
  static TypeId tid =
      TypeId ("HASH_POWS")
          .SetParent<Application> ()
          .SetGroupName ("Applications")
          .AddConstructor<UAV> ()
          .AddAttribute ("Port", "Port on which we listen for incoming packets.", UintegerValue (9),
                         MakeUintegerAccessor (&UAV::m_port),
                         MakeUintegerChecker<uint16_t> ())
          .AddTraceSource ("Rx", "A packet has been received",
                           MakeTraceSourceAccessor (&UAV::m_rxTrace),
                           "ns3::Packet::TracedCallback")
          .AddTraceSource ("RxWithAddresses", "A packet has been received",
                           MakeTraceSourceAccessor (&UAV::m_rxTraceWithAddresses),
                           "ns3::Packet::TwoAddressTracedCallback")
          .AddAttribute ("ServerAddress", "The address of the central server node", Ipv4AddressValue(Ipv4Address((uint32_t) 0)),
                           MakeIpv4AddressAccessor(&UAV::m_serverAddress),
                           MakeIpv4AddressChecker())
          .AddAttribute("Interval", "", TimeValue(Seconds(1)),
                           MakeTimeAccessor(&UAV::m_interval),
                           MakeTimeChecker())
          .AddAttribute ("UavCount", "The number of UAV's in the simulation. Used for finding ip addresses. Always >= 2 because of the central node + 1 client node", UintegerValue (2),
                         MakeUintegerAccessor (&UAV::m_uavCount),
                         MakeUintegerChecker<uint32_t> ())
          .AddAttribute ("UavType", "What type this uav is", UintegerValue (2),
                         MakeUintegerAccessor (&UAV::m_uavType),
                         MakeUintegerChecker<UAVDataType_> ())

          ;

  return tid;
}

UAV::UAV ()
{
  NS_LOG_FUNCTION (this);
}

UAV::~UAV ()
{
  NS_LOG_FUNCTION (this);
  m_socket = 0;
}

void
UAV::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  Application::DoDispose ();
}

void
UAV::StartApplication (void)
{
  NS_LOG_FUNCTION (this);

  if (m_socket == 0)
    {
      TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
      m_socket = Socket::CreateSocket (GetNode (), tid);
      InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny (), m_port);
      if (m_socket->Bind (local) == -1)
        {
          NS_FATAL_ERROR ("Failed to bind socket");
        }
      if (addressUtils::IsMulticast (m_local))
        {
          Ptr<UdpSocket> udpSocket = DynamicCast<UdpSocket> (m_socket);
          if (udpSocket)
            {
              // equivalent to setsockopt (MCAST_JOIN_GROUP)
              udpSocket->MulticastJoinGroup (0, m_local);
            }
          else
            {
              NS_FATAL_ERROR ("Error: Failed to join multicast group");
            }
        }
    }

  m_socket->SetRecvCallback (MakeCallback (&UAV::HandleRead, this));
  m_socket->SetAllowBroadcast(true);

  ScheduleTransmit(Seconds(0.0));
}

void
UAV::StopApplication ()
{
  NS_LOG_FUNCTION (this);

  if (m_socket != 0)
    {
      m_socket->Close ();
      m_socket->SetRecvCallback (MakeNullCallback<void, Ptr<Socket>> ());
    }
}

void
UAV::HandleRead (Ptr<Socket> socket)
{

  Ptr<Packet> packet;
  Address from;
  Address localAddress;
  while ((packet = socket->RecvFrom (from)))
  {
    socket->GetSockName (localAddress);
    m_rxTrace (packet);
    m_rxTraceWithAddresses (packet, from, localAddress);
    Ptr<WaypointMobilityModel> mobility = GetNode()->GetObject<WaypointMobilityModel>(MobilityModel::GetTypeId());
    if (InetSocketAddress::IsMatchingType (from))
    {
      NS_LOG_INFO ("At time " << Simulator::Now ().As (Time::S) << " server received "
                              << packet->GetSize () << " bytes from "
                              << InetSocketAddress::ConvertFrom (from).GetIpv4 () << " port "
                              << InetSocketAddress::ConvertFrom (from).GetPort () << " while at ["
                              << mobility->GetPosition().x << ", "
                              << mobility->GetPosition().y << ", "
                              << mobility->GetPosition().z << "]");
    }
    packet->RemoveAllPacketTags ();
    packet->RemoveAllByteTags ();

  }
}


void
UAV::ScheduleTransmit (Time dt)
{
  NS_LOG_FUNCTION (this << dt);
  m_sendEvent = Simulator::Schedule (dt, &UAV::Send, this);
}

void
UAV::Send (void)
{
  NS_LOG_FUNCTION (this);

  NS_ASSERT (m_sendEvent.IsExpired ());

  UAVData payload;
  payload.x = 0;
  payload.y = 0;
  payload.z = 0;
  payload.type = m_uavType;

  Address localAddress;
  m_socket->GetSockName (localAddress);
  // call to the trace sinks before the packet is actually sent,
  // so that tags added to the packet can be sent as well
  NS_LOG_FUNCTION("Self is " << Ipv4Address(localAddress)); 
  for (uint32_t i = 0; i < m_uavCount; i++) {
    Ipv4Address currentPeer(m_serverAddress.Get() + i);

    if (Address(currentPeer) == localAddress) {
      //Don't send packets to ourselves
      continue;
    }
    NS_LOG_FUNCTION("Sending packet to " << currentPeer); 
    m_socket->SendTo(reinterpret_cast<uint8_t*>(&payload), sizeof(payload), 0, Address(currentPeer));
    m_sent++;

  }
  ScheduleTransmit (m_interval);
}


// ========== Helper stuff ==========


UAVHelper::UAVHelper (Ipv4Address serverAddress, uint16_t port, UAVDataType_ type, Time interPacketInterval, uint32_t uavCount)
{
  m_factory.SetTypeId (UAV::GetTypeId ());
  SetAttribute("ServerAddress", Ipv4AddressValue(serverAddress));
  SetAttribute ("Port", UintegerValue (port));
  SetAttribute("Interval", TimeValue(interPacketInterval));
  SetAttribute("UavCount", UintegerValue(uavCount));
  SetAttribute("UavType", UintegerValue(type));
}

void
UAVHelper::SetAttribute (std::string name, const AttributeValue &value)
{
  m_factory.Set (name, value);
}

ApplicationContainer
UAVHelper::Install (Ptr<Node> node) const
{
  return ApplicationContainer (InstallPriv (node));
}

ApplicationContainer
UAVHelper::Install (std::string nodeName) const
{
  Ptr<Node> node = Names::Find<Node> (nodeName);
  return ApplicationContainer (InstallPriv (node));
}

ApplicationContainer
UAVHelper::Install (NodeContainer c) const
{
  ApplicationContainer apps;
  for (NodeContainer::Iterator i = c.Begin (); i != c.End (); ++i)
    {
      apps.Add (InstallPriv (*i));
    }

  return apps;
}

Ptr<Application>
UAVHelper::InstallPriv (Ptr<Node> node) const
{
  Ptr<Application> app = m_factory.Create<UAV> ();
  node->AddApplication (app);

  return app;
}
