/**
 * @file snmp_success_fail_count_table.cpp
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

#include "snmp_internal/snmp_includes.h"
#include "snmp_internal/snmp_time_period_table.h"
#include "snmp_success_fail_count_table.h"
#include "logger.h"

namespace SNMP
{

// Storage for the underlying data
struct SuccessFailCount
{
  std::atomic_uint_fast64_t attempts;
  std::atomic_uint_fast64_t successes;
  std::atomic_uint_fast64_t failures;

  void reset(SuccessFailCount* previous = NULL, uint32_t periodstart = 0)
  {
    attempts = 0;
    successes = 0;
    failures = 0;
  }
};


// Just a TimeBasedRow that maps the data from SuccessFailCount into the right columns.
class SuccessFailCountRow: public TimeBasedRow<SuccessFailCount>
{
public:
  SuccessFailCountRow(int index, View* view):
    TimeBasedRow<SuccessFailCount>(index, view) {};
  ColumnData get_columns()
  {
    SuccessFailCount* counts = _view->get_data();
    uint_fast32_t attempts = counts->attempts.load();
    uint_fast32_t successes = counts->successes.load();
    uint_fast32_t failures = counts->failures.load();

    // Construct and return a ColumnData with the appropriate values
    ColumnData ret;
    ret[1] = Value::integer(_index);
    ret[2] = Value::uint(attempts);
    ret[3] = Value::uint(successes);
    ret[4] = Value::uint(failures);
    return ret;
  }
};

class SuccessFailCountTableImpl: public ManagedTable<SuccessFailCountRow, int>, public SuccessFailCountTable
{
public:
  SuccessFailCountTableImpl(std::string name,
                            std::string tbl_oid):
    ManagedTable<SuccessFailCountRow, int>(name,
                                           tbl_oid,
                                           2,
                                           4, // Only columns 2-4 should be visible
                                           { ASN_INTEGER }), // Type of the index column
    five_second(5),
    five_minute(300)
  {
    // We have a fixed number of rows, so create them in the constructor.
    add(TimePeriodIndexes::scopePrevious5SecondPeriod);
    add(TimePeriodIndexes::scopeCurrent5MinutePeriod);
    add(TimePeriodIndexes::scopePrevious5MinutePeriod);
  }

  void increment_attempts()
  {
    // Increment each underlying set of data.
    five_second.get_current()->attempts++;
    five_minute.get_current()->attempts++;
  }

  void increment_successes()
  {
    // Increment each underlying set of data.
    five_second.get_current()->successes++;
    five_minute.get_current()->successes++;
  }

  void increment_failures()
  {
    // Increment each underlying set of data.
    five_second.get_current()->failures++;
    five_minute.get_current()->failures++;
  }

private:
  // Map row indexes to the view of the underlying data they should expose
  SuccessFailCountRow* new_row(int index)
  {
    SuccessFailCountRow::View* view = NULL;
    switch (index)
    {
      case TimePeriodIndexes::scopePrevious5SecondPeriod:
        view = new SuccessFailCountRow::PreviousView(&five_second);
        break;
      case TimePeriodIndexes::scopeCurrent5MinutePeriod:
        view = new SuccessFailCountRow::CurrentView(&five_minute);
        break;
      case TimePeriodIndexes::scopePrevious5MinutePeriod:
        view = new SuccessFailCountRow::PreviousView(&five_minute);
        break;
    }
    return new SuccessFailCountRow(index, view);
  }

  SuccessFailCountRow::CurrentAndPrevious five_second;
  SuccessFailCountRow::CurrentAndPrevious five_minute;
};

SuccessFailCountTable* SuccessFailCountTable::create(std::string name, std::string oid)
{
  return new SuccessFailCountTableImpl(name, oid);
}

}
