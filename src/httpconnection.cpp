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

#include "httpconnection.h"

const std::string HttpConnection::SCHEME_PREFIX = "http://";

HTTPCode HttpConnection::send_delete(const std::string& path,
                                     SAS::TrailId trail,
                                     const std::string& body)
{
  return _client.send_delete(SCHEME_PREFIX + _server + path,
                             trail,
                             body);
}

HTTPCode HttpConnection::send_delete(const std::string& path,
                                     SAS::TrailId trail,
                                     const std::string& body,
                                     std::string& response)
{
  return _client.send_delete(SCHEME_PREFIX + _server + path,
                             trail,
                             body,
                             response);
}

HTTPCode HttpConnection::send_delete(const std::string& path,
                                     std::map<std::string, std::string>& headers,
                                     std::string& response,
                                     SAS::TrailId trail,
                                     const std::string& body,
                                     const std::string& username)
{
  return _client.send_delete(SCHEME_PREFIX + _server + path,
                             headers,
                             response,
                             trail,
                             body,
                             username);
}

HTTPCode HttpConnection::send_put(const std::string& path,
                                  const std::string& body,
                                  SAS::TrailId trail,
                                  const std::string& username)
{
  return _client.send_put(SCHEME_PREFIX + _server + path,
                          body,
                          trail,
                          username);
}

HTTPCode HttpConnection::send_put(const std::string& path,
                                  std::string& response,
                                  const std::string& body,
                                  SAS::TrailId trail,
                                  const std::string& username)
{
  return _client.send_put(SCHEME_PREFIX + _server + path,
                          response,
                          body,
                          trail,
                          username);
}

HTTPCode HttpConnection::send_put(const std::string& path,
                                  std::map<std::string, std::string>& headers,
                                  const std::string& body,
                                  SAS::TrailId trail,
                                  const std::string& username)
{
  return _client.send_put(SCHEME_PREFIX + _server + path,
                          headers,
                          body,
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
  return _client.send_put(SCHEME_PREFIX + _server + path,
                          headers,
                          response,
                          body,
                          extra_req_headers,
                          trail,
                          username);
}

HTTPCode HttpConnection::send_post(const std::string& path,
                                   std::map<std::string, std::string>& headers,
                                   const std::string& body,
                                   SAS::TrailId trail,
                                   const std::string& username)
{
  return _client.send_post(SCHEME_PREFIX + _server + path,
                           headers,
                           body,
                           trail,
                           username);
}

HTTPCode HttpConnection::send_post(const std::string& path,
                                   std::map<std::string, std::string>& headers,
                                   std::string& response,
                                   const std::string& body,
                                   SAS::TrailId trail,
                                   const std::string& username)
{
  return _client.send_post(SCHEME_PREFIX + _server + path,
                           headers,
                           response,
                           body,
                           trail,
                           username);
}

/// Get data; return a HTTP return code
HTTPCode HttpConnection::send_get(const std::string& path,
                                  std::string& response,
                                  const std::string& username,
                                  SAS::TrailId trail)
{
  return _client.send_get(SCHEME_PREFIX + _server + path,
                          response,
                          username,
                          trail);
}

/// Get data; return a HTTP return code
HTTPCode HttpConnection::send_get(const std::string& path,
                                  std::map<std::string, std::string>& headers,
                                  std::string& response,
                                  const std::string& username,
                                  SAS::TrailId trail)
{
  return _client.send_get(SCHEME_PREFIX + _server + path,
                          headers,
                          response,
                          username,
                          trail);
}

/// Get data; return a HTTP return code
HTTPCode HttpConnection::send_get(const std::string& path,
                                  std::map<std::string, std::string>& headers,
                                  std::string& response,
                                  const std::string& username,
                                  std::vector<std::string> headers_to_add,
                                  SAS::TrailId trail)
{
  return _client.send_get(SCHEME_PREFIX + _server + path,
                          headers,
                          response,
                          username,
                          headers_to_add,
                          trail);
}

void HttpConnection::set_unix_socket(const std::string& unix_socket)
{
  _client.set_unix_socket(unix_socket);
}