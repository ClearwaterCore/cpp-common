/**
 * @file cass_test_utils.h Cassandra unit test utlities.
 *
 * Project Clearwater - IMS in the cloud.
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

#ifndef CASS_TEST_UTILS_H_
#define CASS_TEST_UTILS_H_

#include <semaphore.h>
#include <time.h>

#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "test_interposer.hpp"
#include "fakelogger.h"

#include "cassandra_store.h"

using ::testing::PrintToString;
using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::Throw;
using ::testing::_;
using ::testing::Mock;
using ::testing::MakeMatcher;
using ::testing::Matcher;
using ::testing::MatcherInterface;
using ::testing::MatchResultListener;
using ::testing::Invoke;
using ::testing::AllOf;
using ::testing::DoAll;
using ::testing::Gt;
using ::testing::Lt;

namespace CassTestUtils
{

//
// TEST HARNESS CODE.
//

// Transaction object used by the testbed. This mocks the on_success and
// on_failure methods to allow testcases to control it's behaviour.
//
// The transaction is destroyed by the store on one of it's worker threads.
// When destroyed, this object posts to a semaphore which signals the main
// thread to continue executing the testcase.
class TestTransaction : public CassandraStore::Transaction
{
public:
  TestTransaction(sem_t* sem) :
    CassandraStore::Transaction(0), _sem(sem)
  {}

  virtual ~TestTransaction()
  {
    sem_post(_sem);
  }

  void check_latency(unsigned long expected_latency_us)
  {
    unsigned long actual_latency_us;
    bool rc;

    rc = get_duration(actual_latency_us);
    EXPECT_TRUE(rc);
    EXPECT_EQ(expected_latency_us, actual_latency_us);

    cwtest_advance_time_ms(1);

    rc = get_duration(actual_latency_us);
    EXPECT_TRUE(rc);
    EXPECT_EQ(expected_latency_us, actual_latency_us);
  }

  MOCK_METHOD1(on_success, void(CassandraStore::Operation*));
  MOCK_METHOD1(on_failure, void(CassandraStore::Operation*));

private:
  sem_t* _sem;
};


// A class (and interface) that records the result of a cassandra operation.
//
// In the template:
// -  O is the operation class.
// -  T is the type of data returned by get_result().
class ResultRecorderInterface
{
public:
  virtual void save(CassandraStore::Operation* op) = 0;
};

template<class R, class T>
class ResultRecorder : public ResultRecorderInterface
{
public:
  void save(CassandraStore::Operation* op)
  {
    dynamic_cast<R*>(op)->get_result(result);
  }

  T result;
};


// A specialized transaction that can be configured to record the result of a
// request on a recorder object.
class RecordingTransaction : public TestTransaction
{
public:
  RecordingTransaction(sem_t* sem,
                       ResultRecorderInterface* recorder) :
    TestTransaction(sem),
    _recorder(recorder)
  {}

  virtual ~RecordingTransaction() {}

  void record_result(CassandraStore::Operation* op)
  {
    _recorder->save(op);
  }

private:
  ResultRecorderInterface* _recorder;
};

//
// TYPE DEFINITIONS AND CONSTANTS
//

// A mutation map as used in batch_mutate(). This is of the form:
// { row: { table : [ Mutation ] } }.
typedef std::map<std::string, std::map<std::string, std::vector<cass::Mutation>>> mutmap_t;

// A slice as returned by get_slice().
typedef std::vector<cass::ColumnOrSuperColumn> slice_t;

const slice_t empty_slice(0);

typedef std::map<std::string, std::vector<cass::ColumnOrSuperColumn>> multiget_slice_t;

const multiget_slice_t empty_slice_multiget;

// utlity functions to make a slice from a map of column names => values.
void make_slice(slice_t& slice,
                std::map<std::string, std::string>& columns,
                int32_t ttl = 0)
{
  for(std::map<std::string, std::string>::const_iterator it = columns.begin();
      it != columns.end();
      ++it)
  {
    cass::Column c;
    c.__set_name(it->first);
    c.__set_value(it->second);
    if (ttl != 0)
    {
      c.__set_ttl(ttl);
    }

    cass::ColumnOrSuperColumn csc;
    csc.__set_column(c);

    slice.push_back(csc);
  }
}


//
// MATCHERS
//

// A class that matches against a supplied mutation map.
class MultipleCfMutationMapMatcher : public MatcherInterface<const mutmap_t&> {
public:
  MultipleCfMutationMapMatcher(const std::vector<CassandraStore::RowColumns>& expected):
    _expected(expected)
  {
  };

  virtual bool MatchAndExplain(const mutmap_t& mutmap,
                               MatchResultListener* listener) const
  {
    // First check we have the right number of rows.
    if (mutmap.size() != _expected.size())
    {
      *listener << "map has " << mutmap.size()
                << " rows, expected " << _expected.size();
      return false;
    }

    // Loop through the rows we expect and check that are all present in the
    // mutmap.
    for(std::vector<CassandraStore::RowColumns>::const_iterator expected = _expected.begin();
        expected != _expected.end();
        ++expected)
    {
      std::string row = expected->key;
      std::map<std::string, std::string> expected_columns = expected->columns;
      mutmap_t::const_iterator row_mut = mutmap.find(row);

      if (row_mut == mutmap.end())
      {
        *listener << row << " row expected but not present";
        return false;
      }

      if (row_mut->second.size() != 1)
      {
        *listener << "multiple tables specified for row " << row;
        return false;
      }

      // Get the table name being operated on (there can only be one as checked
      // above), and the mutations being applied to it for this row.
      const std::string& table = row_mut->second.begin()->first;
      const std::vector<cass::Mutation>& row_table_mut =
                                                row_mut->second.begin()->second;
      std::string row_table_name = row + ":" + table;

      // Check we're modifying the right table.
      if (table != expected->cf)
      {
        *listener << "wrong table for " << row
                  << "(expected " << expected->cf
                  << ", got " << table << ")";
        return false;
      }

      // Check we've modifying the right number of columns for this row/table.
      if (row_table_mut.size() != expected_columns.size())
      {
        *listener << "wrong number of columns for " << row_table_name
                  << "(expected " << expected_columns.size()
                  << ", got " << row_table_mut.size() << ")";
        return false;
      }

      for(std::vector<cass::Mutation>::const_iterator mutation = row_table_mut.begin();
          mutation != row_table_mut.end();
          ++mutation)
      {
        // We only allow mutations for a single column (not supercolumns,
        // counters, etc).
        if (!mutation->__isset.column_or_supercolumn ||
            mutation->__isset.deletion ||
            !mutation->column_or_supercolumn.__isset.column ||
            mutation->column_or_supercolumn.__isset.super_column ||
            mutation->column_or_supercolumn.__isset.counter_column ||
            mutation->column_or_supercolumn.__isset.counter_super_column)
        {
          *listener << row_table_name << " has a mutation that isn't a single column change";
          return false;
        }

        // By now we know we're dealing with a column mutation, so extract the
        // column itself and build a descriptive name.
        const cass::Column& column = mutation->column_or_supercolumn.column;
        const std::string row_table_column_name =
                                             row_table_name + ":" + column.name;

        // Check that we were expecting to receive this column and if we were,
        // extract the expected value.
        if (expected_columns.find(column.name) == expected_columns.end())
        {
          *listener << "unexpected mutation " << row_table_column_name;
          return false;
        }

        const std::string& expected_value = expected_columns.find(column.name)->second;

        // Check it specifies the correct value.
        if (!column.__isset.value)
        {
          *listener << row_table_column_name << " does not have a value";
          return false;
        }

        if (column.value != expected_value)
        {
          *listener << row_table_column_name
                    << " has wrong value (expected " << expected_value
                    << " , got " << column.value << ")";
          return false;
        }
      }
    }

    // Phew! All checks passed.
    return true;
  }

  // User fiendly description of what we expect the mutmap to do.
  virtual void DescribeTo(::std::ostream* os) const
  {
  }

private:
  std::vector<CassandraStore::RowColumns> _expected;
};

// A class that matches against a supplied mutation map.
class BatchDeletionMatcher : public MatcherInterface<const mutmap_t&> {
public:
  BatchDeletionMatcher(const std::vector<CassandraStore::RowColumns>& expected):
    _expected(expected)
  {
  };

  virtual bool MatchAndExplain(const mutmap_t& mutmap,
                               MatchResultListener* listener) const
  {
    // First check we have the right number of rows.
    if (mutmap.size() != _expected.size())
    {
      *listener << "map has " << mutmap.size()
                << " rows, expected " << _expected.size();
      return false;
    }

    // Loop through the rows we expect and check that are all present in the
    // mutmap.
    for(std::vector<CassandraStore::RowColumns>::const_iterator expected = _expected.begin();
        expected != _expected.end();
        ++expected)
    {
      std::string row = expected->key;
      std::map<std::string, std::string> expected_columns = expected->columns;
      mutmap_t::const_iterator row_mut = mutmap.find(row);

      if (row_mut == mutmap.end())
      {
        *listener << row << " row expected but not present";
        return false;
      }

      if (row_mut->second.size() != 1)
      {
        *listener << "multiple tables specified for row " << row;
        return false;
      }

      // Get the table name being operated on (there can only be one as checked
      // above), and the mutations being applied to it for this row.
      const std::string& table = row_mut->second.begin()->first;
      const std::vector<cass::Mutation>& row_table_mut =
                                                row_mut->second.begin()->second;
      std::string row_table_name = row + ":" + table;

      // Check we're modifying the right table.
      if (table != expected->cf)
      {
        *listener << "wrong table for " << row
                  << "(expected " << expected->cf
                  << ", got " << table << ")";
        return false;
      }

      // Deletions should only consist of one mutation per row.
      if (row_table_mut.size() != 1)
      {
        *listener << "wrong number of columns for " << row_table_name
                  << "(expected 1"
                  << ", got " << row_table_mut.size() << ")";
        return false;
      }

      const cass::Mutation* mutation = &row_table_mut.front();
      // We only allow mutations for a single column (not supercolumns,
      // counters, etc).
      if (!mutation->__isset.deletion)
      {
        *listener << row_table_name << " has a mutation that isn't a deletion";
        return false;
      }

      const cass::SlicePredicate* deletion = &mutation->deletion.predicate;

      // Check that the number of columns to be deleted is right
      if (deletion->column_names.size() != expected_columns.size())
      {
        *listener << deletion->column_names.size() << " columns deleted, expected " << expected_columns.size();
        return false;
      }

      // Loop over the columns and check that each of them
      // is expected
      for(std::vector<std::string>::const_iterator col = deletion->column_names.begin();
          col != deletion->column_names.end();
          ++col)
      {
        // Check that we were expecting to receive this column and if we were,
        // extract the expected value.
        if (expected_columns.find(*col) == expected_columns.end())
        {
          *listener << "unexpected mutation " << *col;
          return false;
        }
      }

    }

    // Phew! All checks passed.
    return true;
  }

  // User fiendly description of what we expect the mutmap to do.
  virtual void DescribeTo(::std::ostream* os) const
  {
  }

private:
  std::vector<CassandraStore::RowColumns> _expected;
};

// A class that matches against a supplied mutation map.
class MutationMapMatcher : public MatcherInterface<const mutmap_t&> {
public:
  MutationMapMatcher(const std::string& table,
                     const std::vector<std::string>& rows,
                     const std::map<std::string, std::pair<std::string, int32_t> >& columns,
                     int64_t timestamp,
                     int32_t ttl = 0) :
    _table(table),
    _rows(rows),
    _columns(columns),
    _timestamp(timestamp)
  {};

  MutationMapMatcher(const std::string& table,
                     const std::vector<std::string>& rows,
                     const std::map<std::string, std::string>& columns,
                     int64_t timestamp,
                     int32_t ttl = 0) :
    _table(table),
    _rows(rows),
    _timestamp(timestamp)
  {
    for(std::map<std::string, std::string>::const_iterator column = columns.begin();
        column != columns.end();
        ++column)
    {
      _columns[column->first].first = column->second;
      _columns[column->first].second = ttl;
    }
  };

  virtual bool MatchAndExplain(const mutmap_t& mutmap,
                               MatchResultListener* listener) const
  {
    // First check we have the right number of rows.
    if (mutmap.size() != _rows.size())
    {
      *listener << "map has " << mutmap.size()
                << " rows, expected " << _rows.size();
      return false;
    }

    // Loop through the rows we expect and check that are all present in the
    // mutmap.
    for(std::vector<std::string>::const_iterator row = _rows.begin();
        row != _rows.end();
        ++row)
    {
      mutmap_t::const_iterator row_mut = mutmap.find(*row);

      if (row_mut == mutmap.end())
      {
        *listener << *row << " row expected but not present";
        return false;
      }

      if (row_mut->second.size() != 1)
      {
        *listener << "multiple tables specified for row " << *row;
        return false;
      }

      // Get the table name being operated on (there can only be one as checked
      // above), and the mutations being applied to it for this row.
      const std::string& table = row_mut->second.begin()->first;
      const std::vector<cass::Mutation>& row_table_mut =
                                                row_mut->second.begin()->second;
      std::string row_table_name = *row + ":" + table;

      // Check we're modifying the right table.
      if (table != _table)
      {
        *listener << "wrong table for " << *row
                  << "(expected " << _table
                  << ", got " << table << ")";
        return false;
      }

      // Check we've modifying the right number of columns for this row/table.
      if (row_table_mut.size() != _columns.size())
      {
        *listener << "wrong number of columns for " << row_table_name
                  << "(expected " << _columns.size()
                  << ", got " << row_table_mut.size() << ")";
        return false;
      }

      for(std::vector<cass::Mutation>::const_iterator mutation = row_table_mut.begin();
          mutation != row_table_mut.end();
          ++mutation)
      {
        // We only allow mutations for a single column (not supercolumns,
        // counters, etc).
        if (!mutation->__isset.column_or_supercolumn ||
            mutation->__isset.deletion ||
            !mutation->column_or_supercolumn.__isset.column ||
            mutation->column_or_supercolumn.__isset.super_column ||
            mutation->column_or_supercolumn.__isset.counter_column ||
            mutation->column_or_supercolumn.__isset.counter_super_column)
        {
          *listener << row_table_name << " has a mutation that isn't a single column change";
          return false;
        }

        // By now we know we're dealing with a column mutation, so extract the
        // column itself and build a descriptive name.
        const cass::Column& column = mutation->column_or_supercolumn.column;
        const std::string row_table_column_name =
                                             row_table_name + ":" + column.name;

        // Check that we were expecting to receive this column and if we were,
        // extract the expected value.
        if (_columns.find(column.name) == _columns.end())
        {
          *listener << "unexpected mutation " << row_table_column_name;
          return false;
        }

        const std::string& expected_value = _columns.find(column.name)->second.first;
        const int32_t& expected_ttl = _columns.find(column.name)->second.second;

        // Check it specifies the correct value.
        if (!column.__isset.value)
        {
          *listener << row_table_column_name << " does not have a value";
          return false;
        }

        if (column.value != expected_value)
        {
          *listener << row_table_column_name
                    << " has wrong value (expected " << expected_value
                    << " , got " << column.value << ")";
          return false;
        }

        // The timestamp must be set and correct.
        if (!column.__isset.timestamp)
        {
          *listener << row_table_column_name << " timestamp is not set";
          return false;
        }

        if (column.timestamp != _timestamp)
        {
          *listener << row_table_column_name
                    << " has wrong timestamp (expected " << _timestamp
                    << ", got " << column.timestamp << ")";
        }

        if (expected_ttl != 0)
        {
          // A TTL is expected. Check the field is present and correct.
          if (!column.__isset.ttl)
          {
            *listener << row_table_column_name << " ttl is not set";
            return false;
          }

          if (column.ttl != expected_ttl)
          {
            *listener << row_table_column_name
                      << " has wrong ttl (expected " << expected_ttl <<
                      ", got " << column.ttl << ")";
            return false;
          }
        }
        else
        {
          // A TLL is not expected, so check the field is not set.
          if (column.__isset.ttl)
          {
            *listener << row_table_column_name
                      << " ttl is incorrectly set (value is " << column.ttl << ")";
            return false;
          }
        }
      }
    }

    // Phew! All checks passed.
    return true;
  }

  // User fiendly description of what we expect the mutmap to do.
  virtual void DescribeTo(::std::ostream* os) const
  {
    *os << "to write columns " << PrintToString(_columns) <<
           " to rows " << PrintToString(_rows) <<
           " in table " << _table;
  }

private:
  std::string _table;
  std::vector<std::string> _rows;
  std::map<std::string, std::pair<std::string, int32_t> > _columns;
  int64_t _timestamp;
  int32_t _ttl;
};


// Utility functions for creating MutationMapMatcher objects.
inline Matcher<const mutmap_t&>
MutationMap(const std::string& table,
            const std::string& row,
            const std::map<std::string, std::string>& columns,
            int64_t timestamp,
            int32_t ttl = 0)
{
  std::vector<std::string> rows(1, row);
  return MakeMatcher(new MutationMapMatcher(table, rows, columns, timestamp, ttl));
}

inline Matcher<const mutmap_t&>
MutationMap(const std::string& table,
            const std::vector<std::string>& rows,
            const std::map<std::string, std::string>& columns,
            int64_t timestamp,
            int32_t ttl = 0)
{
  return MakeMatcher(new MutationMapMatcher(table, rows, columns, timestamp, ttl));
}

inline Matcher<const mutmap_t&>
MutationMap(const std::string& table,
            const std::string& row,
            const std::map<std::string, std::pair<std::string, int32_t> >& columns,
            int64_t timestamp)
{
  std::vector<std::string> rows(1, row);
  return MakeMatcher(new MutationMapMatcher(table, rows, columns, timestamp));
}

inline Matcher<const mutmap_t&>
MutationMap(const std::vector<CassandraStore::RowColumns>& expected)
{
  return MakeMatcher(new MultipleCfMutationMapMatcher(expected));
}

inline Matcher<const mutmap_t&>
DeletionMap(const std::vector<CassandraStore::RowColumns>& expected)
{
  return MakeMatcher(new BatchDeletionMatcher(expected));
}


// Matcher that check whether the argument is a ColumnPath that refers to a
// single table.
MATCHER_P(ColumnPathForTable, table, std::string("refers to table ")+table)
{
  *result_listener << "refers to table " << arg.column_family;
  return (arg.column_family == table);
}

// Matcher that check whether the argument is a ColumnPath that refers to a
// single table.
MATCHER_P2(ColumnPath, table, column, std::string("refers to table ")+table)
{
  *result_listener << "refers to table " << arg.column_family;
  *result_listener << "refers to column " << arg.column;
  return ((arg.column_family == table) && (arg.column == column));
}


// Matcher that checks whether a SlicePredicate specifies a sequence of specific
// columns.
MATCHER_P(SpecificColumns,
          columns,
          std::string("specifies columns ")+PrintToString(columns))
{
  if (!arg.__isset.column_names || arg.__isset.slice_range)
  {
    *result_listener << "does not specify individual columns";
    return false;
  }

  // Compare the expected and received columns (sorting them before the
  // comparison to ensure a consistent order).
  std::vector<std::string> expected_columns = columns;
  std::vector<std::string> actual_columns = arg.column_names;

  std::sort(expected_columns.begin(), expected_columns.end());
  std::sort(actual_columns.begin(), actual_columns.end());

  if (expected_columns != actual_columns)
  {
    *result_listener << "specifies columns " << PrintToString(actual_columns);
    return false;
  }

  return true;
}

// Matcher that checks whether a SlicePredicate specifies all columns
MATCHER(AllColumns,
          std::string("requests all columns: "))
{
  if (arg.__isset.column_names || !arg.__isset.slice_range)
  {
    *result_listener << "does not request a slice range"; return false;
  }

  if (arg.slice_range.start != "")
  {
    *result_listener << "has incorrect start (" << arg.slice_range.start << ")";
    return false;
  }

  if (arg.slice_range.finish != "")
  {
    *result_listener << "has incorrect finish (" << arg.slice_range.finish << ")";
    return false;
  }

  return true;
}

// Matcher that checks whether a SlicePredicate specifies all columns with a
// particular prefix.
MATCHER_P(ColumnsWithPrefix,
          prefix,
          std::string("requests columns with prefix: ")+prefix)
{
  if (arg.__isset.column_names || !arg.__isset.slice_range)
  {
    *result_listener << "does not request a slice range"; return false;
  }

  if (arg.slice_range.start != prefix)
  {
    *result_listener << "has incorrect start (" << arg.slice_range.start << ")";
    return false;
  }

  // Calculate what the end of the range should be (the last byte should be
  // one more than the start - we don't handle wrapping since homestead-ng
  // doesn't supply names with non-ASCII characters).
  std::string end_str = prefix;
  char last_char = *end_str.rbegin();
  last_char++;
  end_str = end_str.substr(0, end_str.length()-1) + std::string(1, last_char);

  if (arg.slice_range.finish != end_str)
  {
    *result_listener << "has incorrect finish (" << arg.slice_range.finish << ")";
    return false;
  }

  return true;
}

} // namespace CassTestUtils

#endif

