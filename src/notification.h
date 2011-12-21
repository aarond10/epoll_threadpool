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

#include <condition_variable>
#include <mutex>

namespace epoll_threadpool {

/**
 * Simple Notification object consisting of a mutex and condition variable.
 * This is intended to be a fire once, non-reusable convenience class.
 */
class Notification {
 public:
  Notification() : _signaled(false) {
  }
  virtual ~Notification() {
  }

  /**
   * Triggers any blocking waits to unblock and all future calls to
   * wait() or tryWait() to immediately return.
   */
  void signal() {
    if (!_signaled) {
      std::lock_guard<std::mutex> lock(_mutex);
      _signaled = true;
      _cond.notify_all();
    }
  }
    
  /**
   * Waits until "when" for a Notification to be signaled.
   * @returns true if signaled, false if timed out.
   */
  bool tryWait(EventManager::Time when) {
    if (_signaled) {
      return true;
    } else {
      std::unique_lock<std::mutex> ulock(_mutex);
      _cond.wait_until(ulock, when);
      return _signaled;
    }
  }
  /**
   * Waits (potentially indefinitely) for Notification::signal() to be called.
   */
  void wait() {
    if (!_signaled) {
      std::unique_lock<std::mutex> ulock(_mutex);
      _cond.wait(ulock);
    }
  }
 protected:
  bool _signaled;
  std::mutex _mutex;
  std::condition_variable _cond;
};

}
#endif
