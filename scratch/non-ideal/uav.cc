#include "uav.h"
#include "ns3/udp-server.h"
#include "ns3/udp-client.h"
#include "ns3/udp-trace-client.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"
#include "ns3/names.h"
#include "ns3/log.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv6-address.h"
#include "ns3/address-utils.h"
#include "ns3/nstime.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
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
                           MakeTimeChecker());

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
  m_socket6 = 0;
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

  if (m_socket6 == 0)
    {
      TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
      m_socket6 = Socket::CreateSocket (GetNode (), tid);
      Inet6SocketAddress local6 = Inet6SocketAddress (Ipv6Address::GetAny (), m_port);
      if (m_socket6->Bind (local6) == -1)
        {
          NS_FATAL_ERROR ("Failed to bind socket");
        }
      if (addressUtils::IsMulticast (local6))
        {
          Ptr<UdpSocket> udpSocket = DynamicCast<UdpSocket> (m_socket6);
          if (udpSocket)
            {
              // equivalent to setsockopt (MCAST_JOIN_GROUP)
              udpSocket->MulticastJoinGroup (0, local6);
            }
          else
            {
              NS_FATAL_ERROR ("Error: Failed to join multicast group");
            }
        }
    }

  m_socket->SetRecvCallback (MakeCallback (&UAV::HandleRead, this));
  m_socket6->SetRecvCallback (MakeCallback (&UAV::HandleRead, this));
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
  if (m_socket6 != 0)
    {
      m_socket6->Close ();
      m_socket6->SetRecvCallback (MakeNullCallback<void, Ptr<Socket>> ());
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
    else if (Inet6SocketAddress::IsMatchingType (from))
    {
      NS_LOG_INFO ("At time " << Simulator::Now ().As (Time::S) << " server received "
                              << packet->GetSize () << " bytes from "
                              << Inet6SocketAddress::ConvertFrom (from).GetIpv6 () << " port "
                              << Inet6SocketAddress::ConvertFrom (from).GetPort () << " while at ["
                              << mobility->GetPosition().x << ", "
                              << mobility->GetPosition().y << ", "
                              << mobility->GetPosition().z << "]");
    }

    packet->RemoveAllPacketTags ();
    packet->RemoveAllByteTags ();

  }
}

UAVHelper::UAVHelper (uint16_t port, UAVDataType type, Ipv4Address serverAddress)
{
  m_factory.SetTypeId (UAV::GetTypeId ());
  SetAttribute ("Port", UintegerValue (port));
  SetAttribute("ServerAddress", Ipv4AddressValue(serverAddress));
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
