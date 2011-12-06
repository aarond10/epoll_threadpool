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
#include "gtest/gtest.h"

#include <functional>
#include <string>

#include <stdlib.h>

#include "eventmanager.h"
#include "iobuffer.h"
#include "notification.h"
#include "tcp.h"

using epoll_threadpool::EventManager;
using epoll_threadpool::IOBuffer;
using epoll_threadpool::Notification;
using epoll_threadpool::TcpSocket;
using epoll_threadpool::TcpListenSocket;

using namespace std;
using namespace std::tr1;
using namespace std::tr1::placeholders;

shared_ptr<TcpListenSocket> createListenSocket(
    EventManager *em, int &port) {
  shared_ptr<TcpListenSocket> s;
  while(!s) {
    port = (rand()%40000) + 1024;
    s = TcpListenSocket::create(em, port);
  }
  return s;
}

// Code coverage - test basic creation and listening functionality.
TEST(TCPTest, ListenTest) {
  EventManager em;
  em.start(4);
  int port = 0;
  shared_ptr<TcpListenSocket> s = createListenSocket(&em, port);
  ASSERT_TRUE(s != NULL);
}

// Code coverage - test connect and failed connect code paths.
TEST(TCPTest, ListenConnectTest) {
  EventManager em;
  em.start(4);

  int port = 0;
  shared_ptr<TcpListenSocket> s = createListenSocket(&em, port);
  // TODO(aarond10): Add TcpListenSocket::start() to avoid a race issue?
  shared_ptr<TcpSocket> c = TcpSocket::connect(&em, "127.0.0.1", port);
  ASSERT_TRUE(s);
  ASSERT_TRUE(c);

  // Expect this to fail and return NULL. On occasions we will hit a port in 
  // use so we try a few times before failing
  bool found = false;
  for (int i = 1; i < 4 && !found; i++) {
    shared_ptr<TcpSocket> c2 = TcpSocket::connect(&em, "127.0.0.1", port+i);
    if (!c2) {
      found = true;
    }
  }
  ASSERT_TRUE(found);
}

void receiveChecker(Notification *n, string expected, IOBuffer *data) {
  uint64_t *psize = (uint64_t *)(data->pulldown(sizeof(uint64_t)));
  if (psize != NULL && *psize > 0 && data->size() >= *psize) {
    uint64_t size = *psize;
    ASSERT_TRUE(data->consume(sizeof(uint64_t)));
    const char *str = static_cast<const char *>(data->pulldown(size));
    ASSERT_TRUE(str != NULL);
    ASSERT_EQ(expected, string(str, size));
    data->consume(size);
    n->signal();
  }
}

void acceptHandler(Notification *n, string expected, shared_ptr<TcpSocket> *ps, shared_ptr<TcpSocket> s) {
  *ps = s;
  s->setReceiveCallback(std::tr1::bind(&receiveChecker, n, expected, _1));
  s->start();
}

// Code coverage - tests we can accept data from a newly connected client
TEST(TCPTest, ListenConnectSendDataTest) {
  EventManager em;
  em.start(1);

  EventManager::WallTime t = EventManager::currentTime();
  const char *data = "some test data";
  uint64_t size = strlen(data);

  int port = 0;
  shared_ptr<TcpListenSocket> s = createListenSocket(&em, port);

  Notification n;
  shared_ptr<TcpSocket> ps;
  s->setAcceptCallback(std::tr1::bind(&acceptHandler, &n, string(data), &ps, _1));

  shared_ptr<TcpSocket> c = TcpSocket::connect(&em, "127.0.0.1", port);
  ASSERT_TRUE(c != NULL);
  c->start();

  ASSERT_FALSE(n.tryWait(t+0.001));
  c->write(new IOBuffer((char *)&size, sizeof(size)));
  ASSERT_FALSE(n.tryWait(t+0.002));
  c->write(new IOBuffer(data, size));
  ASSERT_TRUE(n.tryWait(t+10.0));
  ASSERT_TRUE(ps != NULL);

  c->disconnect();
  ps->disconnect();
}

void disconnectChecker(Notification *n) {
  n->signal();
}

void acceptDisconnectHandler(Notification *n, shared_ptr<TcpSocket> *ps, shared_ptr<TcpSocket> s) {
  *ps = s;
  s->setDisconnectCallback(std::tr1::bind(&disconnectChecker, n));
  s->start();
}

// Code coverage - test disconnect handler.
TEST(TCPTest, ListenConnectDisconnectTest) {
  EventManager em;
  em.start(4);

  EventManager::WallTime t = EventManager::currentTime();
  const char *data = "some test data";
  uint64_t size = strlen(data);

  int port = 0;
  shared_ptr<TcpListenSocket> s = createListenSocket(&em, port);

  Notification n;
  shared_ptr<TcpSocket> ps;
  s->setAcceptCallback(std::tr1::bind(&acceptDisconnectHandler, &n, &ps, _1));
  //s->start();

  shared_ptr<TcpSocket> c = TcpSocket::connect(&em, "127.0.0.1", port);
  ASSERT_TRUE(c != NULL);

  ASSERT_FALSE(n.tryWait(t+0.001));
  c->disconnect();
  ASSERT_TRUE(n.tryWait(t+0.4));
  ASSERT_TRUE(ps != NULL);

  ps->disconnect();
}

void acceptConnectionCallback(shared_ptr<TcpSocket> s) {
  // This does nothing but because we don't store the TcpSocket, it will cause
  // it to get destroyed, immediately disconnecting the endpoint.
}

// Order of operations - write before start.
TEST(TCPTest, WriteBeforeStartedClient) {
  EventManager em;
  em.start(4);

  int port = 0;
  shared_ptr<TcpListenSocket> s = createListenSocket(&em, port);
  s->setAcceptCallback(std::tr1::bind(&acceptConnectionCallback, std::tr1::placeholders::_1));

  shared_ptr<TcpSocket> t(TcpSocket::connect(&em, "127.0.0.1", port));
  Notification n;
  t->setDisconnectCallback(std::tr1::bind(&Notification::signal, &n));
  t->write(new IOBuffer("abcd", 4));
  t->start();
  LOG(INFO) << "Print " << t->isDisconnected();
  n.wait();
}

void FillWriteBufferReceive(int *counter, Notification *n, IOBuffer *buf) {
  int s = buf->size();
  *counter -= s;
  buf->consume(s);
  CHECK_GE(*counter, 0);
  if (*counter == 0) {
    n->signal();
  }
}

// Consume data and count bytes.
void FillWriteBufferAccept(int *counter, Notification *n, shared_ptr<TcpSocket> *ps, shared_ptr<TcpSocket> s) {
  *ps = s;
  s->setReceiveCallback(std::tr1::bind(&FillWriteBufferReceive, 
      counter, n, std::tr1::placeholders::_1));
  s->start();
}

// Fill write buffer - Big write
TEST(TCPTest, FillWriteBuffer) {
  EventManager em;
  em.start(4);

  int counter = 0;
  Notification n;
  shared_ptr<TcpSocket> accept_sock;

  int port = 0;
  shared_ptr<TcpListenSocket> s = createListenSocket(&em, port);
  s->setAcceptCallback(std::tr1::bind(&FillWriteBufferAccept, &counter, &n, &accept_sock, std::tr1::placeholders::_1));
  shared_ptr<TcpSocket> t(TcpSocket::connect(&em, "127.0.0.1", port));

  const int kSize = 1024*1024;
  //const int kSize = 1024 * 10;
  counter = kSize;
  char *data = new char[kSize];
  for (int i = 0; i < kSize; ++i) {
    data[i] = i;
  }
  t->write(new IOBuffer(data, kSize));
  delete[] data;

  t->start();
  n.wait();
  t->disconnect();
}

// Order of operations - write after disconnect (no effect)
TEST(TCPTest, WriteAfterDisconnect) {
  EventManager em;
  em.start(4);

  int port = 0;
  shared_ptr<TcpListenSocket> s = createListenSocket(&em, port);
  shared_ptr<TcpSocket> t(TcpSocket::connect(&em, "127.0.0.1", port));

  Notification n;

  const char data[] = "testdata";
  t->setDisconnectCallback(std::tr1::bind(&Notification::signal, &n));
  t->start();
  n.wait();
  t->write(new IOBuffer(data, sizeof(data)));
}

// TODO: Order of operations - disconnect after disconnect
// TODO: Order of operations - start after disconnect
// TODO: Order of operations - disconnect from receive handler
// TODO: Order of operations - write from receive handler
// TODO: Destruction - destroy while receiving
// TODO: Destruction - destroy before start
// TODO: Destruction - destroy before disconnect
// TODO: Destruction - destroy after disconnect
