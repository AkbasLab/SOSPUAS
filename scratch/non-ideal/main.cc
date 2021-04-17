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
#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-helper.h"
#include "ns3/mobility-model.h"
#include "ns3/waypoint-mobility-model.h"
#include "ns3/rectangle.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/internet-stack-helper.h"

#include "uav.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("UAV-APPS");

static void
CourseChange (std::string foo, Ptr<const MobilityModel> mobility)
{
  Vector pos = mobility->GetPosition ();
  Vector vel = mobility->GetVelocity ();
  std::cout << (Simulator::Now ().GetMilliSeconds () / 1000.0) << ", model=" << mobility
            << ", POS: x=" << pos.x << ", y=" << pos.y << ", z=" << pos.z << "; VEL:" << vel.x
            << ", y=" << vel.y << ", z=" << vel.z << std::endl;
}

int
main (int argc, char *argv[])
{

  LogComponentEnable ("UdpClient", LOG_LEVEL_INFO);
  LogComponentEnable ("UdpServer", LOG_LEVEL_INFO);


  CommandLine cmd (__FILE__);
  cmd.Parse (argc, argv);

  uint32_t peripheralNodes = 10;

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

  NS_LOG_INFO ("Create Applications.");
  
  Time interPacketInterval = Seconds (0.05);
  uint16_t port = 4000;

  UAVHelper central (port, UAVDataType::VIRTUAL_FORCES_CENTRAL_POSITION, serverAddress);
  central.SetAttribute ("Interval", TimeValue (interPacketInterval));

  ApplicationContainer apps = central.Install (nodes.Get (0));
  apps.Start (Seconds (1.0));
  apps.Stop (Seconds (100.0));


  UAVHelper client (port, UAVDataType::VIRTUAL_FORCES_POSITION, serverAddress);
  client.SetAttribute ("Interval", TimeValue (interPacketInterval));
  for (uint32_t i = 1; i < nodes.GetN(); i++)
  {
    apps = client.Install (nodes.Get (i));
  }

  MobilityHelper mobility;

  mobility.SetPositionAllocator ("ns3::GridPositionAllocator", "MinX", DoubleValue (0.0), "MinY",
                                 DoubleValue (0.0), "DeltaX", DoubleValue (1.0), "DeltaY",
                                 DoubleValue (1.0), "GridWidth", UintegerValue (3), "LayoutType",
                                 StringValue ("RowFirst"));

  mobility.SetMobilityModel ("ns3::WaypointMobilityModel", "InitialPositionIsWaypoint", BooleanValue(true));

  mobility.Install (nodes);
  Config::Connect ("/NodeList/*/$ns3::MobilityModel/CourseChange", MakeCallback (&CourseChange));
  nodes.Get(0)->GetObject<ns3::WaypointMobilityModel>(MobilityModel::GetTypeId())->AddWaypoint(Waypoint(Seconds(6), Vector(10, 10, 10)));

  apps.Start (Seconds (2.0));
  apps.Stop (Seconds (9.0));

  //
  // Now, do the actual simulation.
  // Limit to 15 seconds
  Simulator::Stop (Seconds (15));

  AsciiTraceHelper ascii;
  wifiPhy.EnablePcap("UAV", nodes);

  NS_LOG_INFO ("Run Simulation.");
  Simulator::Run ();
  NS_LOG_INFO ("Run Finished.");

  Simulator::Destroy ();
  NS_LOG_INFO ("Done.");
}
