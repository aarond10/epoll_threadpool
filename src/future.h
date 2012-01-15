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

#include <glog/logging.h>

#include <pthread.h>

#include <list>
#include <set>
#include <tr1/functional>
#include <tr1/memory>

namespace epoll_threadpool {

using std::tr1::bind;
using std::tr1::function;
using std::tr1::shared_ptr;

/**
 * A Future is a placeholder for an arbitrary return value that might not be
 * known until a later point in time.
 *
 * The user should be aware that there are several caveats to using this class.
 *   1. Access to the return value *may* incur a mutex lock overhead.
 *   2. Only types with copy constructors can be used because the class takes 
 *      a copy of the returned value to handle cases where addCallback() or 
 *      get() are called once the return value is out of scope.
 *   3. The return value is always const. Its possible to have multiple
 *      callbacks registered and non-const would be dangerous.
 *
 * Aside from these conditions, the user is free to use a cast Future in place
 * of its representive type. In such cases, the object behaves synchronously 
 * like a regualr return value. To use it asynchronously, simply call 
 * addCallback() on the return value instead and let it fall out of scope.
 * (Futures maintain a reference counted internal state and thus will clean
 * themselves up.)
 *
 * @see Notification
 */
template<class T>
class Future {
 public:
  Future() : _internal(new Internal()) { }
  Future(T value) : _internal(new Internal(new T(value))) { }
  Future(const Future &other) {  
    _internal = other._internal; 
  }
  Future &operator=(Future &other) { 
    if (&other == this) {
      return *this;
    }
    if (_internal->hasValue()) {
      LOG(ERROR) << "Future assigned another future's value but value "
                 << "has already been set.";
      return *this;
    }
    if (_internal->hasCallbacks()) {
      LOG(ERROR) << "Future assigned another future but we have "
                 << "pending callbacks for this one.";
    }
    _internal = other._internal;
    return *this;
  }
  virtual ~Future() { }

  operator const T&() { return get(); }

  /**
   * Sets the return value.
   */
  bool set(T value) { return _internal->set(new T(value)); }

  /**
   * Inherit the return value from another Future instance.
   */
  bool set(Future<T> &other) { *this = other; }

  /**
   * Waits until we either have a value to return or 'when' is reached.
   * @returns true if value available, false otherwise.
   */
  bool tryWait(EventManager::WallTime when) {
    return _internal->tryWait(when);
  }

  /**
   * Returns the value, blocking if necessary until it becomes available.
   */
  const T& get() {
    return _internal->get();
  }

  /**
   * Registers a callback to get run when the Future's value is set.
   * If a callback is added after the value has been set, it will be
   * executed immediately.
   * @note Callbacks registered here will run on either the calling
   *       thread or the thread that calls set().
   */
  void addCallback(function<void(const T&)> callback) {
    _internal->addCallback(callback);
  }

  /**
   * Deregisters a callback registered with addCallback() to ensure
   * it is never called after this function returns.
   */
  void removeCallback(function<void(const T&)> callback) {
    _internal->removeCallback(callback);
  }

  /**
   * Adds an "errback", a callback that is triggered if the Future
   * is destroyed without ever setting a value.
   */
  void addErrback(function<void()> errback) {
    _internal->addErrback(errback);
  }

  /**
   * Deregisters an errback.
   * @see addErrback
   */
  void removeErrback(function<void()> errback) {
    _internal->removeErrback(errback);
  }

 private:
  class Internal {
   public:
    Internal() : _value(NULL) {
      pthread_mutex_init(&_mutex, 0);
      pthread_cond_init(&_cond, 0);
    }
    Internal(T* value) : _value(value) {
      pthread_mutex_init(&_mutex, 0);
      pthread_cond_init(&_cond, 0);
    }
    virtual ~Internal() {
      if (_value == NULL) {
	for (std::list< function<void()> >::iterator i = 
	     _errbacks.begin(); i != _errbacks.end(); ++i) {
	  (*i)();
	}
      }
      pthread_mutex_destroy(&_mutex);
      pthread_cond_destroy(&_cond);
      delete _value;
    }

    bool hasValue() {
      return (_value != NULL);
    }

    bool hasCallbacks() {
      return (_callbacks.size() != 0);
    }

    bool set(T* value) {
      if (_value != NULL) {
        delete value;
        return false;
      }
      pthread_mutex_lock(&_mutex);
      if (_value != NULL) {
	pthread_mutex_unlock(&_mutex);
        delete value;
	return false;
      } else {
	_value = value;
	pthread_cond_broadcast(&_cond);
	pthread_mutex_unlock(&_mutex);
	for (class std::list< function<void(const T&)> >::iterator i = 
	     _callbacks.begin(); i != _callbacks.end(); ++i) {
	  (*i)(*_value);
	}
        // Must delete callbacks last as they may be holding references to us.
        _callbacks.clear();
        _errbacks.clear();
        return true;
      }
    }

    bool tryWait(EventManager::WallTime when) {
      if (_value != NULL) {
        return true;
      }
      pthread_mutex_lock(&_mutex);
      if (_value != NULL) {
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
      if (_value != NULL) {
        return *_value;
      }
      pthread_mutex_lock(&_mutex);
      if (_value != NULL) {
	pthread_mutex_unlock(&_mutex);
	return *_value;
      }
      int ret = pthread_cond_wait(&_cond, &_mutex);
      pthread_mutex_unlock(&_mutex);
      return *_value;
    }

    void addCallback(function<void(const T&)> callback) {
      if (_value != NULL) {
 	callback(*_value);
        return;
      }
      pthread_mutex_lock(&_mutex);
      if (_value != NULL) {
	pthread_mutex_unlock(&_mutex);
	callback(*_value);
      } else {
	_callbacks.push_back(callback);
	pthread_mutex_unlock(&_mutex);
      }
    }

    void removeCallback(function<void(const T&)> callback) {
      pthread_mutex_lock(&_mutex);
      _callbacks.erase(callback);
      pthread_mutex_unlock(&_mutex);
    }
      
    void addErrback(function<void()> errback) {
      pthread_mutex_lock(&_mutex);
      _errbacks.push_back(errback);
      pthread_mutex_unlock(&_mutex);
    }

    void removeErrback(function<void(const T&)> errback) {
      pthread_mutex_lock(&_mutex);
      _errbacks.erase(errback);
      pthread_mutex_unlock(&_mutex);
    }

   private:
    T* _value;
    pthread_mutex_t _mutex;
    pthread_cond_t _cond;
    std::list< function<void(const T&)> > _callbacks;
    std::list< function<void()> > _errbacks;

    Internal(const Internal& other);
    Internal& operator=(const Internal& other);
  };
  shared_ptr<Internal> _internal;
};

/**
 * This is a convenience object that combines a counter and a set of Future
 * objects to create a synchronisation barrier.
 *
 * To do this cleanly, we use a special FutureSet class and virtualisation
 * to allow us to store different Future types in the same set. As a result
 * of this, we are not able to retrieve the results of any of these Future
 * objects so its the users responsibility to keep a reference to
 * the Future's around if their results are needed. 
 *
 * @note This class expects to eventually be notified by all Futures.
 *       If it is deleted before all its Future's have returned a result, 
 *       your application *will* crash.
 */
class FutureBarrier {
 public:
  /**
   * Stores a set of arbitrary Future<T> instances.
   * These instances may be of different types so once added to a FutureSet
   * they can only ever be waited on. If the result is also required, a
   * copy of the original Future<T> should be kept elsewhere.
   */
  class FutureSet {
   public:
    FutureSet() { }
    virtual ~FutureSet() { }
    template<class T>
    void push_back(Future<T> &item) {
      _items.insert(shared_ptr<Item>(new ConcreteItem<T>(item)));
    }
    size_t size() const {
      return _items.size();
    }
   private:
    friend class FutureBarrier;
    /**
     * We use virtualisation to strip the type specialisation from
     * Future<T> instances for storage in a FutureSet.
     */
    class Item {
     public:
      virtual ~Item() {}
      virtual void addCallback(function<void()> callback) = 0;
      virtual bool tryWait(EventManager::WallTime when) = 0;
    };
    template<class T>
    class ConcreteItem : public Item {
     public:
      ConcreteItem(Future<T> &val) : _val(val) { }
      virtual ~ConcreteItem() { }
      virtual void addCallback(function<void()> callback) {
        // Note that we register both a callback and an errback here
        // so the provided callback is triggered exactly once regardless 
        // of whether or not the Future is ever set.
        _val.addCallback(bind(&addCallbackHelper, callback, 
                              std::tr1::placeholders::_1));
        _val.addErrback(callback);
      }
      virtual bool tryWait(EventManager::WallTime when) {
        return _val.tryWait(when);
      }
     private:
      // Helper that discards argument on addCallback() callback.
      static void addCallbackHelper(function<void()> callback, const T&) {
        callback();
      }
      Future<T> _val;
    };
    set< shared_ptr<Item> > _items;
  };

  FutureBarrier(FutureSet &future_set) {
    pthread_mutex_init(&_mutex, 0);
    pthread_cond_init(&_cond, 0);
    _counter = future_set.size();
    for (set< shared_ptr<FutureSet::Item> >::iterator i = 
             future_set._items.begin();
         i != future_set._items.end(); ++i) {
      (*i)->addCallback(bind(&FutureBarrier::signal, this));
    }
  }
    
  ~FutureBarrier() {
    pthread_mutex_destroy(&_mutex);
    pthread_cond_destroy(&_cond);
  }
    
  void addCallback(function<void()> callback) {
    _callbacks.push_back(callback);
    if (!_counter) {
      callback();
      return;
    }
    pthread_mutex_lock(&_mutex);
    if (!_counter) {
      pthread_mutex_unlock(&_mutex);
      callback();
    } else {
      _callbacks.push_back(callback);
      pthread_mutex_unlock(&_mutex);
    }
  }

  bool tryWait(EventManager::WallTime when) {
    if (!_counter) {
      return true;
    }
    pthread_mutex_lock(&_mutex);
    if (!_counter) {
      pthread_mutex_unlock(&_mutex);
      return true;
    }
    struct timespec ts = { (int64_t)when, 
                           (when - (int64_t)when) * 1000000000 };
    int r = pthread_cond_timedwait(&_cond, &_mutex, &ts);
    pthread_mutex_unlock(&_mutex);
    return (r == 0);
  }
 private:
  void signal() {
    pthread_mutex_lock(&_mutex);
    if (!--_counter) {
      pthread_cond_broadcast(&_cond);
      pthread_mutex_unlock(&_mutex);
      for (std::list< function<void()> >::iterator i = 
           _callbacks.begin(); i != _callbacks.end(); ++i) {
        (*i)();
      }
      return;
    } else {
      pthread_mutex_unlock(&_mutex);
    }
  }

  std::list< function<void()> > _callbacks;
  int _counter;
  pthread_mutex_t _mutex;
  pthread_cond_t _cond;

  // Illegal operators
  FutureBarrier(FutureBarrier &other);
  FutureBarrier& operator=(FutureBarrier &other);
};

}

#endif
