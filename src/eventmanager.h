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
#ifndef _EPOLL_THREADPOOL_EVENTMANAGER_H_
#define _EPOLL_THREADPOOL_EVENTMANAGER_H_

#include <algorithm>
#include <list>
#include <map>
#include <set>
#include <tr1/functional>
#include <tr1/memory>
#include <vector>

#include <pthread.h>
#include <stdint.h>

namespace epoll_threadpool {

using namespace std;
using std::tr1::function;
using std::tr1::shared_ptr;

/**
 * Tiny epoll based event manager with a configurable pool of threads.
 * Can watch on sockets and execute functions on event threads.
 */
class EventManager {
 public:
  typedef double WallTime;

  /**
   * These represent the type of events a user can watch for on a file 
   * descriptor.
   * @see watchFd removeFd
   */
  enum EventType {
    EM_READ,
    EM_WRITE,
    EM_ERROR
  };

  /**
   * A simple std::tr1::function wrapper that adds the ability to cancel
   * a call if it hasn't already started. Internally reference counted
   * to make passing the function object around easy to do.
   */
  class Function {
   public:
    template<class T>
    Function(T f) : _internal(new Internal(f)) { }
    void cancel() {
      _internal->cancel();
    }
    void operator()() {
      _internal->run();
    }
   private:
    class Internal {
     public:
      Internal(function<void()> f) : _f(f) {
        pthread_mutex_init(&_mutex, 0);
      }
      ~Internal() {
        pthread_mutex_destroy(&_mutex);
      }
      void cancel() {
        pthread_mutex_lock(&_mutex);
        _f = NULL;
        pthread_mutex_unlock(&_mutex);
      }
      void run() {
        pthread_mutex_lock(&_mutex);
        function<void()> f(_f);
        pthread_mutex_unlock(&_mutex);
        if (f != NULL) {
          f();
        }
      }
     private:
      pthread_mutex_t _mutex;
      function<void()> _f;
    };
    shared_ptr<Internal> _internal;
  };

  EventManager();
  virtual ~EventManager();

  /**
   * Starts a number of worker threads for the EventManager.
   * This can be called repeatedly to start more threads as necessary.
   * This function will fail if called from an EventManager thread.
   * Returns true on success, false on failure.
   */
  bool start(int num_threads);

  /**
   * Gracefully shut down running worker threads.
   * This function will fail if called from an EventManager thread.
   * Returns true on success, false on failure.
   */
  bool stop();

  /**
   * Returns the current wall time in fractional seconds since epoch.
   */
  static WallTime currentTime();

  /**
   * Enqueues a function to be run on one of the EventManager's worker threads.
   * The function will be run on the first available thread.
   * It is safe to call this function from a worker thread itself.
   */
  void enqueue(Function f) {
    enqueue(f, currentTime());
  }

  /**
   * Enqueues a function to be run at a specified time on a worker thread.
   * The function will be run on the first available thread at or after the 
   * requested time.
   * It is safe to call this function from a worker thread itself.
   */
  void enqueue(Function f, WallTime when);

  /**
   * Watches for activity on a given file descriptor and triggers a callback 
   * when an event occurs. (fd, type) can be considered a tuple. To watch
   * a file descriptor for multiple events, you must call this function
   * multiple times. 
   * It is safe to call this function from a worker thread itself.
   */
  bool watchFd(int fd, EventType type, function<void()> f);
  
  /**
   * Stops triggering callbacks when a given event type occurs for a given FD.
   * The type argument should match that passed in to watchFd.
   * It is safe to call this function from a worker thread itself.
   */
  bool removeFd(int fd, EventType type);

 private:
  // Stores a scheduled task callback.
  struct Task {
    WallTime when;
    Function f;
  };
  // Used to sort heap with earliest time at the top
  static bool compareTasks(const Task&a, const Task& b) { 
    return a.when > b.when; 
  }

  /**
   * Helper that wraps and hides epoll_ctl calls, intended to make updating 
   * the epoll fd simpler.
   */
  void epollUpdate(int fd, int op);

  pthread_mutex_t _mutex;

  int _epoll_fd;
  int _event_fd;
  volatile bool _is_running;
  
  std::set<pthread_t> _thread_set;
  
  std::vector<Task> _tasks;
  std::map<int, std::map<EventType, function<void()> > > _fds;

  static void* trampoline(void *arg);
  void thread_main();
};
}

#endif
