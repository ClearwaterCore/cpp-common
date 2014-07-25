/**
 * @file httpstack_utils.h Utilities for use with the HttpStack
 *
 * Project Clearwater - IMS in the cloud.
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

#ifndef HTTPSTACK_UTILS_H__
#define HTTPSTACK_UTILS_H__

#include "httpstack.h"
#include "threadpool.h"

namespace HttpStackUtils
{
  /// @class SpawningHandler
  ///
  /// Many handlers use an asynchronous non-blocking execution model.
  /// Instead of blocking the current thread when doing external operations,
  /// they register callbacks that are called (potentially on a different
  /// thread) when the operation completes.  These handlers create a new
  /// "task" object per request that tracks the state necessary to continue
  /// processing when the callback is triggered.
  ///
  /// This class is an implementation of the handler part of this model.
  ///
  /// It takes two template parameters:
  /// @tparam T the type of the task.
  /// @tparam C Although not mandatory according to the HandlerInterface, in
  ///   practice all handlers have some sort of associated config. This is
  ///   the type of the config object.
  template<class T, class C>
  class SpawningHandler : public HttpStack::HandlerInterface
  {
  public:
  inline SpawningHandler(const C* cfg, HttpStack::SasLogger* sas_logger = NULL) :
    _cfg(cfg),
    _sas_logger(sas_logger) {};
  virtual ~SpawningHandler() {};

    /// Process an HTTP request by spawning a new task object and running it.
    /// @param req the request to process.
    /// @param trail the SAS trail ID for the request.
    void process_request(HttpStack::Request& req, SAS::TrailId trail)
    {
      T* task = new T(req, _cfg, trail);
      task->run();
    }

    virtual HttpStack::SasLogger* sas_logger(HttpStack::Request& req)
    {
      if (_sas_logger != NULL)
      {
        return _sas_logger;
      }
      else
      {
        return &HttpStack::DEFAULT_SAS_LOGGER;
      }
    }

  private:
    const C* _cfg;
    HttpStack::SasLogger* _sas_logger;
  };

  /// @class Task
  ///
  /// Base class for per-request task objects spawned by a
  /// SpawningHandler.
  class Task
  {
  public:
    inline Task(HttpStack::Request& req, SAS::TrailId trail) :
      _req(req), _trail(trail)
    {}

    virtual ~Task() {}

    /// Process the request associated with this task. Subclasses of this
    /// class should implement it with their specific business logic.
    virtual void run() = 0;

  protected:
    /// Send an HTTP reply. Calls through to Request::send_reply, picking up
    /// the trail ID from the task.
    ///
    /// @param status_code the HTTP status code to use on the reply.
    void send_http_reply(int status_code)
    {
      _req.send_reply(status_code, trail());
    }

    /// @return the trail ID associated with the request.
    inline SAS::TrailId trail() { return _trail; }

    /// Record a penalty with the load monitor.  This is used to apply
    /// backpressure in the event of overload of a downstream device.
    void record_penalty() { _req.record_penalty(); }

    HttpStack::Request _req;
    SAS::TrailId _trail;
  };

  /// @class PingHandler
  ///
  /// Simple handler that receives ping requests and responds to them.
  class PingHandler : public HttpStack::HandlerInterface
  {
    void process_request(HttpStack::Request& req, SAS::TrailId trail);
  };

  /// @class HandlerThreadPool
  ///
  /// The HttpStack has a limited number of transport threads so handlers
  /// must take care not to block them while doing external work.  This class
  /// is a thread pool that allows the application to execute certain
  /// handlers in a worker thread (which is allowed to block).
  ///
  /// Example code:
  ///   stack = HttpStack::get_instance();
  ///   ExampleHandler handler1;
  ///   ExampleHandler handler2;
  ///
  ///   HttpStackUtils::HandlerThreadPool pool(50);
  ///   stack->register_handler("^/example1", pool.wrap(&handler1));
  ///   stack->register_handler("^/example2", pool.wrap(&handler2));
  class HandlerThreadPool
  {
  public:
    HandlerThreadPool(unsigned int num_threads,
                      unsigned int max_queue = 0);
    ~HandlerThreadPool();

    /// Wrap a handler in a 'wrapper' object.  Requests passed to this
    /// wrapper will be processed on a worker thread.
    HttpStack::HandlerInterface* wrap(HttpStack::HandlerInterface* handler);

  private:
    /// @struct RequestParams
    ///
    /// Structure that is used for passing requests from the HttpStack transport
    /// thread to the thread pool.
    struct RequestParams
    {
      RequestParams(HttpStack::HandlerInterface* handler_param,
                    HttpStack::Request& request_param,
                    SAS::TrailId trail_param) :
        handler(handler_param),
        request(request_param),
        trail(trail_param)
      {}

      HttpStack::HandlerInterface* handler;
      HttpStack::Request request;
      SAS::TrailId trail;
    };

    /// @class Pool
    ///
    /// The thread pool that manages the worker threads and defines how a work
    /// item is processed.
    class Pool : public ThreadPool<RequestParams*>
    {
    public:
      Pool(unsigned int num_threads,
           unsigned int max_queue = 0);

      void process_work(RequestParams*& params);
    };

    /// @class Wrapper
    ///
    /// The wrapper class that is returned to the application on calling
    /// HandlerThreadPool::wrap().
    ///
    /// This implements HttpStack::HandlerInterface so can be used in place
    /// of the real handler when registering with the HttpStack. Its
    /// process_request() method takes an HTTP request object and passes it to
    /// the actual thread pool for processing in a worker thread.
    class Wrapper : public HttpStack::HandlerInterface
    {
    public:
      Wrapper(Pool* pool, HandlerInterface* handler);

      /// Implementation of HandlerInterface::process_request(). This passes
      /// the request to a thread pool for processing.
      void process_request(HttpStack::Request& req, SAS::TrailId trail);

      /// Implementation of HandlerInterface::sas_logger().  This calls
      /// the method on the underlying handler.
      HttpStack::SasLogger* sas_logger(HttpStack::Request& req);

    private:
      // The pool that new requests are passed to.
      Pool* _pool;

      // The wrapped handler.
      HandlerInterface* _handler;
    };

    // The threadpool containing the worker threads.
    Pool _pool;

    // Vector of all the wrapper objects that have been allocated.  These are
    // owned by the HandlerThreadPool (which is responsible for freeing
    // them) and we use this vector to keep track of them.
    std::vector<Wrapper*> _wrappers;
  };

  // Implementation of an HttpStack SAS logger for logging chronos flows. This
  // logs all transactions at "detail" level (level 40).
  class ChronosSasLogger : public HttpStack::SasLogger
  {
    void sas_log_rx_http_req(SAS::TrailId trail,
                             HttpStack::Request& req,
                             uint32_t instance_id = 0);

    void sas_log_tx_http_rsp(SAS::TrailId trail,
                             HttpStack::Request& req,
                             int rc,
                             uint32_t instance_id = 0);

    void sas_log_overload(SAS::TrailId trail,
                          HttpStack::Request& req,
                          int rc,
                          uint32_t instance_id = 0);
  };

  extern ChronosSasLogger CHRONOS_SAS_LOGGER;

} // namespace HttpStackUtils

#endif

