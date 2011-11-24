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

using std::tr1::function;

namespace {
inline int getEpollFlags(std::map<int, 
    std::map<EventManager::EventType, function<void()> > >& fds, int fd) {
  int flags = 0;
  if (fds.find(fd) == fds.end()) {
    return 0;
  }
  for (std::map<EventManager::EventType, 
      function<void()> >::iterator i = fds[fd].begin();
      i != fds[fd].end(); ++i) {
    switch (i->first) {
     case EventManager::EM_READ:
      flags |= EPOLLIN;
      break;
     case EventManager::EM_WRITE:
      flags |= EPOLLOUT;
      break;
     case EventManager::EM_ERROR:
      flags |= EPOLLRDHUP | EPOLLHUP;
      break;
     default:
      LOG(ERROR) << "Unknown event type " << i->first;
    };
  }
  return flags;
}
}  // anonymous namespace

EventManager::EventManager() : _is_running(false) {
  pthread_mutex_init(&_mutex, 0);
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
  close(_event_fd);
  close(_epoll_fd);
  _event_fd = -1;
  _epoll_fd = -1;
  pthread_mutex_destroy(&_mutex);
}

bool EventManager::start(int num_threads) {
  pthread_mutex_lock(&_mutex);

  // Tried to call start() from one of our worker threads? There lies madness.
  if (_thread_set.find(pthread_self()) != _thread_set.end()) {
    pthread_mutex_unlock(&_mutex);
    return false;
  }

  _is_running = true;
  for (int i = 0; i < num_threads; ++i) {
    pthread_t thread;
    pthread_create(&thread, NULL, trampoline, this);
    _thread_set.insert(thread);
  }
  pthread_mutex_unlock(&_mutex);
  return true;
}

bool EventManager::stop() {
  pthread_mutex_lock(&_mutex);

  // We don't allow a stop() call from one of our worker threads.
  if (_thread_set.find(pthread_self()) != _thread_set.end()) {
    pthread_mutex_unlock(&_mutex);
    return false;
  }

  _is_running = false;

  eventfd_write(_event_fd, 1);
  for (set<pthread_t>::const_iterator i = _thread_set.begin();
      i != _thread_set.end(); ++i) {
    pthread_mutex_unlock(&_mutex);
    pthread_join(*i, NULL);
    pthread_mutex_lock(&_mutex);
  }

  // Cancel any unprocessed tasks or fds.
  DLOG_IF(WARNING, !_fds.empty()) << "Stopping event manager with attached "
      << "file descriptors. You should consider calling removeFd first.";
  DLOG_IF(WARNING, !_tasks.empty()) << "Stopping event manager with pending "
      << "tasks.";
  _fds.clear();
  _tasks.clear();

  _thread_set.clear();
  pthread_mutex_unlock(&_mutex);
  return true;
}

EventManager::WallTime EventManager::currentTime() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec + tv.tv_usec/1000000.0;
}

void EventManager::enqueue(function<void()> f, WallTime when) {
  pthread_mutex_lock(&_mutex);
  Task t = { when, f };
  double oldwhen = _tasks.empty() ? -1 : _tasks.front().when;
  _tasks.push_back(t);
  push_heap(_tasks.begin(), _tasks.end(), &EventManager::compareTasks);
  // Do we need to wake up a worker to get this done on time?
  if (oldwhen != _tasks.front().when) {
    eventfd_write(_event_fd, 1);
  }
  pthread_mutex_unlock(&_mutex);
}

bool EventManager::watchFd(int fd, EventType type, function<void()> f) {
  pthread_mutex_lock(&_mutex);

  int epoll_op = 0;
  if (_fds.find(fd) != _fds.end()) {
    epoll_op = EPOLL_CTL_MOD;
  } else {
    epoll_op = EPOLL_CTL_ADD;
  }

  _fds[fd][type] = f;

  struct epoll_event ev;
  ev.data.u64 = 0;  // stop valgrind whinging
  ev.events = getEpollFlags(_fds, fd);
  ev.data.fd = fd;

  if (_fds.find(fd) != _fds.end()) {
    epoll_ctl(_epoll_fd, epoll_op, fd, &ev);
  } else {
    epoll_ctl(_epoll_fd, epoll_op, fd, &ev);
  }

  eventfd_write(_event_fd, 1);
  pthread_mutex_unlock(&_mutex);
  return true;
}

bool EventManager::removeFd(int fd, EventType type) {
  pthread_mutex_lock(&_mutex);

  if (_fds.find(fd) == _fds.end()) {
    pthread_mutex_unlock(&_mutex);
    return false;
  }

  _fds[fd].erase(type);

  struct epoll_event ev;
  ev.data.u64 = 0;  // stop valgrind whinging
  ev.events = getEpollFlags(_fds, fd);
  ev.data.fd = fd;

  if (ev.events != 0) {
    epoll_ctl(_epoll_fd, EPOLL_CTL_MOD, fd, &ev);
  } else {
    epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, fd, &ev);
    _fds.erase(fd);
  }

  pthread_mutex_unlock(&_mutex);
  return true;
}

void* EventManager::trampoline(void *arg) {
  EventManager *em = static_cast<EventManager *>(arg);
  em->thread_main();
  return NULL;
}

void EventManager::thread_main() {
  const int kMaxEvents = 32;
  const int kEpollDefaultWait = 10000;

  struct epoll_event events[kMaxEvents];
  pthread_mutex_lock(&_mutex);
  while (_is_running) {
    int timeout;
    if (!_tasks.empty()) {
      timeout = static_cast<int>((_tasks.front().when - currentTime())*1000);
      if (timeout < 0) {
        timeout = 0;
      }
    } else {
      timeout = kEpollDefaultWait;
    }
    pthread_mutex_unlock(&_mutex);
    int ret = epoll_wait(_epoll_fd, events, kMaxEvents, timeout);
    pthread_mutex_lock(&_mutex);

    if (ret < 0) {
      if (errno != EINTR) {
        LOG(ERROR) << "Epoll error: " << errno << " fd is " << _epoll_fd;
      }
      continue;
    }

    // Execute triggered fd handlers
    for (int i = 0; i < ret; i++) {
      int fd = events[i].data.fd;
      int flags = events[i].events;

      if (events[i].data.fd == _event_fd) {
        // just a thread wake-up event. consume and continue.
        uint64_t val;
        int ret = eventfd_read(_event_fd, &val);
      } else if (_fds.find(fd) != _fds.end()) {
        if ((flags | EPOLLOUT) && _fds[fd].find(EM_WRITE) != _fds[fd].end()) {
          function<void()> f = _fds[fd][EM_WRITE];
          pthread_mutex_unlock(&_mutex);
          f();
          pthread_mutex_lock(&_mutex);
        } 
        else if ((flags | EPOLLIN) && _fds[fd].find(EM_READ) != _fds[fd].end()) {
          function<void()> f = _fds[fd][EM_READ];
          pthread_mutex_unlock(&_mutex);
          f();
          pthread_mutex_lock(&_mutex);
        }
        else if ((flags | EPOLLHUP | EPOLLRDHUP) &&
                 _fds[fd].find(EM_ERROR) != _fds[fd].end()) {
          function<void()> f = _fds[fd][EM_ERROR];
          pthread_mutex_unlock(&_mutex);
          f();
          pthread_mutex_lock(&_mutex);
        }
      }
    }

    // Execute queued events that are due to be run.
    while (!_tasks.empty() && _tasks.front().when <= currentTime()) {
      Task t = _tasks.front();
      pop_heap(_tasks.begin(), _tasks.end(), &EventManager::compareTasks);
      _tasks.pop_back();
      pthread_mutex_unlock(&_mutex);
      t.f();
      pthread_mutex_lock(&_mutex);
    }
  }
  // wake up another thread - its likely we want to shut down.
  eventfd_write(_event_fd, 1); 
  pthread_mutex_unlock(&_mutex);
}

}
