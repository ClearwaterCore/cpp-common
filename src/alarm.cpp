/**
 * @file alarm.cpp
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

#include <string.h>
#include <time.h>
#include <zmq.h>

#include <sstream>

#include "alarm.h"
#include "log.h"



AlarmReqAgent AlarmReqAgent::_instance;



Alarm::Alarm(const std::string& issuer, const std::string& identifier)
             : _issuer(issuer), _identifier(identifier)
{
}

void Alarm::issue()
{
  std::vector<std::string> req;

  req.push_back("issue-alarm");
  req.push_back(_issuer);
  req.push_back(_identifier);

  AlarmReqAgent::get_instance().alarm_request(req);

  LOG_DEBUG("%s issued %s alarm", _issuer.c_str(), _identifier.c_str());
}


void Alarm::clear_all(const std::string& issuer)
{
  std::vector<std::string> req;

  req.push_back("clear-alarms");
  req.push_back(issuer);

  AlarmReqAgent::get_instance().alarm_request(req);

  LOG_DEBUG("%s cleared its alarms", issuer.c_str());
}



AlarmPair::AlarmPair(const std::string& issuer,
                     const std::string& clear_alarm_id,
                     const std::string& set_alarm_id) :
  _alarmed(false)
{
  _clear_alarm.set_issuer(issuer);
  _clear_alarm.set_identifier(clear_alarm_id);

  _set_alarm.set_issuer(issuer);
  _set_alarm.set_identifier(set_alarm_id);
}


void AlarmPair::set()
{
  bool previously_alarmed = __sync_fetch_and_or(&_alarmed, true);

  if (! previously_alarmed)
  {
    _set_alarm.issue();
  }
}


void AlarmPair::clear()
{
  bool previously_alarmed = __sync_fetch_and_and(&_alarmed, false);

  if (previously_alarmed)
  {
    _clear_alarm.issue();
  }
}



bool AlarmReqAgent::start()
{
  if (! zmq_init_ctx())
  {
    return false;
  }

  int rc = pthread_create(&_thread, NULL, &agent_thread, (void*) &_instance);
  if (rc != 0)
  {
    LOG_ERROR("AlarmReqAgent: error creating thread %s", strerror(rc));

    zmq_clean_ctx();
    return false;
  }

  return true;
}


void AlarmReqAgent::stop()
{
  _req_q.terminate();

  zmq_clean_ctx();

  pthread_join(_thread, NULL);
}


void AlarmReqAgent::alarm_request(std::vector<std::string> req)
{
  if (! _req_q.push_noblock(req))
  {
    LOG_DEBUG("AlarmReqAgent: queue overflowed");
  }
}


void* AlarmReqAgent::agent_thread(void* alarm_req_agent)
{
  ((AlarmReqAgent*) alarm_req_agent)->agent();

  return NULL;
}


AlarmReqAgent::AlarmReqAgent() : _ctx(NULL), _sck(NULL)
{
}


bool AlarmReqAgent::zmq_init_ctx()
{
  _ctx = zmq_ctx_new();
  if (_ctx == NULL)
  {
    LOG_ERROR("AlarmReqAgent: zmq_ctx_new failed: %s", zmq_strerror(errno));
    return false;
  }

  return true;
}


bool AlarmReqAgent::zmq_init_sck()
{
  _sck = zmq_socket(_ctx, ZMQ_REQ);
  if (_sck == NULL)
  {
    LOG_ERROR("AlarmReqAgent: zmq_socket failed: %s", zmq_strerror(errno));
    return false;
  }

  int linger = 0;
  if (zmq_setsockopt(_sck, ZMQ_LINGER, &linger, sizeof(linger)) == -1)
  {
    LOG_ERROR("AlarmReqAgent: zmq_setsockopt failed: %s", zmq_strerror(errno));
    return false;
  }

  std::stringstream ss;
  ss << "tcp://127.0.0.1:" << ZMQ_PORT;

  if (zmq_connect(_sck, ss.str().c_str()) == -1)
  {
    LOG_ERROR("AlarmReqAgent: zmq_connect failed: %s", zmq_strerror(errno));
    return false;
  }

  return true;
}


void AlarmReqAgent::zmq_clean_ctx()
{
  if (_ctx)
  {
    if (zmq_ctx_destroy(_ctx) == -1)
    {
      LOG_ERROR("AlarmReqAgent: zmq_ctx_destroy failed: %s", zmq_strerror(errno));
    }

    _ctx = NULL;
  }
}


void AlarmReqAgent::zmq_clean_sck()
{
  if (_sck)
  {
    if (zmq_close(_sck) == -1)
    {
      LOG_ERROR("AlarmReqAgent: zmq_close failed: %s", zmq_strerror(errno));
    }

    _sck = NULL;
  }
}


void AlarmReqAgent::agent()
{
  if (! zmq_init_sck())
  {
    return;
  }
  
  std::vector<std::string> req;

  char reply[MAX_REPLY_LEN];

  while (_req_q.pop(req))
  {
    LOG_DEBUG("servicing request queue");

    for (std::vector<std::string>::iterator it = req.begin(); it != req.end(); it++)
    {
      if (zmq_send(_sck, it->c_str(), it->size(), ((it + 1) != req.end()) ? ZMQ_SNDMORE : 0) == -1)
      {
        if (errno != ETERM)
        {
          LOG_ERROR("AlarmReqAgent: zmq_sendmsg failed: %s", zmq_strerror(errno));
        }

        zmq_clean_sck();
        return;
      }
    }

    if (zmq_recv(_sck, &reply, sizeof(reply), 0) == -1)
    {
      if (errno != ETERM)
      {
        LOG_ERROR("AlarmReqAgent: zmq_recv failed: %s", zmq_strerror(errno));
      }

      zmq_clean_sck();
      return;
    }
  }

  zmq_clean_sck();
}

