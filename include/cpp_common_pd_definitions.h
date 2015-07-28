/**
 * @file cpp-common_pd_definitions.h Defines instances of PDLog for cpp-common
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2014  Metaswitch Networks Ltd
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version, along with the "Special Exception" for use of
 * the program along with SSL, set forth below. This program is distributed
 * in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details. You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * The author can be reached by email at clearwater@metaswitch.com or by
 * post at Metaswitch Networks Ltd, 100 Church St, Enfield EN2 6BQ, UK
 *
 * Special Exception
 * Metaswitch Networks Ltd  grants you permission to copy, modify,
 * propagate, and distribute a work formed by combining OpenSSL with The
 * Software, or a work derivative of such a combination, even if such
 * copying, modification, propagation, or distribution would otherwise
 * violate the terms of the GPL. You must comply with the GPL in all
 * respects for all of the code used other than OpenSSL.
 * "OpenSSL" means OpenSSL toolkit software distributed by the OpenSSL
 * Project and licensed under the OpenSSL Licenses, or a work based on such
 * software and licensed under the OpenSSL Licenses.
 * "OpenSSL Licenses" means the OpenSSL License and Original SSLeay License
 * under which the OpenSSL Project distributes the OpenSSL toolkit software,
 * as those licenses appear in the file LICENSE-OPENSSL.
 */


#ifndef CPP_COMMON_PD_DEFINITIONS_H__
#define CPP_COMMON_PD_DEFINITIONS_H__

#include "pdlog.h"

// Defines instances of PDLog for the cpp-common module

// The fields for each PDLog instance contains:
//   Identity - Identifies the log id to be used in the syslog id field.
//   Severity - One of Emergency, Alert, Critical, Error, Warning, Notice, 
//              and Info.  Directly corresponds to the syslog severity types.
//              Only PDLOG_ERROR or PDLOG_NOTICE are used.  
//              See syslog_facade.h for definitions.
//   Message  - Formatted description of the condition.
//   Cause    - The cause of the condition.
//   Effect   - The effect the condition.
//   Action   - A list of one or more actions to take to resolve the condition 
//              if it is an error.
static const PDLog CL_DIAMETER_START
(
  PDLogBase::CL_CPP_COMMON_ID + 1,
  PDLOG_NOTICE,
  "Diameter stack is starting.",
  "Diameter stack is beginning initialization.",
  "Normal.",
  "None."
);

static const PDLog CL_DIAMETER_INIT_CMPL
(
  PDLogBase::CL_CPP_COMMON_ID + 2,
  PDLOG_NOTICE,
  "Diameter stack initialization completed.",
  "Diameter stack has completed initialization.",
  "Normal.",
  "None."
);

static const PDLog4<const char*, int, const char*, const char*> 
  CL_DIAMETER_ROUTE_ERR
(
  PDLogBase::CL_CPP_COMMON_ID + 3,
  PDLOG_ERR,
  "Diameter routing error: %s for message with Command-Code %d, "
  "Destination-Host %s and Destination-Realm %s.",
  "No route was found for a Diameter message.",
  "The Diameter message with the specified command code could not "
  "be routed to the destination host within the destination realm.",
  "(1). Check the Diameter host configuration. "
  "(2). Check to see that there is a route to the destination host. "
  "(3). Check for IP connectivity on the Diameter interface using ping. "
  "(4). Wireshark the Diameter interface."
);

static const PDLog1<const char*> CL_DIAMETER_CONN_ERR
(
  PDLogBase::CL_CPP_COMMON_ID + 4,
  PDLOG_ERR,
  "Failed to make a Diameter connection to host %s.",
  "A Diameter connection attempt failed to the specified host.",
  "This impacts the ability to register, subscribe, or make a call.",
  "(1). Check the Diameter host configuration. "
  "(2). Check to see that there is a route to the destination host. "
  "(3). Check for IP connectivity on the Diameter interface using ping. "
  "(4). Wireshark the interface on Diameter interface."
);

static const PDLog4<const char*, const char*, const char*, int> CL_HTTP_COMM_ERR
(
  PDLogBase::CL_CPP_COMMON_ID + 5,
  PDLOG_ERR,
  "Request for %s to HTTP server %s failed with error \"%s\" (code %d).",
  "An HTTP request to the specified server failed with the specified error code.",
  "This condition may impact the ability to register, subscribe, or make a call.",
  "(1). Check to see if the specified host has failed. "
  "(2). Check to see if there is TCP connectivity to the host by using ping "
  "and/or Wireshark."
);

static const PDLog2<int, const char*> CL_MEMCACHED_CLUSTER_UPDATE_STABLE
(
  PDLogBase::CL_CPP_COMMON_ID + 6,
  PDLOG_NOTICE,
  "The memcached cluster configuration has been updated. There are now %d nodes in the cluster.",
  "A change has been detected to the %s configuration file that has changed the memcached cluster.",
  "Normal.",
  "None."
);

static const PDLog3<int, int, const char*> CL_MEMCACHED_CLUSTER_UPDATE_RESIZE
(
  PDLogBase::CL_CPP_COMMON_ID + 7,
  PDLOG_NOTICE,
  "The memcached cluster configuration has been updated. The cluster is resizing from %d nodes to %d nodes.",
  "A change has been detected to the %s configuration file that has changed the memcached cluster.",
  "Normal.",
  "None."
);

static const PDLog3<const char*, const char*, int> CL_HTTP_PROTOCOL_ERR
(
  PDLogBase::CL_CPP_COMMON_ID + 8,
  PDLOG_ERR,
  "Request for %s to HTTP server %s failed with HTTP status %d.",
  "An HTTP request was rejected at the specified server with the specified status code.",
  "This condition may impact the ability to register, subscribe, or make a call.",
  "Check for logs on the specified server to see why it rejected the request."
);

static const PDLog CL_CM_CASSANDRA_CONNECTION_LOST
(
  PDLogBase::CL_CPP_COMMON_ID + 9,
  PDLOG_ERR,
  "The connection to the local Cassandra has been lost.",
  "The connection to the local Cassandra has been lost. The node will periodically "
  "attempt to reconnect.",
  "This node won't be able to read from or write to Cassandra until Cassandra "
  "connectivity is restored.",
  "(1). Check that Cassandra is running on the node."
  "(2). Check that the configuration files in /etc/clearwater and /etc/cassandra are correct."
);

static const PDLog CL_CM_CASSANDRA_CONNECTION_RECOVERED
(
  PDLogBase::CL_CPP_COMMON_ID + 10,
  PDLOG_NOTICE,
  "Cassandra communication error cleared.",
  "Communication to the local Cassandra has been restored.",
  "Normal.",
  "None."
);

static const PDLog CL_CM_MEMCACHED_CONNECTION_LOST
(
  PDLogBase::CL_CPP_COMMON_ID + 11,
  PDLOG_ERR,
  "The connection to Memcached has been lost.",
  "The connection to Memcached has been lost. The node will periodically "
  "attempt to reconnect.",
  "This node won't be able to read from or write to Memcached until Memcached "
  "connectivity is restored.",
  "(1). Check that Memcached is running on the node."
  "(2). Check that the configuration files in /etc/clearwater/ are correct."
);

static const PDLog CL_CM_MEMCACHED_CONNECTION_RECOVERED
(
  PDLogBase::CL_CPP_COMMON_ID + 12,
  PDLOG_NOTICE,
  "Memcached communication error cleared.",
  "Communication to at least one Memcached process has been restored.",
  "Normal.",
  "None."
);
#endif
