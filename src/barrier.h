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
#ifndef _EPOLL_THREADPOOL_BARRIER_H_
#define _EPOLL_THREADPOOL_BARRIER_H_

#include "eventmanager.h"

#include <pthread.h>
#include <stdint.h>

#include <functional>
#include <tr1/memory>
#include <string>

namespace epoll_threadpool {

using std::tr1::bind;
using std::tr1::function;

/**
 * A simple class used to provide a synchronisation point. It can called as
 * a std::tr1::function<void()> a fixed number of times before triggering
 * a callback and destroying itself.
 */
class Barrier {
 private:
  pthread_mutex_t _mutex;
  int _n;
  function<void()> _callback;

 public:
  Barrier(int n, function<void()> callback)
      : _n(n), _callback(callback) {
    pthread_mutex_init(&_mutex, 0);
  }
  virtual ~Barrier() {
    pthread_mutex_destroy(&_mutex);
  }

  function<void()> callback() {
    return bind(&Barrier::signal, this);
  }
  /*operator function<void()>() {
    return bind(&Barrier::signal, this);
  }*/

 private:

  void signal() {
    pthread_mutex_lock(&_mutex);
    --_n;
    if (_n > 0) {
      pthread_mutex_unlock(&_mutex);
#ifdef NDEBUG
    } else if(_n < 0) {
      DLOG(ERROR) << "Barrier " << this 
                  << " called too many times (" << _n << ").";
#endif
    } else {
      pthread_mutex_unlock(&_mutex);
      _callback();
      delete this;
    }
  }

 private:

  Barrier(const Barrier& b); // No copying
  Barrier& operator=(const Barrier& b); // No copying
};

}

#endif
