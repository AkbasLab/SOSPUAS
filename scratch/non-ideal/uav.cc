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
                           MakeIpv4AddressAccessor(&UAV::m_rootAddress),
                           MakeIpv4AddressChecker())
          .AddAttribute ("ClientAddress", "The address of the this uav", Ipv4AddressValue(Ipv4Address((uint32_t) 0)),
                           MakeIpv4AddressAccessor(&UAV::m_uavAddress),
                           MakeIpv4AddressChecker())

          .AddAttribute("PacketInterval", "", TimeValue(Seconds(1)),
                           MakeTimeAccessor(&UAV::m_packetInterval),
                           MakeTimeChecker())
          .AddAttribute("CalculateInterval", "", TimeValue(Seconds(0.1)),
                           MakeTimeAccessor(&UAV::m_calculateInterval),
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
      InetSocketAddress local = InetSocketAddress (m_uavAddress, m_port);
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

  m_sendEvent = Simulator::Schedule (Seconds(0.0), &UAV::Send, this);
  m_calculateEvent = Simulator::Schedule (Seconds(0.0), &UAV::Calculate, this);
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
      if (packet->GetSize() != sizeof(UAVData)) {
        //Drop packets that are not the correct size
        continue;
      }
      auto ipv4Addr = InetSocketAddress::ConvertFrom (from).GetIpv4 ();
      if (ipv4Addr == m_uavAddress) {
        continue;
      }
      
      UAVData data;
      packet->CopyData(reinterpret_cast<uint8_t*>(&data), sizeof(UAVData));
      
      
      auto& entry = m_swarmData[ipv4Addr];
      entry.data = data;
    }
    packet->RemoveAllPacketTags ();
    packet->RemoveAllByteTags ();

  }
}


void operator+=(Vector& a, const Vector& b) {
  a.x += b.x;
  a.y += b.y;
  a.z += b.z;
}


Vector operator*(const Vector& a, const Vector& b) {
  return { a.x * b.x, a.y * b.y, a.z * b.z };
}

Vector operator*(const Vector& a, double b) {
  return { a.x * b, a.y * b, a.z * b };
}

Vector operator/(const Vector& a, double b) {
  return { a.x / b, a.y / b, a.z / b };
}

Vector operator/(double a, const Vector& b) {
  return { a / b.x, a / b.y, a / b.z };
}



void
UAV::Send (void)
{
  NS_ASSERT (m_sendEvent.IsExpired ());
  auto mobilityModel = this->GetNode()->GetObject<ns3::WaypointMobilityModel>();
  NS_ASSERT(mobilityModel->IsInitialized());

  UAVData payload;
  payload.position = mobilityModel->GetPosition();
  payload.type = m_uavType;

  Address localAddress;
  m_socket->GetSockName (localAddress);

  for (uint32_t i = 0; i < m_uavCount; i++) {
    Ipv4Address currentPeer(m_rootAddress.Get() + i);

    if (Ipv4Address(currentPeer) == localAddress) {
      //Don't send packets to ourselves
      continue;
    }
    
    m_socket->SendTo(reinterpret_cast<uint8_t*>(&payload), sizeof(payload), 0, InetSocketAddress(currentPeer, m_port));
    m_sent++;

  }

  m_sendEvent = Simulator::Schedule (m_packetInterval, &UAV::Send, this);
}

const double VIRTUAL_FORCES_A = 0.2;
const double VIRTUAL_FORCES_R = 0.4;

void UAV::Calculate() {
  auto mobilityModel = this->GetNode()->GetObject<ns3::WaypointMobilityModel>();

  Vector myPosition = mobilityModel->GetPosition();

  Vector attraction = { 0, 0, 0};
  Vector repulsion = { 0, 0, 0};
  for (auto& pair : m_swarmData) {
    auto& data = pair.second;
    //Points from us to the other node
    auto toOther = data.data.position - myPosition;
    if (m_uavType == UAVDataType::VIRTUAL_FORCES_POSITION && data.data.type == UAVDataType::VIRTUAL_FORCES_CENTRAL_POSITION) {
      //attraction += toOther;
    }
    if (m_uavType == UAVDataType::VIRTUAL_FORCES_POSITION && data.data.type == UAVDataType::VIRTUAL_FORCES_POSITION) {
      //double length = toOther.GetLength();
      //const double MIN_DISTANCE = 0.1;
      //if (length < MIN_DISTANCE) {
        //Stop possible / 0 errors
       // continue;
        //toOther = toOther / length * MIN_DISTANCE;
      //}

      repulsion += 1.0 / toOther;
    }
  }

  double dt = m_calculateInterval.GetSeconds();
  double mass = 1;
  //a=F/m
  Vector acceleration = (attraction * VIRTUAL_FORCES_A + repulsion * VIRTUAL_FORCES_R) / mass;
  if (acceleration.GetLength() < 100) {
    m_velocity += acceleration * dt;
    auto now = Simulator::Now();
    auto later = now + m_calculateInterval;
    mobilityModel->AddWaypoint(Waypoint(later, myPosition + m_velocity * dt));
  } else {
    NS_LOG_ERROR("length too large! " << acceleration << " me at " << myPosition);
  }
  
  m_calculateEvent = Simulator::Schedule (m_calculateInterval, &UAV::Calculate, this);

}

// ========== Helper stuff ==========


UAVHelper::UAVHelper (Ipv4Address serverAddress, uint16_t port, UAVDataType_ type, Time packetInterval, Time calculateInterval, uint32_t uavCount)
{
  m_factory.SetTypeId (UAV::GetTypeId ());
  SetAttribute("ServerAddress", Ipv4AddressValue(serverAddress));
  SetAttribute ("Port", UintegerValue (port));
  SetAttribute("PacketInterval", TimeValue(packetInterval));
  SetAttribute("CalculateInterval", TimeValue(calculateInterval));
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
