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
#include "future.h"

#include "eventmanager.h"
#include "notification.h"

#include <gtest/gtest.h>
#include <string>

using epoll_threadpool::EventManager;
using epoll_threadpool::Future;
using epoll_threadpool::FutureBarrier;
using epoll_threadpool::Notification;
using std::string;

TEST(FutureTest, BasicSynchronous) {
  Future<string> f("apple");
  ASSERT_EQ(string("apple"), f.get());
}

TEST(FutureTest, TryGet) {
  Future<string> f;
  EventManager::WallTime t = EventManager::currentTime();
  ASSERT_FALSE(f.tryWait(t + 0.002));
  f.set(string("apple"));
  ASSERT_TRUE(f.tryWait(t + 0.002));
  ASSERT_EQ(string("apple"), (string)f);
}

void callbackHelper(Notification *n, const string &s) {
  ASSERT_EQ(string("apple"), s);
  n->signal();
}

TEST(FutureTest, AddCallback) {
  Future<string> f;
  Notification n1, n2, n3, n4;
  EventManager::WallTime t = EventManager::currentTime();
  f.addCallback(bind(&callbackHelper, &n1, std::tr1::placeholders::_1));
  f.addCallback(bind(&callbackHelper, &n2, std::tr1::placeholders::_1));
  f.set("apple");
  f.addCallback(bind(&callbackHelper, &n3, std::tr1::placeholders::_1));
  ASSERT_EQ(string("apple"), f.get());
  ASSERT_TRUE(n1.tryWait(t + 1));
  ASSERT_TRUE(n2.tryWait(t + 1));
  ASSERT_TRUE(n3.tryWait(t + 1));
  f.addCallback(bind(&callbackHelper, &n4, std::tr1::placeholders::_1));
  ASSERT_TRUE(n4.tryWait(t + 1));
}

TEST(FutureBarrierTest, TryGet) {
  Future<string> f1, f2, f3;

  FutureBarrier::FutureSet future_set;
  future_set.push_back(f1);
  future_set.push_back(f2);
  future_set.push_back(f3);
  FutureBarrier barrier(future_set);

  EventManager::WallTime t = EventManager::currentTime();

  ASSERT_FALSE(barrier.tryWait(t + 0.001));
  f1.set(string("apple"));
  ASSERT_FALSE(barrier.tryWait(t + 0.001));
  f2.set(string("banana"));
  ASSERT_FALSE(barrier.tryWait(t + 0.001));
  f3.set(string("carrot"));
  ASSERT_TRUE(barrier.tryWait(t + 0.004));
}

TEST(FutureBarrierTest, AddCallback) {
  Future<string> f1, f2, f3, f4;
  Notification n1, n2;

  FutureBarrier::FutureSet future_set;
  future_set.push_back(f1);
  future_set.push_back(f2);
  future_set.push_back(f3);
  FutureBarrier barrier(future_set);

  FutureBarrier::FutureSet future_set2;
  future_set2.push_back(f1);
  future_set2.push_back(f2);
  future_set2.push_back(f3);
  future_set2.push_back(f4);
  FutureBarrier barrier2(future_set2);

  EventManager::WallTime t = EventManager::currentTime();

  f1.set(string("apple"));
  f2.set(string("banana"));
  f3.set(string("carrot"));

  // Add Callback after all preconditions have been met.
  barrier.addCallback(bind(&Notification::signal, &n1));
  ASSERT_TRUE(n1.tryWait(t + 0.001));

  // Add Callback before all preconditions have been met.
  barrier2.addCallback(bind(&Notification::signal, &n2));
  f4.set(string("donut"));
  ASSERT_TRUE(n2.tryWait(t + 0.002));
}

TEST(FutureBarrierTest, TryErrBack) {
  Notification n1, n2;
  EventManager::WallTime t = EventManager::currentTime();

  FutureBarrier::FutureSet future_set;
  FutureBarrier barrier(future_set);
  { 
    Future<string> f1;
    future_set.push_back(f1);
    barrier.addCallback(bind(&Notification::signal, &n1));
    f1.addCallback(bind(&Notification::signal, &n2));
  }


  ASSERT_TRUE(n1.tryWait(t + 0.002));
  ASSERT_TRUE(n1.tryWait(t + 0.002));
}

