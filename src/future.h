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
#ifndef _EPOLL_THREADPOOL_FUTURE_H_
#define _EPOLL_THREADPOOL_FUTURE_H_

#include "eventmanager.h"

#include <pthread.h>

#include <list>
#include <tr1/functional>
#include <tr1/memory>

namespace epoll_threadpool {

using std::tr1::function;
using std::tr1::shared_ptr;

/**
 * This class serves as a placeholder for a value. Its intended to be returned
 * in place of an actual value that is typically determined asynchronously.
 * The class allows both synchronous and asynchronous style programming 
 * constructs to be used. pthread code borrowed from Notification is currently
 * used to handle most of the synchronisation.
 * Internal state is reference counted so copies of the object should be
 * completely safe to use.
 * @see Notification
 */
template<class T>
class Future {
 public:
  Future() : _internal(new Internal()) { }
  explicit Future(const T &value) : _internal(new Internal()) {
    _internal->set(value);
  }
  explicit Future(T &value) : _internal(new Internal()) {
    _internal->set(value);
  }
  Future(const Future &other) {
    _internal = other._internal;
  }
  Future &operator=(Future &other) {
    _internal = other._internal;
  }
  virtual ~Future() { }

  operator const T&() {
    return get();
  }

  /**
   * Sets the return value. Ownership of 'value' is passed to the function.
   */
  bool set(T& value) {
    return _internal->set(value);
  }
  bool set(T value) {
    return _internal->set(value);
  }

  /**
   * Waits until we either have a value to return or 'when' is reached.
   * @returns true if value available, false otherwise.
   */
  bool tryWait(EventManager::WallTime when) {
    return _internal->tryWait(when);
  }

  /**
   * Returns the value, blocking if necessary until it becomes available.
   * @note Be careful of deadlocks if using this method. Consider using
   *       addCallback() instead.
   */
  const T& get() {
    return _internal->get();
  }

  /**
   * Registers a callback to get run when the Future's value is set.
   * If a callback is added after the value has been set, it will be
   * executed immediately.
   */
  void addCallback(function<void(const T&)> callback) {
    _internal->addCallback(callback);
  }

 private:
  class Internal {
   public:
    Internal() : _value(NULL), _signaled(false) {
      pthread_mutex_init(&_mutex, 0);
      pthread_cond_init(&_cond, 0);
    }
    virtual ~Internal() {
      pthread_mutex_destroy(&_mutex);
      pthread_cond_destroy(&_cond);
      delete _value;
    }

    bool set(T value) {
      pthread_mutex_lock(&_mutex);
      if (_signaled) {
	pthread_mutex_unlock(&_mutex);
	return false;
      } else {
	_value = new T(value);
	_signaled = true;
	pthread_cond_broadcast(&_cond);
	pthread_mutex_unlock(&_mutex);
	for (class std::list< function<void(const T&)> >::iterator i = 
	     _callbacks.begin(); i != _callbacks.end(); ++i) {
	  (*i)(*_value);
	}
      }
    }

    bool tryWait(EventManager::WallTime when) {
      pthread_mutex_lock(&_mutex);
      if (_signaled) {
	pthread_mutex_unlock(&_mutex);
	return true;
      }
      struct timespec ts = { (int64_t)when, 
			     (when - (int64_t)when) * 1000000000 };
      int r = pthread_cond_timedwait(&_cond, &_mutex, &ts);
      pthread_mutex_unlock(&_mutex);
      return (r == 0);
    }

    const T& get() {
      pthread_mutex_lock(&_mutex);
      if (_signaled) {
	pthread_mutex_unlock(&_mutex);
	return *_value;
      }
      int ret = pthread_cond_wait(&_cond, &_mutex);
      pthread_mutex_unlock(&_mutex);
      return *_value;
    }

    void addCallback(function<void(const T&)> callback) {
      pthread_mutex_lock(&_mutex);
      if (_signaled) {
	pthread_mutex_unlock(&_mutex);
	callback(*_value);
      } else {
	_callbacks.push_back(callback);
	pthread_mutex_unlock(&_mutex);
      }
    }

   private:
    T* _value;
    bool _signaled;
    pthread_mutex_t _mutex;
    pthread_cond_t _cond;
    std::list< function<void(const T&)> > _callbacks;
  };
  shared_ptr<Internal> _internal;
};

}

#endif
