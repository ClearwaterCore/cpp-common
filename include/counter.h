/**
 * @file counter.h class definition for a statistics counter
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

#ifndef COUNTER_H__
#define COUNTER_H__

#include <atomic>
#include <time.h>

#include "statrecorder.h"
#include "zmq_lvc.h"

/// @class Counter
///
/// Counts events over a set period, pushing the total number as the statistic
class Counter : public StatRecorder
{
public:

  inline Counter(uint_fast64_t period_us = DEFAULT_PERIOD_US) :
           StatRecorder(period_us)
  {
    reset();
  }

  virtual ~Counter() {}

  /// Increment function
  void increment(void);

  /// Refresh our calculations - called at the end of each period, or
  /// optionally at other times to get an up-to-date result.
  virtual void refresh(bool force = false);

  /// Get number of results in last period.
  inline uint_fast64_t get_count() { return _last._count; }

  virtual void reset();

private:
  /// Current accumulated count.
  struct {
    std::atomic_uint_fast64_t _timestamp_us;
    std::atomic_uint_fast64_t _count;
  } _current;

  /// Count accumulated over the previous period.
  struct {
    volatile uint_fast64_t _count;
  } _last;

  virtual void read(uint_fast64_t period_us);
};

/// @class StatisticCounter
///
/// Counts and reports value as a zeroMQ-based statistic.
class StatisticCounter : public Counter
{
public:
  /// Constructor.
  inline StatisticCounter(std::string statname,
                          LastValueCache* lvc,
                          uint_fast64_t period_us = DEFAULT_PERIOD_US) :
    Counter(period_us),
    _statistic(statname, lvc)
  {}

  /// Callback whenever the accumulated statistics are refreshed. Passes
  /// values to zeroMQ.
  virtual void refreshed();

private:
  /// The zeroMQ-based statistic to report to.
  Statistic _statistic;
};

#endif

