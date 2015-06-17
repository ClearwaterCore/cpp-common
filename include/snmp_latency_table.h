/**
 * @file snmp_latency_table.h
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2015 Metaswitch Networks Ltd
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

#include "snmp_table.h"
#include <vector>
#include <map>
#include <string>
#include <atomic>
#include "snmp_includes.h"
#include "logger.h"

#ifndef SNMP_LATENCY_TABLE_H
#define SNMP_LATENCY_TABLE_H


// A group of rows for a latency or accumulator table, containing:
// - a row for the previous five seconds, with count of samples, average size, variance,
// low-water-mark and high-water-mark
// - a row for the previous five minutes, with count of samples, average size, variance,
// low-water-mark and high-water-mark
class SNMPLatencyRowGroup : public SNMPRowGroup
{
public:
  SNMPLatencyRowGroup(int ignored);
  ColumnData get_columns(netsnmp_tdata_row* row);

  // Add a sample to the statistics
  void accumulate(uint32_t sample);

private:
  netsnmp_tdata_row* five_second_row;
  netsnmp_tdata_row* five_minute_row;

  struct LatencyValues
  {
    int idx;
    std::atomic<uint32_t> count;
    std::atomic<uint32_t> sum;
    std::atomic<uint32_t> sqsum;
    std::atomic<uint32_t> hwm;
    std::atomic<uint32_t> lwm;
  };

  uint32_t tick;

  // Double-buffering - keep two sets of LatencyValues, one which we're currently updating, and one
  // which represents the previous period which we just read from. Switch the reading/writing
  // pointers between the two every five seconds.
  LatencyValues* fiveseconds_writing;
  LatencyValues* fiveseconds_reading;
  LatencyValues fiveseconds_even;
  LatencyValues fiveseconds_odd;

  LatencyValues* fiveminutes_writing;
  LatencyValues* fiveminutes_reading;
  
  void update_internal(LatencyValues* val, uint32_t latency);

  // Make copy/move constructors private to avoid unexpected behaviour
  SNMPLatencyRowGroup(const SNMPLatencyRowGroup&);
  SNMPLatencyRowGroup(const SNMPLatencyRowGroup&&);

  class Row
  {
  public:
    Row(int index_p,
        int interval_p
        void* row_data):
      index(index_p),
      interval(interval_p),
      tick(0)
    {
      zero_struct(even);
      zero_struct(odd);
      netsnmp_row = netsnmp_tdata_create_row();
      netsnmp_tdata_row_add_index(netsnmp_row,
                                  ASN_INTEGER,
                                  &index,
                                  sizeof(int));
      netsnmp_row->data = row_data;
 
    }

    LatencyValues* get_writing_ptr() { update_time(); return writing; } 
    LatencyValues* get_reading_ptr() { update_time(); return reading; } 
    netsnmp_tdata_row* netsnmp_row;

    int index;
  private:
    int interval;
    uint32_t tick;

    // Double-buffering - keep two sets of LatencyValues, one which we're currently updating, and one
    // which represents the previous period which we just read from. Switch the reading/writing
    // pointers between the two every five seconds.
    LatencyValues* writing;
    LatencyValues* reading;
    void zero_struct(LatencyValues& s);
    void update_time();
    LatencyValues even;
    LatencyValues odd;
  }


};

class SNMPLatencyTable: public SNMPTable<SNMPLatencyRowGroup>
{
public:
  SNMPLatencyTable(std::string name,
            oid* tbl_oid,
            int oidlen) : SNMPTable(name, tbl_oid, oidlen)
  {
    // Index off an integer (1 for 5s, 2 for 5m)
    netsnmp_tdata_add_index(_table, ASN_INTEGER);
    netsnmp_table_helper_add_index(_table_info, ASN_INTEGER);

    _table_info->min_column = 2;
    _table_info->max_column = 6;
    register_tbl();
  }
};

class ManagedSNMPLatencyTable: public ManagedSNMPTable<SNMPLatencyTable, SNMPLatencyRowGroup, int>
{
public:
  ManagedSNMPLatencyTable(std::string name,
                          oid* tbl_oid,
                          int oidlen) :
    ManagedSNMPTable<SNMPLatencyTable, SNMPLatencyRowGroup, int>(name, tbl_oid, oidlen)
  {
    // There's only one row group in this table, so force it to be added.
    get(0);
  }
  
  void accumulate(uint32_t sample)
  {
    // Pass samples through to the underlying row group
    SNMPLatencyRowGroup* group = get(0);
    group->accumulate(sample);
  }
};

typedef ManagedSNMPLatencyTable ManagedSNMPAccumulatorTable;

#endif
