/*
 Copyright (c) 2011 Aaron Drew
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:
 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
 3. Neither the name of the copyright holders nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "eventmanager.h"

#include <glog/logging.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/time.h>
#include <unistd.h>

namespace epoll_threadpool {

using namespace std;
using std::tr1::function;

EventManager::EventManager() : _is_running(false) {
  _epoll_fd = epoll_create(64);

  // Set up and add eventfd to the epoll descriptor
  _event_fd = eventfd(0, 0);
  fcntl(_event_fd, F_SETFL, O_NONBLOCK);
  struct epoll_event ev;
  ev.data.u64 = 0;  // stop valgrind whinging
  ev.events = EPOLLIN;
  ev.data.fd = _event_fd;
  epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, _event_fd, &ev);
}

EventManager::~EventManager() {
  stop();

  struct epoll_event ev;
  ev.data.u64 = 0;  // stop valgrind whinging
  ev.events = EPOLLIN;
  ev.data.fd = _event_fd;
  epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, _event_fd, &ev);
  close(_event_fd);

  close(_epoll_fd);
}

bool EventManager::start(int num_threads) {
  lock_guard<mutex> lock(_mutex);

  // Tried to call start() from one of our worker threads? There lies madness.
  if (_thread_map.find(this_thread::get_id()) != _thread_map.end()) {
    return false;
  }

  _is_running = true;
  for (int i = 0; i < num_threads; ++i) {
    shared_ptr<thread> t(new thread(
        bind(&EventManager::thread_main, this)));
    _thread_map[t->get_id()] = t;
  }
  return true;
}

bool EventManager::stop() {
  lock_guard<mutex> lock(_mutex);

  // We don't allow a stop() call from one of our worker threads.
  if (_thread_map.find(this_thread::get_id()) != _thread_map.end()) {
    return false;
  }

  _is_running = false;

  eventfd_write(_event_fd, 1);
  for (map<thread::id, shared_ptr<thread> >::const_iterator i = 
           _thread_map.begin(); i != _thread_map.end(); ++i) {
    _mutex.unlock();
    i->second->join();
    _mutex.lock();
  }
  _thread_map.clear();

  // Cancel any unprocessed tasks or fds.
  DLOG_IF(WARNING, !_fds.empty()) << "Stopping event manager with attached "
      << "file descriptors. You should consider calling removeFd first.";
  DLOG_IF(WARNING, !_tasks.empty()) << "Stopping event manager with pending "
      << "tasks.";
  _fds.clear();
  _tasks.clear();
  return true;
}

void EventManager::enqueue(function<void()> f, Time when) {
  lock_guard<mutex> lock(_mutex);
  Task t = { when, f };
  Time oldwhen = 
      _tasks.empty() ? Time::min() : _tasks.front().when;
  _tasks.push_back(t);
  push_heap(_tasks.begin(), _tasks.end(), &EventManager::compareTasks);
  // Do we need to wake up a worker to get this done on time?
  if (oldwhen != _tasks.front().when) {
    eventfd_write(_event_fd, 1);
  }
}

bool EventManager::watchFd(int fd, EventType type, function<void()> f) {
  lock_guard<mutex> lock(_mutex);

  if (_fds.find(fd) == _fds.end()) {
    _fds[fd][type] = f;
    epollUpdate(fd, EPOLL_CTL_ADD);
  } else {
    _fds[fd][type] = f;
    epollUpdate(fd, EPOLL_CTL_MOD);
  }

  eventfd_write(_event_fd, 1);
  return true;
}

bool EventManager::removeFd(int fd, EventType type) {
  lock_guard<mutex> lock(_mutex);

  if (_fds.find(fd) == _fds.end()) {
    return false;
  }

  if (_fds[fd].size() == 1 &&
      _fds[fd].find(type) != _fds[fd].end()) {
    epollUpdate(fd, EPOLL_CTL_DEL);
    _fds.erase(fd);
  } else {
    _fds[fd].erase(type);
    epollUpdate(fd, EPOLL_CTL_MOD);
  }
  return true;
}

void EventManager::epollUpdate(int fd, int epoll_op) {
  struct epoll_event ev;
  ev.data.u64 = 0;  // stop valgrind whinging
  ev.events = 0;
  for (map<EventManager::EventType, 
      function<void()> >::iterator i = _fds[fd].begin();
      i != _fds[fd].end(); ++i) {
    switch (i->first) {
     case EventManager::EM_READ:
      ev.events |= EPOLLIN;
      break;
     case EventManager::EM_WRITE:
      ev.events |= EPOLLOUT;
      break;
     case EventManager::EM_ERROR:
      ev.events |= EPOLLRDHUP | EPOLLHUP;
      break;
     default:
      LOG(ERROR) << "Unknown event type " << i->first;
    };
  }
  ev.data.fd = fd;

  int r = epoll_ctl(_epoll_fd, epoll_op, fd, &ev);
  DLOG_IF(WARNING, r != 0) 
      << "epoll_ctl(" << _epoll_fd << ", " << epoll_op << ", " << fd 
      << ", &ev) returned error " << errno;
}

void EventManager::thread_main() {
  const int kMaxEvents = 32;
  const int kEpollDefaultWait = 10000;

  struct epoll_event events[kMaxEvents];
  lock_guard<mutex> lock(_mutex);
  while (_is_running) {
    int timeout;
    if (!_tasks.empty()) {
      timeout = duration_cast<milliseconds>(
          _tasks.front().when - currentTime()).count();
      if (timeout < 0) {
        timeout = 0;
      }
    } else {
      timeout = kEpollDefaultWait;
    }
    _mutex.unlock();
    int ret = epoll_wait(_epoll_fd, events, kMaxEvents, timeout);
    _mutex.lock();

    if (ret < 0) {
      if (errno != EINTR) {
        LOG(ERROR) << "Epoll error: " << errno << " fd is " << _epoll_fd;
      }
      continue;
    }

    // Execute triggered fd handlers
    for (int i = 0; i < ret; i++) {
      int fd = events[i].data.fd;
      if (fd == _event_fd) {
        uint64_t val;
        eventfd_read(_event_fd, &val);
      } else {
        int flags = events[i].events;
        if ((flags | EPOLLIN) && 
            _fds.find(fd) != _fds.end() &&
            _fds[fd].find(EM_READ) != _fds[fd].end()) {
          function<void()> f = _fds[fd][EM_READ];
          _mutex.unlock();
          f();
          _mutex.lock();
        }
        if ((flags | EPOLLOUT) && 
            _fds.find(fd) != _fds.end() &&
            _fds[fd].find(EM_WRITE) != _fds[fd].end()) {
          function<void()> f = _fds[fd][EM_WRITE];
          _mutex.unlock();
          f();
          _mutex.lock();
        } 
        if ((flags | EPOLLHUP | EPOLLRDHUP) &&
            _fds.find(fd) != _fds.end() &&
            _fds[fd].find(EM_ERROR) != _fds[fd].end()) {
          function<void()> f = _fds[fd][EM_ERROR];
          _mutex.unlock();
          f();
          _mutex.lock();
        }
      }
    }

    // Execute queued events that are due to be run.
    while (!_tasks.empty() && _tasks.front().when <= currentTime()) {
      Task t = _tasks.front();
      pop_heap(_tasks.begin(), _tasks.end(), &EventManager::compareTasks);
      _tasks.pop_back();
      _mutex.unlock();
      t.f();
      _mutex.lock();
    }
  }
  // wake up another thread - its likely we want to shut down.
  eventfd_write(_event_fd, 1); 
}
}
