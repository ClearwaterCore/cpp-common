/**
 * @file httpconnection.cpp HttpConnection class methods.
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

#include <curl/curl.h>
#include <cassert>
#include <iostream>
#include <map>

#include "cpp_common_pd_definitions.h"
#include "utils.h"
#include "log.h"
#include "sas.h"
#include "httpconnection.h"
#include "load_monitor.h"
#include "random_uuid.h"

/// Maximum number of targets to try connecting to.
static const int MAX_TARGETS = 5;

/// Create an HTTP connection object.
///
/// @param server Server to send HTTP requests to.
/// @param assert_user Assert user in header?
/// @param resolver HTTP resolver to use to resolve server's IP addresses
/// @param stat_name SNMP table to report connection info to.
/// @param load_monitor Load Monitor.
/// @param sas_log_level the level to log HTTP flows at (none/protocol/detail).
HttpConnection::HttpConnection(const std::string& server,
                               bool assert_user,
                               HttpResolver* resolver,
                               SNMP::IPCountTable* stat_table,
                               LoadMonitor* load_monitor,
                               SASEvent::HttpLogLevel sas_log_level,
                               BaseCommunicationMonitor* comm_monitor) :
  _server(server),
  _host(host_from_server(server)),
  _port(port_from_server(server)),
  _assert_user(assert_user),
  _resolver(resolver),
  _load_monitor(load_monitor),
  _sas_log_level(sas_log_level),
  _comm_monitor(comm_monitor),
  _stat_table(stat_table),
  _conn_pool(load_monitor, stat_table)
{
  pthread_key_create(&_uuid_thread_local, cleanup_uuid);
  pthread_mutex_init(&_lock, NULL);
  curl_global_init(CURL_GLOBAL_DEFAULT);

  if (_load_monitor)
  {
  std::vector<std::string> no_stats;
  }

  TRC_STATUS("Configuring HTTP Connection");
  TRC_STATUS("  Connection created for server %s", _server.c_str());
}

/// Create an HTTP connection object.
///
/// @param server Server to send HTTP requests to.
/// @param assert_user Assert user in header?
/// @param resolver HTTP resolver to use to resolve server's IP addresses
/// @param sas_log_level the level to log HTTP flows at (none/protocol/detail).
HttpConnection::HttpConnection(const std::string& server,
                               bool assert_user,
                               HttpResolver* resolver,
                               SASEvent::HttpLogLevel sas_log_level,
                               BaseCommunicationMonitor* comm_monitor) :
  HttpConnection(server,
                 assert_user,
                 resolver,
                 NULL,
                 NULL,
                 sas_log_level,
                 comm_monitor)
{
}

HttpConnection::~HttpConnection()
{
  RandomUUIDGenerator* uuid_gen =
    (RandomUUIDGenerator*)pthread_getspecific(_uuid_thread_local);

  if (uuid_gen != NULL)
  {
    pthread_setspecific(_uuid_thread_local, NULL);
    cleanup_uuid(uuid_gen); uuid_gen = NULL;
  }

  pthread_key_delete(_uuid_thread_local);
}

// Map the CURLcode into a sensible HTTP return code.
HTTPCode HttpConnection::curl_code_to_http_code(CURL* curl, CURLcode code)
{
  switch (code)
  {
  case CURLE_OK:
  {
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    return http_code;
  // LCOV_EXCL_START
  }
  case CURLE_URL_MALFORMAT:
  case CURLE_NOT_BUILT_IN:
    return HTTP_BAD_REQUEST;
  // LCOV_EXCL_STOP
  case CURLE_REMOTE_FILE_NOT_FOUND:
    return HTTP_NOT_FOUND;
  // LCOV_EXCL_START
  case CURLE_COULDNT_RESOLVE_PROXY:
  case CURLE_COULDNT_RESOLVE_HOST:
  case CURLE_COULDNT_CONNECT:
  case CURLE_AGAIN:
    return HTTP_NOT_FOUND;
  default:
    return HTTP_SERVER_ERROR;
  // LCOV_EXCL_STOP
  }
}

HTTPCode HttpConnection::send_delete(const std::string& path,
                                     SAS::TrailId trail,
                                     const std::string& body)
{
  std::string unused_response;
  std::map<std::string, std::string> unused_headers;

  return send_delete(path, unused_headers, unused_response, trail, body);
}

HTTPCode HttpConnection::send_delete(const std::string& path,
                                     SAS::TrailId trail,
                                     const std::string& body,
                                     const std::string& override_server)
{
  std::string unused_response;
  std::map<std::string, std::string> unused_headers;
  change_server(override_server);

  return send_delete(path, unused_headers, unused_response, trail, body);
}

HTTPCode HttpConnection::send_delete(const std::string& path,
                                     SAS::TrailId trail,
                                     const std::string& body,
                                     std::string& response)
{
  std::map<std::string, std::string> unused_headers;

  return send_delete(path, unused_headers, response, trail, body);
}

HTTPCode HttpConnection::send_delete(const std::string& path,
                                     std::map<std::string, std::string>& headers,
                                     std::string& response,
                                     SAS::TrailId trail,
                                     const std::string& body,
                                     const std::string& username)
{
  std::vector<std::string> unused_extra_headers;
  HTTPCode status = send_request(RequestType::DELETE,
                                 path,
                                 body,
                                 response,
                                 username,
                                 trail,
                                 unused_extra_headers,
                                 NULL);
  return status;
}

HTTPCode HttpConnection::send_put(const std::string& path,
                                  const std::string& body,
                                  SAS::TrailId trail,
                                  const std::string& username)
{
  std::string unused_response;
  std::map<std::string, std::string> unused_headers;
  std::vector<std::string> extra_req_headers;
  return HttpConnection::send_put(path,
                                  unused_headers,
                                  unused_response,
                                  body,
                                  extra_req_headers,
                                  trail,
                                  username);
}

HTTPCode HttpConnection::send_put(const std::string& path,
                                  std::string& response,
                                  const std::string& body,
                                  SAS::TrailId trail,
                                  const std::string& username)
{
  std::map<std::string, std::string> unused_headers;
  std::vector<std::string> extra_req_headers;
  return HttpConnection::send_put(path,
                                  unused_headers,
                                  response,
                                  body,
                                  extra_req_headers,
                                  trail,
                                  username);
}

HTTPCode HttpConnection::send_put(const std::string& path,
                                  std::map<std::string, std::string>& headers,
                                  const std::string& body,
                                  SAS::TrailId trail,
                                  const std::string& username)
{
  std::string unused_response;
  std::vector<std::string> extra_req_headers;
  return HttpConnection::send_put(path,
                                  headers,
                                  unused_response,
                                  body,
                                  extra_req_headers,
                                  trail,
                                  username);
}

HTTPCode HttpConnection::send_put(const std::string& path,
                                  std::map<std::string, std::string>& headers,
                                  std::string& response,
                                  const std::string& body,
                                  const std::vector<std::string>& extra_req_headers,
                                  SAS::TrailId trail,
                                  const std::string& username)
{
  HTTPCode status = send_request(RequestType::PUT,
                                 path,
                                 body,
                                 response,
                                 "",
                                 trail,
                                 extra_req_headers,
                                 &headers);
  return status;
}

HTTPCode HttpConnection::send_post(const std::string& path,
                                   std::map<std::string, std::string>& headers,
                                   const std::string& body,
                                   SAS::TrailId trail,
                                   const std::string& username)
{
  std::string unused_response;
  return HttpConnection::send_post(path, headers, unused_response, body, trail, username);
}

HTTPCode HttpConnection::send_post(const std::string& path,
                                   std::map<std::string, std::string>& headers,
                                   std::string& response,
                                   const std::string& body,
                                   SAS::TrailId trail,
                                   const std::string& username)
{
  std::vector<std::string> unused_extra_headers;
  HTTPCode status = send_request(RequestType::POST,
                                 path,
                                 body,
                                 response,
                                 username,
                                 trail,
                                 unused_extra_headers,
                                 &headers);
  return status;
}

/// Get data; return a HTTP return code
HTTPCode HttpConnection::send_get(const std::string& path,
                                  std::string& response,
                                  const std::string& username,
                                  SAS::TrailId trail)
{
  std::map<std::string, std::string> unused_rsp_headers;
  std::vector<std::string> unused_req_headers;
  return HttpConnection::send_get(path, unused_rsp_headers, response, username, unused_req_headers, trail);
}

/// Get data; return a HTTP return code
HTTPCode HttpConnection::send_get(const std::string& path,
                                  std::string& response,
                                  std::vector<std::string> headers,
                                  const std::string& override_server,
                                  SAS::TrailId trail)
{
  change_server(override_server);

  std::map<std::string, std::string> unused_rsp_headers;
  return HttpConnection::send_get(path, unused_rsp_headers, response, "", headers, trail);
}

/// Get data; return a HTTP return code
HTTPCode HttpConnection::send_get(const std::string& path,
                                  std::map<std::string, std::string>& headers,
                                  std::string& response,
                                  const std::string& username,
                                  SAS::TrailId trail)
{
  std::vector<std::string> unused_req_headers;
  return HttpConnection::send_get(path, headers, response, username, unused_req_headers, trail);
}

/// Get data; return a HTTP return code
HTTPCode HttpConnection::send_get(const std::string& path,
                                  std::map<std::string, std::string>& headers,
                                  std::string& response,
                                  const std::string& username,
                                  std::vector<std::string> headers_to_add,
                                  SAS::TrailId trail)
{
  return send_request(RequestType::GET,
                      path,
                      "",
                      response,
                      username,
                      trail,
                      headers_to_add,
                      NULL);
}

std::string HttpConnection::request_type_to_string(RequestType request_type)
{
  switch (request_type) {
  case RequestType::DELETE:
    return "DELETE";
  case RequestType::PUT:
    return "PUT";
  case RequestType::POST:
    return "POST";
  case RequestType::GET:
    return "GET";
  // LCOV_EXCL_START
  // The above cases are exhaustive by the definition of RequestType
  default:
    return "UNKNOWN";
  // LCOV_EXCL_STOP
  }
}

/// Get data; return a HTTP return code
HTTPCode HttpConnection::send_request(RequestType request_type,
                                      const std::string& path,
                                      std::string body,
                                      std::string& doc,
                                      const std::string& username,
                                      SAS::TrailId trail,
                                      std::vector<std::string> headers_to_add,
                                      std::map<std::string, std::string>* response_headers)
{
  HTTPCode http_code;
  CURLcode rc;

  // Create a UUID to use for SAS correlation.
  boost::uuids::uuid uuid = get_random_uuid();
  std::string uuid_str = boost::uuids::to_string(uuid);

  // Now log the marker to SAS. Flag that SAS should not reactivate the trail
  // group as a result of associations on this marker (doing so after the call
  // ends means it will take a long time to be searchable in SAS).
  SAS::Marker corr_marker(trail, MARKER_ID_VIA_BRANCH_PARAM, 0);
  corr_marker.add_var_param(uuid_str);
  SAS::report_marker(corr_marker, SAS::Marker::Scope::Trace, false);

  // Resolve the host.
  std::vector<AddrInfo> targets;
  _resolver->resolve(_host, _port, MAX_TARGETS, targets, trail);

  // If the list of targets only contains 1 target, clone it - we always want
  // to retry at least once.
  if (targets.size() == 1)
  {
    targets.push_back(targets[0]);
  }

  // Track the number of HTTP 503 and 504 responses and the number of timeouts
  // or I/O errors.
  int num_http_503_responses = 0;
  int num_http_504_responses = 0;
  int num_timeouts_or_io_errors = 0;

  // Track the IP addresses we're connecting to.  If we fail, we failed to
  // resolve the host, so default to that.
  const char *remote_ip = NULL;
  rc = CURLE_COULDNT_RESOLVE_HOST;
  http_code = HTTP_NOT_FOUND;

  std::vector<AddrInfo>::const_iterator target_it;

  for (target_it = targets.begin(); target_it != targets.end(); ++target_it)
  {
    // Get a curl handle and the associated pool entry
    ConnectionHandle<CURL*> conn_handle = _conn_pool.get_connection(*target_it);
    CURL* curl = conn_handle.get_connection();

    // Construct and add extra headers
    struct curl_slist* extra_headers = build_headers(headers_to_add,
                                                     _assert_user,
                                                     username,
                                                     uuid_str);

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, extra_headers);

    // Set general curl options
    set_curl_options_general(curl, body, doc);

    // Set response header curl options
    set_curl_options_response(curl, response_headers);

    // Set request-type specific curl options
    set_curl_options_request(curl, request_type);

    // Convert the target IP address into a string and fix up the URL.  It
    // would be nice to use curl_easy_setopt(CURL_RESOLVE) here, but its
    // implementation is incomplete.
    char buf[100];
    remote_ip = inet_ntop(target_it->address.af,
                          &target_it->address.addr,
                          buf,
                          sizeof(buf));

    std::string ip_url;
    if (target_it->address.af == AF_INET6)
    {
      ip_url = "http://[" + std::string(remote_ip) + "]:" + std::to_string(target_it->port) + path;
    }
    else
    {
      ip_url = "http://" + std::string(remote_ip) + ":" + std::to_string(target_it->port) + path;
    }

    // Set the curl target URL
    curl_easy_setopt(curl, CURLOPT_URL, ip_url.c_str());

    // Create and register an object to record the HTTP transaction.
    Recorder recorder;
    curl_easy_setopt(curl, CURLOPT_DEBUGDATA, &recorder);

    // Get the current timestamp before calling into curl.  This is because we
    // can't log the request to SAS until after curl_easy_perform has returned.
    // This could be a long time if the server is being slow, and we want to log
    // the request with the right timestamp.
    SAS::Timestamp req_timestamp = SAS::get_current_timestamp();

    // Send the request.
    std::string url = "http://" + _server + path;
    doc.clear();
    TRC_DEBUG("Sending HTTP request : %s (trying %s)", url.c_str(), remote_ip);
    rc = curl_easy_perform(curl);

    // If a request was sent, log it to SAS.
    std::string method_str = request_type_to_string(request_type);
    if (recorder.request.length() > 0)
    {
      sas_log_http_req(trail, curl, method_str, url, recorder.request, req_timestamp, 0);
    }

    // Log the result of the request.
    long http_rc = 0;
    if (rc == CURLE_OK)
    {
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_rc);
      sas_log_http_rsp(trail, curl, http_rc, method_str, url, recorder.response, 0);
      TRC_DEBUG("Received HTTP response: status=%d, doc=%s", http_rc, doc.c_str());
    }
    else
    {
      TRC_ERROR("%s failed at server %s : %s (%d) : fatal",
                url.c_str(), remote_ip, curl_easy_strerror(rc), rc);
      sas_log_curl_error(trail, remote_ip, target_it->port, method_str, url, rc, 0);
    }

    http_code = curl_code_to_http_code(curl, rc);

    // At this point, we are finished with the curl object, so it is safe to
    // free the headers
    curl_slist_free_all(extra_headers);

    // Update the connection recycling and retry algorithms.
    if ((rc == CURLE_OK) && !(http_rc >= 400))
    {
      // Success!
      _resolver->success(*target_it);
      break;
    }
    else
    {
      // If we failed to even to establish an HTTP connection, blacklist this IP
      // address.
      if (!(http_rc >= 400) &&
          (rc != CURLE_REMOTE_FILE_NOT_FOUND) &&
          (rc != CURLE_REMOTE_ACCESS_DENIED))
      {
        // The CURL connection should not be returned to the pool
        conn_handle.set_return_to_pool(false);

        _resolver->blacklist(*target_it);
      }
      else
      {
        _resolver->success(*target_it);
      }

      // Determine the failure mode and update the correct counter.
      bool fatal_http_error = false;

      if (http_rc >= 400)
      {
        if (http_rc == 503)
        {
          num_http_503_responses++;
        }
        // LCOV_EXCL_START fakecurl doesn't let us return custom return codes.
        else if (http_rc == 504)
        {
          num_http_504_responses++;
        }
        else
        {
          fatal_http_error = true;
        }
        // LCOV_EXCL_STOP
      }
      else if ((rc == CURLE_REMOTE_FILE_NOT_FOUND) ||
               (rc == CURLE_REMOTE_ACCESS_DENIED))
      {
        fatal_http_error = true;
      }
      else if ((rc == CURLE_OPERATION_TIMEDOUT) ||
               (rc == CURLE_SEND_ERROR) ||
               (rc == CURLE_RECV_ERROR))
      {
        num_timeouts_or_io_errors++;
      }

      // Decide whether to keep trying.
      if ((num_http_503_responses + num_timeouts_or_io_errors >= 2) ||
          (num_http_504_responses >= 1) ||
          fatal_http_error)
      {
        // Make a SAS log so that its clear that we have stopped retrying
        // deliberately.
        HttpErrorResponseTypes reason = fatal_http_error ?
                                        HttpErrorResponseTypes::Permanent :
                                        HttpErrorResponseTypes::Temporary;
        sas_log_http_abort(trail, reason, 0);
        break;
      }
    }
  }

  // Report to the resolver that the remaining records were not tested.
  ++target_it;
  while (target_it < targets.end())
  {
    _resolver->untested(*target_it);
    ++target_it;
  }

  // Check whether we should apply a penalty. We do this when:
  //  - both attempts return 503 errors, which means the downstream node is
  //    overloaded/requests to it are timeing.
  //  - the error is a 504, which means that the node downsteam of the node
  //    we're connecting to currently has reported that it is overloaded/was
  //    unresponsive.
  if (((num_http_503_responses >= 2) ||
       (num_http_504_responses >= 1)) &&
      (_load_monitor != NULL))
  {
    _load_monitor->incr_penalties();
  }

  // Get the current time in ms
  struct timespec tp;
  int rv = clock_gettime(CLOCK_MONOTONIC, &tp);
  assert(rv == 0);
  unsigned long now_ms = tp.tv_sec * 1000 + (tp.tv_nsec / 1000000);

  if (rc == CURLE_OK)
  {
    if (_comm_monitor)
    {
      // If both attempts fail due to overloaded downstream nodes, consider
      // it a communication failure.
      if (num_http_503_responses >= 2)
      {
        _comm_monitor->inform_failure(now_ms); // LCOV_EXCL_LINE - No UT for 503 fails
      }
      else
      {
        _comm_monitor->inform_success(now_ms);
      }
    }
  }
  else
  {
    if (_comm_monitor)
    {
      _comm_monitor->inform_failure(now_ms);
    }
  }

  if (((rc != CURLE_OK) && (rc != CURLE_REMOTE_FILE_NOT_FOUND)) || (http_code >= 400))
  {
    TRC_ERROR("cURL failure with cURL error code %d (see man 3 libcurl-errors) and HTTP error code %ld", (int)rc, http_code);  // LCOV_EXCL_LINE
  }

  return http_code;
}

struct curl_slist* HttpConnection::build_headers(std::vector<std::string> headers_to_add,
                                                 bool assert_user,
                                                 const std::string& username,
                                                 std::string uuid_str)
{
  struct curl_slist* extra_headers = NULL;
  extra_headers = curl_slist_append(extra_headers, "Content-Type: application/json");

  // Add the UUID for SAS correlation to the HTTP message.
  extra_headers = curl_slist_append(extra_headers,
                                    (SASEvent::HTTP_BRANCH_HEADER_NAME + ": " + uuid_str).c_str());

  // By default cURL will add `Expect: 100-continue` to certain requests. This
  // causes the HTTP stack to send 100 Continue responses, which messes up the
  // SAS call flow. To prevent this add an empty Expect header, which stops
  // cURL from adding its own.
  extra_headers = curl_slist_append(extra_headers, "Expect:");


  // Add in any extra headers
  for (std::vector<std::string>::const_iterator i = headers_to_add.begin();
       i != headers_to_add.end();
       ++i)
  {
    extra_headers = curl_slist_append(extra_headers, (*i).c_str());
  }

  // Add the user's identity (if required).
  if (assert_user)
  {
    extra_headers = curl_slist_append(extra_headers,
                                      ("X-XCAP-Asserted-Identity: " + username).c_str());
  }
  return extra_headers;
}

void HttpConnection::set_curl_options_general(CURL* curl,
                                              std::string body,
                                              std::string& doc)
{
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &doc);

  if (!body.empty())
  {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  }
}

void HttpConnection::set_curl_options_response(CURL* curl,
                               std::map<std::string, std::string>* response_headers)
{
  // If response_headers is not null, the headers returned by the curl request
  // should be stored there.
  if (response_headers)
  {
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, &HttpConnection::write_headers);
    curl_easy_setopt(curl, CURLOPT_WRITEHEADER, response_headers);
  }
}

void HttpConnection::set_curl_options_request(CURL* curl, RequestType request_type)
{
  switch (request_type)
  {
  case RequestType::DELETE:
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    break;
  case RequestType::PUT:
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    break;
  case RequestType::POST:
    curl_easy_setopt(curl, CURLOPT_POST, 1);
    break;
  case RequestType::GET:
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);
    break;
  }
}

/// cURL helper - write data into string.
size_t HttpConnection::string_store(void* ptr, size_t size, size_t nmemb, void* stream)
{
  ((std::string*)stream)->append((char*)ptr, size * nmemb);
  return (size * nmemb);
}

size_t HttpConnection::write_headers(void *ptr, size_t size, size_t nmemb, std::map<std::string, std::string> *headers)
{
  char* headerLine = reinterpret_cast<char *>(ptr);

  // convert to string
  std::string headerString(headerLine, (size * nmemb));

  std::string key;
  std::string val;

  // find colon
  size_t colon_loc = headerString.find(":");
  if (colon_loc == std::string::npos)
  {
    key = headerString;
    val = "";
  }
  else
  {
    key = headerString.substr(0, colon_loc);
    val = headerString.substr(colon_loc + 1, std::string::npos);
  }

  // Lowercase the key (for consistency) and remove spaces
  std::transform(key.begin(), key.end(), key.begin(), ::tolower);
  key.erase(std::remove_if(key.begin(), key.end(), ::isspace), key.end());
  val.erase(std::remove_if(val.begin(), val.end(), ::isspace), val.end());

  TRC_DEBUG("Received header %s with value %s", key.c_str(), val.c_str());
  (*headers)[key] = val;

  return size * nmemb;
}

void HttpConnection::cleanup_uuid(void *uuid_gen)
{
  delete (RandomUUIDGenerator*)uuid_gen; uuid_gen = NULL;
}

boost::uuids::uuid HttpConnection::get_random_uuid()
{
  // Get the factory from thread local data (creating it if it doesn't exist).
  RandomUUIDGenerator* uuid_gen;
  uuid_gen =
    (RandomUUIDGenerator*)pthread_getspecific(_uuid_thread_local);

  if (uuid_gen == NULL)
  {
    uuid_gen = new RandomUUIDGenerator();
    pthread_setspecific(_uuid_thread_local, uuid_gen);
  }

  // _uuid_gen_ is a pointer to a callable object that returns a UUID.
  return (*uuid_gen)();
}

void HttpConnection::sas_add_ip(SAS::Event& event, CURL* curl, CURLINFO info)
{
  char* ip;

  if (curl_easy_getinfo(curl, info, &ip) == CURLE_OK)
  {
    event.add_var_param(ip);
  }
  else
  {
    event.add_var_param("unknown"); // LCOV_EXCL_LINE FakeCurl cannot fail the getinfo call.
  }
}

void HttpConnection::sas_add_port(SAS::Event& event, CURL* curl, CURLINFO info)
{
  long port;

  if (curl_easy_getinfo(curl, info, &port) == CURLE_OK)
  {
    event.add_static_param(port);
  }
  else
  {
    event.add_static_param(0); // LCOV_EXCL_LINE FakeCurl cannot fail the getinfo call.
  }
}

void HttpConnection::sas_add_ip_addrs_and_ports(SAS::Event& event,
                                                CURL* curl)
{
  // Add the local IP and port.
  sas_add_ip(event, curl, CURLINFO_PRIMARY_IP);
  sas_add_port(event, curl, CURLINFO_PRIMARY_PORT);

  // Now add the remote IP and port.
  sas_add_ip(event, curl, CURLINFO_LOCAL_IP);
  sas_add_port(event, curl, CURLINFO_LOCAL_PORT);
}

void HttpConnection::sas_log_http_req(SAS::TrailId trail,
                                      CURL* curl,
                                      const std::string& method_str,
                                      const std::string& url,
                                      const std::string& request_bytes,
                                      SAS::Timestamp timestamp,
                                      uint32_t instance_id)
{
  if (_sas_log_level != SASEvent::HttpLogLevel::NONE)
  {
    int event_id = ((_sas_log_level == SASEvent::HttpLogLevel::PROTOCOL) ?
                    SASEvent::TX_HTTP_REQ : SASEvent::TX_HTTP_REQ_DETAIL);
    SAS::Event event(trail, event_id, instance_id);

    sas_add_ip_addrs_and_ports(event, curl);
    event.add_compressed_param(request_bytes, &SASEvent::PROFILE_HTTP);
    event.add_var_param(method_str);
    event.add_var_param(Utils::url_unescape(url));

    event.set_timestamp(timestamp);
    SAS::report_event(event);
  }
}

void HttpConnection::sas_log_http_rsp(SAS::TrailId trail,
                                      CURL* curl,
                                      long http_rc,
                                      const std::string& method_str,
                                      const std::string& url,
                                      const std::string& response_bytes,
                                      uint32_t instance_id)
{
  if (_sas_log_level != SASEvent::HttpLogLevel::NONE)
  {
    int event_id = ((_sas_log_level == SASEvent::HttpLogLevel::PROTOCOL) ?
                    SASEvent::RX_HTTP_RSP : SASEvent::RX_HTTP_RSP_DETAIL);
    SAS::Event event(trail, event_id, instance_id);

    sas_add_ip_addrs_and_ports(event, curl);
    event.add_static_param(http_rc);
    event.add_compressed_param(response_bytes, &SASEvent::PROFILE_HTTP);
    event.add_var_param(method_str);
    event.add_var_param(Utils::url_unescape(url));

    SAS::report_event(event);
  }
}

void HttpConnection::sas_log_http_abort(SAS::TrailId trail,
                                        HttpErrorResponseTypes reason,
                                        uint32_t instance_id)
{
  int event_id = ((_sas_log_level == SASEvent::HttpLogLevel::PROTOCOL) ?
                    SASEvent::HTTP_ABORT : SASEvent::HTTP_ABORT_DETAIL);
  SAS::Event event(trail, event_id, instance_id);
  event.add_static_param(static_cast<uint32_t>(reason));
  SAS::report_event(event);
}

void HttpConnection::sas_log_curl_error(SAS::TrailId trail,
                                        const char* remote_ip_addr,
                                        unsigned short remote_port,
                                        const std::string& method_str,
                                        const std::string& url,
                                        CURLcode code,
                                        uint32_t instance_id)
{
  if (_sas_log_level != SASEvent::HttpLogLevel::NONE)
  {
    int event_id = ((_sas_log_level == SASEvent::HttpLogLevel::PROTOCOL) ?
                    SASEvent::HTTP_REQ_ERROR : SASEvent::HTTP_REQ_ERROR_DETAIL);
    SAS::Event event(trail, event_id, instance_id);

    event.add_static_param(remote_port);
    event.add_static_param(code);
    event.add_var_param(remote_ip_addr);
    event.add_var_param(method_str);
    event.add_var_param(Utils::url_unescape(url));
    event.add_var_param(curl_easy_strerror(code));

    SAS::report_event(event);
  }
}

void HttpConnection::host_port_from_server(const std::string& server, std::string& host, int& port)
{
  std::string server_copy = server;
  Utils::trim(server_copy);
  size_t colon_idx;
  if (((server_copy[0] != '[') ||
       (server_copy[server_copy.length() - 1] != ']')) &&
      ((colon_idx = server_copy.find_last_of(':')) != std::string::npos))
  {
    host = server_copy.substr(0, colon_idx);
    port = stoi(server_copy.substr(colon_idx + 1));
  }
  else
  {
    host = server_copy;
    port = 0;
  }
}

std::string HttpConnection::host_from_server(const std::string& server)
{
  std::string host;
  int port;
  host_port_from_server(server, host, port);
  return host;
}

int HttpConnection::port_from_server(const std::string& server)
{
  std::string host;
  int port;
  host_port_from_server(server, host, port);
  return port;
}

// Changes the underlying server used by this connection. Use this when
// the HTTPConnection was created without a server (e.g.
// ChronosInternalConnection)
void HttpConnection::change_server(std::string override_server)
{
  _server = override_server;
  _host = host_from_server(override_server);
  _port = port_from_server(override_server);
}

// This function determines an appropriate absolute HTTP request timeout
// (in ms) given the target latency for requests that the downstream components
// will be using.
long HttpConnection::calc_req_timeout_from_latency(int latency_us)
{
  return std::max(1, (latency_us * TIMEOUT_LATENCY_MULTIPLIER) / 1000);
}

HttpConnection::Recorder::Recorder() {}

HttpConnection::Recorder::~Recorder() {}

int HttpConnection::Recorder::debug_callback(CURL *handle,
                                             curl_infotype type,
                                             char *data,
                                             size_t size,
                                             void *userptr)
{
  return ((Recorder*)userptr)->record_data(type, data, size);
}

int HttpConnection::Recorder::record_data(curl_infotype type,
                                          char* data,
                                          size_t size)
{
  switch (type)
  {
  case CURLINFO_HEADER_IN:
  case CURLINFO_DATA_IN:
    response.append(data, size);
    break;

  case CURLINFO_HEADER_OUT:
  case CURLINFO_DATA_OUT:
    request.append(data, size);
    break;

  default:
    break;
  }

  return 0;
}
