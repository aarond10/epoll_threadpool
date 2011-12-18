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
using epoll_threadpool::Notification;
using std::string;

TEST(FutureTest, BasicSynchronous) {
  Future<string> f;
  f.set(new string("apple"));
  ASSERT_EQ(string("apple"), *f.get());
}

TEST(FutureTest, TryGet) {
  Future<string> f;
  const string *ret;
  EventManager::WallTime t = EventManager::currentTime();
  ASSERT_FALSE(f.tryGet(t + 0.002));
  f.set(new string("apple"));
  ASSERT_TRUE((ret = f.tryGet(t + 0.2)) != NULL);
  ASSERT_EQ(string("apple"), *ret);
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
  f.set(new string("apple"));
  f.addCallback(bind(&callbackHelper, &n3, std::tr1::placeholders::_1));
  ASSERT_EQ(string("apple"), *f.get());
  ASSERT_TRUE(n1.tryWait(t + 1));
  ASSERT_TRUE(n2.tryWait(t + 1));
  ASSERT_TRUE(n3.tryWait(t + 1));
  f.addCallback(bind(&callbackHelper, &n4, std::tr1::placeholders::_1));
  ASSERT_TRUE(n4.tryWait(t + 1));
}

