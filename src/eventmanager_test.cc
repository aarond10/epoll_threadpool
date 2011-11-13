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
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>

#include <gtest/gtest.h>

#include "eventmanager.h"
#include "notification.h"

TEST(EventManagerTest, StartStop) {
  rpc::EventManager em;

  ASSERT_TRUE(em.start(10));
  em.stop();
  ASSERT_TRUE(em.start(10));
  em.stop();
  ASSERT_TRUE(em.start(10));
}

TEST(EventManagerTest, NotificationDelayedRaise) {
  rpc::EventManager em;
  rpc::Notification n;
  rpc::EventManager::WallTime t = rpc::EventManager::currentTime();

  ASSERT_TRUE(em.start(10));
  em.enqueue(std::tr1::bind(&rpc::Notification::signal, &n), t+0.001);
  ASSERT_TRUE(n.tryWait(t+0.500));
  em.stop();
}

TEST(EventManagerTest, NotificationPreDelayRaise) {
  rpc::EventManager em;
  rpc::Notification n;
  rpc::EventManager::WallTime t = rpc::EventManager::currentTime();

  ASSERT_TRUE(em.start(10));
  em.enqueue(std::tr1::bind(&rpc::Notification::signal, &n));
  usleep(10);
  ASSERT_TRUE(n.tryWait(t+0.500));
  em.stop();
}

TEST(EventManagerTest, StartEnqueueStop) {
  rpc::EventManager em;
  rpc::Notification n;
  rpc::EventManager::WallTime t = rpc::EventManager::currentTime();

  em.start(4);
  em.enqueue(std::tr1::bind(&rpc::Notification::signal, &n));
  ASSERT_TRUE(n.tryWait(t+0.500));
  em.stop();
}

TEST(EventManagerTest, StartEnqueueStop2) {
  rpc::EventManager em;
  rpc::Notification n;
  rpc::EventManager::WallTime t = rpc::EventManager::currentTime();

  em.start(8);
  em.enqueue(std::tr1::bind(&rpc::Notification::signal, &n), t+0.001);
  n.wait();
  em.stop();
}

TEST(EventManagerTest, StartEnqueueStop3) {
  rpc::EventManager em;
  rpc::Notification n;
  rpc::EventManager::WallTime t = rpc::EventManager::currentTime();

  em.start(8);
  em.enqueue(std::tr1::bind(&rpc::Notification::signal, &n), t+0.020);
  ASSERT_TRUE(n.tryWait(t+0.500));
  em.stop();
}

void EnqueuedCountCheck(pthread_mutex_t *mutex, int *cnt, rpc::Notification *n, int expected) {
  pthread_mutex_lock(mutex);
  EXPECT_EQ(expected, *cnt);
  (*cnt)++;
  if (n) {
    n->signal();
  }
  pthread_mutex_unlock(mutex);
}

TEST(EventManagerTest, StartEnqueueStop4) {
  rpc::EventManager em;
  rpc::Notification n;
  rpc::EventManager::WallTime t = rpc::EventManager::currentTime();

  pthread_mutex_t mutex;
  pthread_mutex_init(&mutex, 0);
  int cnt = 1;

  // Check double precision - should be ok but just to be safe...
  ASSERT_LT(t+0.001, t+0.002);
  ASSERT_LT(t+0.002, t+0.003);
  ASSERT_LT(t+0.003, t+0.004);
  ASSERT_LT(t+0.004, t+0.005);

  em.enqueue(std::tr1::bind(&EnqueuedCountCheck, &mutex, &cnt, &n, 5), t+0.005);
  em.enqueue(std::tr1::bind(&EnqueuedCountCheck, &mutex, &cnt, (rpc::Notification *)NULL, 4), t+0.004);
  em.enqueue(std::tr1::bind(&EnqueuedCountCheck, &mutex, &cnt, (rpc::Notification *)NULL, 2), t+0.002);
  em.enqueue(std::tr1::bind(&EnqueuedCountCheck, &mutex, &cnt, (rpc::Notification *)NULL, 1), t+0.001);
  em.enqueue(std::tr1::bind(&EnqueuedCountCheck, &mutex, &cnt, (rpc::Notification *)NULL, 3), t+0.003);

  // We start this AFTER adding the tasks to ensure we don't start one before we've added them all.
  em.start(1);
  ASSERT_TRUE(n.tryWait(t+0.500));
  em.stop();

  pthread_mutex_destroy(&mutex);
}

void EnqueuedAfterCheck(volatile bool *flag) {
  bool flagcpy = *flag;
  usleep(1000);
  EXPECT_EQ(*flag, flagcpy);
}

void EnqueuedAfterCheck2(volatile bool *flag, rpc::Notification *n) {
  *flag = true;
  if (n) {
    n->signal();
  }
}

void WorkerStartStop(rpc::EventManager *em) {
  ASSERT_FALSE(em->start(4));
  ASSERT_FALSE(em->stop());
}

TEST(EventManagerTest, CallMethodsFromWorkerThread) {
  rpc::EventManager em;
  rpc::Notification n;
  rpc::EventManager::WallTime t = rpc::EventManager::currentTime();

  em.enqueue(std::tr1::bind(&WorkerStartStop, &em));
  em.enqueue(std::tr1::bind(&rpc::Notification::signal, &n), t + 0.001);
  ASSERT_TRUE(em.start(1));
  ASSERT_TRUE(n.tryWait(t+0.500));
  em.stop();
}

