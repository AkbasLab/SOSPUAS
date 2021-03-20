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
#include "uav-apps.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("UAV-APPS");

/*static void
CourseChange (std::string foo, Ptr<const MobilityModel> mobility)
{
  Vector pos = mobility->GetPosition ();
  Vector vel = mobility->GetVelocity ();
  std::cout << (Simulator::Now ().GetMilliSeconds () / 1000.0) << ", model=" << mobility
            << ", POS: x=" << pos.x << ", y=" << pos.y << ", z=" << pos.z << "; VEL:" << vel.x
            << ", y=" << vel.y << ", z=" << vel.z << std::endl;
}*/

int
main (int argc, char *argv[])
{

  //
  // Enable logging for UdpClient and
  //
  LogComponentEnable ("UdpClient", LOG_LEVEL_INFO);
  LogComponentEnable ("UdpServer", LOG_LEVEL_INFO);

  Address serverAddress;

  CommandLine cmd (__FILE__);
  cmd.Parse (argc, argv);

  //
  // Explicitly create the nodes required by the topology (shown above).
  //
  NS_LOG_INFO ("Create nodes.");
  NodeContainer nodes;
  nodes.Create (2);

  InternetStackHelper internet;
  internet.Install (nodes);

  NS_LOG_INFO ("Create channels.");
  //
  // Explicitly create the channels required by the topology (shown above).
  //
  CsmaHelper csma;
  csma.SetChannelAttribute ("DataRate", DataRateValue (DataRate (5000000)));
  csma.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (2)));
  csma.SetDeviceAttribute ("Mtu", UintegerValue (1400));
  NetDeviceContainer netDevices = csma.Install (nodes);

  //
  // We've got the "hardware" in place.  Now we need to add IP addresses.
  //
  NS_LOG_INFO ("Assign IP Addresses.");
  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer assignedAddresses = ipv4.Assign (netDevices);
  serverAddress = Address (assignedAddresses.GetAddress (1));

  NS_LOG_INFO ("Create Applications.");
  //
  // Create one udpServer applications on node one.
  //
  uint16_t port = 4000;
  UAVServerHelper server (port);
  ApplicationContainer apps = server.Install (nodes.Get (1));
  apps.Start (Seconds (1.0));
  apps.Stop (Seconds (10.0));

  //
  // Create one UdpClient application to send UDP datagrams from node zero to
  // node one.
  //
  uint32_t MaxPacketSize = 1024;
  Time interPacketInterval = Seconds (0.05);
  uint32_t maxPacketCount = 4;

  UAVClientHelper client (serverAddress, port);
  client.SetAttribute ("MaxPackets", UintegerValue (maxPacketCount));
  client.SetAttribute ("Interval", TimeValue (interPacketInterval));
  client.SetAttribute ("PacketSize", UintegerValue (MaxPacketSize));
  apps = client.Install (nodes.Get (0));

  UAVData packet;
  packet.x = 1.0f;
  packet.y = 0.0f;
  packet.z = -175.0f;
  packet.type = UAVDataType::POSITION;

  client.SetFill (apps.Get (0), reinterpret_cast<std::uint8_t*>(&packet), sizeof(packet));
  apps.Start (Seconds (2.0));
  apps.Stop (Seconds (10.0));

  //
  // Now, do the actual simulation.
  //
  NS_LOG_INFO ("Run Simulation.");
  Simulator::Run ();
  Simulator::Destroy ();
  NS_LOG_INFO ("Done.");
}
