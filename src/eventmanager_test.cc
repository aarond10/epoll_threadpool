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
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "eventmanager.h"
#include "notification.h"

using epoll_threadpool::EventManager;
using epoll_threadpool::Notification;

TEST(EventManagerTest, StartStop) {
  EventManager em;

  ASSERT_TRUE(em.start(2));
  em.stop();
  ASSERT_TRUE(em.start(2));
  em.stop();
}

TEST(EventManagerTest, NotificationDelayedRaise) {
  EventManager em;
  Notification n;
  EventManager::WallTime t = EventManager::currentTime();

  ASSERT_TRUE(em.start(2));
  em.enqueue(std::tr1::bind(&Notification::signal, &n), t+0.001);
  ASSERT_TRUE(n.tryWait(t+0.500));
  em.stop();
}

TEST(EventManagerTest, NotificationPreDelayRaise) {
  EventManager em;
  Notification n;
  EventManager::WallTime t = EventManager::currentTime();

  ASSERT_TRUE(em.start(2));
  em.enqueue(std::tr1::bind(&Notification::signal, &n));
  usleep(10);
  ASSERT_TRUE(n.tryWait(t+0.500));
  em.stop();
}

TEST(EventManagerTest, StartEnqueueStop) {
  EventManager em;
  Notification n;
  EventManager::WallTime t = EventManager::currentTime();

  em.start(2);
  em.enqueue(std::tr1::bind(&Notification::signal, &n));
  ASSERT_TRUE(n.tryWait(t+0.500));
  em.stop();
}

TEST(EventManagerTest, StartEnqueueStop2) {
  EventManager em;
  Notification n;
  EventManager::WallTime t = EventManager::currentTime();

  ASSERT_TRUE(em.start(2));
  em.enqueue(std::tr1::bind(&Notification::signal, &n), t+0.001);
  n.wait();
  em.stop();
}

TEST(EventManagerTest, StartEnqueueStop3) {
  EventManager em;
  Notification n;
  EventManager::WallTime t = EventManager::currentTime();

  ASSERT_TRUE(em.start(2));
  em.enqueue(std::tr1::bind(&Notification::signal, &n), t+0.005);
  ASSERT_TRUE(n.tryWait(t+0.500));
  em.stop();
}

void EnqueuedCountCheck(pthread_mutex_t *mutex, int *cnt, Notification *n, int expected) {
  pthread_mutex_lock(mutex);
  EXPECT_EQ(expected, *cnt);
  (*cnt)++;
  if (n) {
    n->signal();
  }
  pthread_mutex_unlock(mutex);
}

TEST(EventManagerTest, StartEnqueueStop4) {
  EventManager em;
  Notification n;
  EventManager::WallTime t = EventManager::currentTime();

  pthread_mutex_t mutex;
  pthread_mutex_init(&mutex, 0);
  int cnt = 1;

  // Check double precision - should be ok but just to be safe...
  ASSERT_LT(t+0.001, t+0.002);
  ASSERT_LT(t+0.002, t+0.003);
  ASSERT_LT(t+0.003, t+0.004);
  ASSERT_LT(t+0.004, t+0.005);

  em.enqueue(std::tr1::bind(&EnqueuedCountCheck, &mutex, &cnt, &n, 5), t+0.005);
  em.enqueue(std::tr1::bind(&EnqueuedCountCheck, &mutex, &cnt, (Notification *)NULL, 4), t+0.004);
  em.enqueue(std::tr1::bind(&EnqueuedCountCheck, &mutex, &cnt, (Notification *)NULL, 2), t+0.002);
  em.enqueue(std::tr1::bind(&EnqueuedCountCheck, &mutex, &cnt, (Notification *)NULL, 1), t+0.001);
  em.enqueue(std::tr1::bind(&EnqueuedCountCheck, &mutex, &cnt, (Notification *)NULL, 3), t+0.003);

  // We start this AFTER adding the tasks to ensure we don't start one before
  // we've added them all. Note that we only start one thread because its 
  // otherwise possible we start two tasks at close to the same time and 
  // the second one runs first, leading to flaky tests.
  ASSERT_TRUE(em.start(1));
  ASSERT_TRUE(n.tryWait(t+0.500));
  em.stop();

  pthread_mutex_destroy(&mutex);
}

void EnqueuedAfterCheck(volatile bool *flag) {
  bool flagcpy = *flag;
  usleep(1000);
  EXPECT_EQ(*flag, flagcpy);
}

void EnqueuedAfterCheck2(volatile bool *flag, Notification *n) {
  *flag = true;
  if (n) {
    n->signal();
  }
}

void WorkerStartStop(EventManager *em) {
  ASSERT_FALSE(em->start(2));
  ASSERT_FALSE(em->stop());
}

TEST(EventManagerTest, CallMethodsFromWorkerThread) {
  EventManager em;
  Notification n;
  EventManager::WallTime t = EventManager::currentTime();

  em.enqueue(std::tr1::bind(&WorkerStartStop, &em));
  em.enqueue(std::tr1::bind(&Notification::signal, &n), t + 0.001);
  ASSERT_TRUE(em.start(2));
  ASSERT_TRUE(n.tryWait(t+0.500));
  em.stop();
}

void WatchFdRead(Notification *n, int fd) {
  char buf[9];
  ASSERT_EQ(9, ::read(fd, buf, 9));
  EXPECT_STREQ("testdata", buf);
  n->signal();
}

void WatchFdWrite(Notification *n, int fd, EventManager *em) {
  ASSERT_EQ(9, ::write(fd, "testdata", 9));
  // Tests removeFd() from worker thread
  ASSERT_TRUE(em->removeFd(fd, EventManager::EM_WRITE));
  n->signal();
}

TEST(EventManagerTest, WatchFdAndRemoveFdFromWorker) {
  EventManager em;
  Notification n, n2;
  EventManager::WallTime t = EventManager::currentTime();

  int fds[2];
  ASSERT_EQ(0, pipe2(fds, O_NONBLOCK));

  em.watchFd(fds[0], EventManager::EM_READ,
      std::tr1::bind(&WatchFdRead, &n, fds[0]));
  em.watchFd(fds[1], EventManager::EM_WRITE,
      std::tr1::bind(&WatchFdWrite, &n2, fds[1], &em));
  ASSERT_TRUE(em.start(1));
  ASSERT_TRUE(n.tryWait(t + 0.500));
  ASSERT_TRUE(n2.tryWait(t + 0.500));
  
  ASSERT_TRUE(em.removeFd(fds[0], EventManager::EM_READ));
  em.stop();
  close(fds[0]);
  close(fds[1]);
}

TEST(EventManagerTest, WatchFdConcurrentReadWrite) {
  EventManager em;
  Notification n, n2;
  EventManager::WallTime t = EventManager::currentTime();

  int fds[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));

  // Write test data to fds[1] so fds[0] has something to read.
  ASSERT_EQ(9, ::write(fds[1], "testdata", 9));

  em.watchFd(fds[0], EventManager::EM_READ,
      std::tr1::bind(&WatchFdRead, &n, fds[0]));
  em.watchFd(fds[0], EventManager::EM_WRITE,
      std::tr1::bind(&WatchFdWrite, &n2, fds[0], &em));
  ASSERT_TRUE(em.start(1));
  ASSERT_TRUE(n.tryWait(t + 0.500));
  ASSERT_TRUE(n2.tryWait(t + 0.500));
  
  ASSERT_TRUE(em.removeFd(fds[0], EventManager::EM_READ));
  em.stop();
  close(fds[0]);
  close(fds[1]);
}

void TestCancelA() {
  // We expect this to get called.
}
void TestCancelB() {
  // We expec this to get cancelled and never called.
  ASSERT_TRUE(false);
}

TEST(EventManagerTest, TestCancel) {
  EventManager em;
  Notification n;
  EventManager::WallTime t = EventManager::currentTime();

  em.enqueue(&TestCancelA);
  em.enqueue(&TestCancelA);

  EventManager::Function f(&TestCancelB);
  em.enqueue(f);
  f.cancel();

  em.enqueue(std::tr1::bind(&Notification::signal, &n));

  ASSERT_TRUE(em.start(1));
  ASSERT_TRUE(n.tryWait(t + 0.500));
  em.stop();
}


// TODO: Test watchFd() with two events firing one at a time.
// TODO: Test watchFd() with two events firing at the same time.

