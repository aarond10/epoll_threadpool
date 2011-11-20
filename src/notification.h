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
#ifndef _EPOLL_THREADPOOL_NOTIFICATION_H_
#define _EPOLL_THREADPOOL_NOTIFICATION_H_

#include "eventmanager.h"
#include <glog/logging.h>

namespace epoll_threadpool {

/**
 * Simple pthread based notification object.
 * Similar to pthread_cond_t but it can only be fired once and it will
 * stay triggered once fired.
 */
class Notification {
 public:
  Notification() {
    pthread_mutex_init(&_mutex, 0);
    pthread_cond_init(&_cond, 0);
    _signaled = false;
  }
  virtual ~Notification() {
    pthread_mutex_destroy(&_mutex);
    pthread_cond_destroy(&_cond);
  }

  void signal() {
    pthread_mutex_lock(&_mutex);
    _signaled = true;
    pthread_cond_broadcast(&_cond);
    pthread_mutex_unlock(&_mutex);
  }
    
  bool tryWait(EventManager::WallTime when) {
    pthread_mutex_lock(&_mutex);
    if (_signaled) {
      pthread_mutex_unlock(&_mutex);
      return true;
    }
    struct timespec ts = { (int64_t)when, (when - (int64_t)when) * 1000000000 };
    int ret = pthread_cond_timedwait(&_cond, &_mutex, &ts);
    pthread_mutex_unlock(&_mutex);
    return ret == 0;
  }
  void wait() {
    pthread_mutex_lock(&_mutex);
    if (_signaled) {
      pthread_mutex_unlock(&_mutex);
      return;
    }
    int ret = pthread_cond_wait(&_cond, &_mutex);
    pthread_mutex_unlock(&_mutex);
  }
 protected:
  volatile bool _signaled;
  pthread_mutex_t _mutex;
  pthread_cond_t _cond;
};

/**
 * Similar to Notification but required a set number of calls to signal() 
 * before becoming 'signalled'.
 * TODO(aarond10): Fix terminology. Overloaded use of the word "signal".
 */
class CountingNotification : public Notification {
 public:
  /**
   * Creates a notification that will only be signalled after num calls to
   * signal().
   */
  CountingNotification(int num) : _num(num) { }
  virtual ~CountingNotification() { }

  void signal() {
    pthread_mutex_lock(&_mutex);
    if (--_num <= 0) {
      _signaled = true;
      pthread_cond_broadcast(&_cond);
    }
    pthread_mutex_unlock(&_mutex);
  }
 private:
  int _num;
};
}
#endif
