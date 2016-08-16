/**
 * @file memcachedconnectionpool.h  Declaration of derived class for memcached
 * connection pooling
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2013  Metaswitch Networks Ltd
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

#ifndef MEMCACHEDCONNECTIONPOOL_H__
#define MEMCACHEDCONNECTIONPOOL_H__

// Compilation fails when surrounding these two includes with extern "C",
// although it may cause problems not to. Compilation succeeds with extern "C"
// provided <sstream> is included previously. We are unsure of the cause.
#include <libmemcached/memcached.h>
#include <libmemcached/util.h>

#include "connectionpool.h"

class MemcachedConnectionPool : public ConnectionPool<memcached_st*>
{
public:
  MemcachedConnectionPool(time_t max_idle_time_s, std::string options) :
    ConnectionPool<memcached_st*>(max_idle_time_s),
    _options(options),
    _max_connect_latency(50)
  {
  }

  ~MemcachedConnectionPool() {}

protected:
  memcached_st* create_connection(AddrInfo target);
  void destroy_connection(memcached_st* conn);

  std::string _options;
  unsigned int _max_connect_latency;
};

#endif
