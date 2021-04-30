/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
* Copyright (c) 2006,2007 INRIA
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation;
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <cstdint>
#include <fstream>
#include <memory>
#include <fstream>

#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-helper.h"
#include "ns3/mobility-model.h"
#include "ns3/ptr.h"
#include "ns3/waypoint-mobility-model.h"
#include "ns3/rectangle.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/attribute-helper.h"

#include "uav.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("UAV-MAIN");

static void
CourseChange (std::string _unused, Ptr<const MobilityModel> mobility)
{
  /*
  Vector pos = mobility->GetPosition ();
  Vector vel = mobility->GetVelocity ();
  std::cout << (Simulator::Now ().GetMilliSeconds () / 1000.0) << ", model=" << mobility
            << ", POS: x=" << pos.x << ", y=" << pos.y << ", z=" << pos.z << "; VEL:" << vel.x
            << ", y=" << vel.y << ", z=" << vel.z << std::endl;
  */
}

std::unique_ptr<std::ofstream> s_csvFile;

static void LogPositions(const NodeContainer& nodes)
{
  if (!s_csvFile) {
    s_csvFile.reset(new std::ofstream("positions.csv"));
    const char header[] = "Time (s),IP Address, X (m), Y (m), Z (m)";
    s_csvFile->write(header, sizeof(header));
  }

  auto& stream = *s_csvFile;

  for (uint32_t i = 0; i < nodes.GetN(); i++) {
    Ptr<Node> node = nodes.Get(i);
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    auto mobility = node->GetObject<ns3::WaypointMobilityModel>(MobilityModel::GetTypeId());
    Address address = node->GetDevice(0)->GetAddress(); 

    stream << Simulator::Now().GetSeconds() << ',';

    stream << node << ',';
    
    if (InetSocketAddress::IsMatchingType(address)) {
      stream << InetSocketAddress::ConvertFrom(address).GetIpv4() << ',';

    } else if (Ipv4Address::IsMatchingType(address)) {
      stream << Ipv4Address::ConvertFrom(address) << ',';

    } else {
      stream << address.IsMatchingType(0);
      stream << address << ',';
    }
    stream << mobility->GetPosition().x << ',';
    stream << mobility->GetPosition().y << ',';
    stream << mobility->GetPosition().z << ',';
    stream << std::endl;

  }
  
  Simulator::Schedule(MilliSeconds(1), &LogPositions, nodes);
}

int
main (int argc, char *argv[])
{

  LogComponentEnable ("UdpClient", LOG_LEVEL_INFO);
  LogComponentEnable ("UdpServer", LOG_LEVEL_INFO);


  CommandLine cmd (__FILE__);
  cmd.Parse (argc, argv);

  uint32_t peripheralNodes = 3;

  //
  // Explicitly create the nodes required by the topology (shown above).
  //
  NS_LOG_INFO ("Create nodes.");
  NodeContainer nodes;
  nodes.Create (1 + peripheralNodes);

  NS_LOG_INFO ("Create channels.");

  std::string phyMode ("DsssRate1Mbps");
  double rss = -80;  // -dBm
  bool verbose = false;

  // The below set of helpers will help us to put together the wifi NICs we want
  WifiHelper wifi;
  if (verbose)
  {
    wifi.EnableLogComponents ();  // Turn on all Wifi logging
  }
  wifi.SetStandard (WIFI_STANDARD_80211b);
 
  YansWifiPhyHelper wifiPhy;
  // This is one parameter that matters when using FixedRssLossModel
  // set it to zero; otherwise, gain will be added
  wifiPhy.Set ("RxGain", DoubleValue (0) );
  // ns-3 supports RadioTap and Prism tracing extensions for 802.11b
  wifiPhy.SetPcapDataLinkType (WifiPhyHelper::DLT_IEEE802_11_RADIO);
 
  YansWifiChannelHelper wifiChannel;
  wifiChannel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
  // The below FixedRssLossModel will cause the rss to be fixed regardless
  // of the distance between the two stations, and the transmit power
  wifiChannel.AddPropagationLoss ("ns3::FixedRssLossModel","Rss",DoubleValue (rss));
  wifiPhy.SetChannel (wifiChannel.Create ());
 
  // Add a mac and disable rate control
  WifiMacHelper wifiMac;
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                "DataMode",StringValue (phyMode),
                                "ControlMode",StringValue (phyMode));
  // Set it to adhoc mode
  wifiMac.SetType ("ns3::AdhocWifiMac");
  NetDeviceContainer devices = wifi.Install (wifiPhy, wifiMac, nodes);

  NS_LOG_INFO ("Setup ip stack");
  InternetStackHelper internet;
  internet.Install (nodes);

  //Assing IPs
  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer assignedAddresses = ipv4.Assign (devices);
  auto serverAddress = assignedAddresses.GetAddress (0);

  NS_LOG_INFO ("Create Applications. Server address is: " << serverAddress);
  
  Time packetInterval = Seconds (0.05);
  Time calculateInterval = Seconds (0.01);
  uint16_t port = 4000;

  UAVHelper central (serverAddress, port, UAVDataType::VIRTUAL_FORCES_CENTRAL_POSITION, packetInterval, calculateInterval, 1 + peripheralNodes);

  ApplicationContainer apps = central.Install (nodes.Get (0));
  apps.Start (Seconds (0.0));

  UAVHelper client (serverAddress, port, UAVDataType::VIRTUAL_FORCES_POSITION, packetInterval, calculateInterval, 1 + peripheralNodes);
  #if 0
    uint32_t startCount = 2;
  #else
    uint32_t startCount = nodes.GetN();
  #endif

  for (uint32_t i = 1; i < startCount; i++)
  {
    ApplicationContainer apps = client.Install (nodes.Get (i));
    apps.Start (Seconds (1.0));
  }

  MobilityHelper mobility;
#if 1
  Ptr<RandomBoxPositionAllocator> alloc = CreateObject<RandomBoxPositionAllocator> ();

  Ptr<UniformRandomVariable> xz = CreateObject<UniformRandomVariable> ();
  xz->SetAttribute ("Min", DoubleValue (-10));
  xz->SetAttribute ("Max", DoubleValue (10));

  Ptr<UniformRandomVariable> y = CreateObject<UniformRandomVariable> ();
  y->SetAttribute ("Min", DoubleValue (0));
  y->SetAttribute ("Max", DoubleValue (10));


  alloc->SetX(xz);
  alloc->SetY(y);
  alloc->SetZ(xz);
  mobility.SetPositionAllocator(alloc);

#else
  mobility.SetPositionAllocator ("ns3::RandomBoxPositionAllocator", "MinX", DoubleValue (0.0), "MinY",
                                 DoubleValue (0.0), "DeltaX", DoubleValue (100.0), "DeltaY",
                                 DoubleValue (1.0), "GridWidth", UintegerValue (10), "LayoutType",
                                 StringValue ("RowFirst"));

#endif
  mobility.SetMobilityModel ("ns3::WaypointMobilityModel", "InitialPositionIsWaypoint", BooleanValue(true));

  mobility.Install (nodes);
  Config::Connect ("/NodeList/*/$ns3::MobilityModel/CourseChange", MakeCallback (&CourseChange));
  //
  // Now, do the actual simulation.
  Simulator::Stop (Seconds (20));

  AsciiTraceHelper ascii;
  wifiPhy.EnablePcap("UAV", nodes);
  
  Simulator::Schedule(Seconds(0), &LogPositions, nodes);
  NS_LOG_INFO ("Run Simulation.");
  Simulator::Run ();
  NS_LOG_INFO ("Run Finished.");

  Simulator::Destroy ();
  NS_LOG_INFO ("Done.");

  //Save file 
  s_csvFile->flush();
  s_csvFile.reset(nullptr);
}

