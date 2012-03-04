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
#ifndef _EPOLL_THREADPOOL_TCP_H_
#define _EPOLL_THREADPOOL_TCP_H_

#include "eventmanager.h"
#include "iobuffer.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/socket.h>

#include <functional>
#include <tr1/memory>
#include <string>

namespace epoll_threadpool {

using std::string;
using std::tr1::bind;
using std::tr1::function;
using std::tr1::shared_ptr;

class TcpListenSocket;

/**
 * Represents a TCP data socket connected to some remote endpoint.
 *
 * An instance of this class can only be created internally by TcpListenSocket
 * in the case of incoming connections or by TcpSocket::connect for outgoing
 * connections. In both cases, a TcpSocket is in a connected state when
 * first passed to the user.
 *
 * setReceiveCallback and setDisconnectCallback methods can (and should) both
 * be set prior to using the socket. To avoid a potential race condition, a
 * TcpSocket will not start processing data until start() is called, giving the
 * user a chance to set up callbacks safely without missing potential events.
 *
 * After a sockets disconnect callback has been triggered, we guarantee that no
 * subsequent callbacks will be triggered and thus that the object can be
 * safely deleted (by reset()ing or otherwise letting shared_ptr's to the
 * object fall out of scope).
 *
 * TODO(aarond10): Confirm and add test cases for the following:
 * Both disconnect and receive callbacks are guaranteed to run on one of socket
 * EventManager's threads. As a general rule of thumb, these functions should
 * not block as the EventManager's thread pool is of limited size.
 *
 * The disconnect callback will NOT be run if a TcpSocket is disconnected at
 * the time of deletion. (i.e. dereferencing to zero)
 */
class TcpSocket {
 public:
  virtual ~TcpSocket();

  /**
   * Attempts to connect to the given host:port and return a TcpSocket for the
   * connection. If a connection cannot be made, returns NULL.
   */
  static shared_ptr<TcpSocket> connect(
      EventManager* em, string host, uint16_t port);

  /**
   * Called to begin the event handling.
   * This should be called exactly once. It is done explicitly to give
   * sockets a chance to set up event callbacks.
   */
  void start();

  /**
   * Writes data to the TCP stream.
   * This class takes ownership of the provided IOBuffer instance.
   */
  void write(IOBuffer* data);

  /**
   * Disconnects the socket endpoint. No further events will be triggered.
   */
  void disconnect();

  /**
   * Returns true if the socket is closed.
   */
  bool isDisconnected() const;

  /**
   * Registers a callback to be triggered on received data.
   * The callback is passed a pointer to an IOBuffer. This IOBuffer remains
   * the property of this class. The callback is free to consume data at will.
   */
  void setReceiveCallback(function<void(IOBuffer*)> callback);

  /**
   * Registes a callback to be triggered on disconnect.
   */
  void setDisconnectCallback(function<void()> callback);

  /**
   * Returns the eventmanager for this class.
   */
  EventManager* getEventManager() const { return _internal->_em; }

  /**
   * Returns the file descriptor for the socket.
   * @note This shouldn't be used directly. Its exposed for debugging.
   */
  int fd() const { return _internal->_fd; }

 private:
  // Because we need to preserve data structures until we can be sure they
  // aren't in use on worker threads, we store them separately.
  class Internal : public std::tr1::enable_shared_from_this<Internal> {
   public:
    Internal(EventManager* em, int fd);
    ~Internal();
    pthread_mutex_t _mutex;
    EventManager* _em;
    int _fd;
    int _maxReceiveSize;
    int _maxSendSize;
    volatile bool _isStarted;
    IOBuffer _recvBuffer;
    IOBuffer _sendBuffer;
    function<void(IOBuffer*)> _recvCallback;
    function<void()> _disconnectCallback;

    void start();
    void write(IOBuffer* data);
    void disconnect();
    void onReceive();
    void onCanSend();

    static void cleanup(shared_ptr<Internal> ptr) {
      ptr.reset();
    }
  };
  shared_ptr<Internal> _internal;

  friend class TcpListenSocket;
  TcpSocket(EventManager* em, int fd);

 private:
  // Bad constructors not implemented.
  TcpSocket(const TcpSocket&);
};

/**
 * Represents a TCP listening socket on a specific TCP port.
 *
 * When a connection is made to the socket, it will be accepted and a
 * shared_ptr<TcpSocket> passed to the registered accept callback.
 * If an accept callback has not been set, the TcpSocket will end up being
 * dereferenced to zero and deleted.
 *
 * It is the callback's responsibility to set up any event handlers on the
 * TcpSocket and then call its start() method to begin receiving events.
 */
class TcpListenSocket {
 public:
  virtual ~TcpListenSocket();

  /**
   * Attempts to create a new TcpListenSocket on a provided TCP port.
   * Uses the provided EventManager to handle the incoming connections.
   * Returns a pointer to a new TcpListenSocket object on success, NULL on
   * failure.
   */
  static shared_ptr<TcpListenSocket> create(EventManager* em, uint16_t port);

  /**
   * Registers a callback to be triggered when a new client connects.
   * The callback is passed a pointer to a new TcpSocket instance for the
   * connection. The callback inherits ownership of this socket.
   */
  void setAcceptCallback(function<void(shared_ptr<TcpSocket>)> callback);

  /**
   * Returns the eventmanager for this class.
   */
  EventManager* getEventManager() const { return _internal->_em; }

 private:
  // Because we need to preserve data structures until we can be sure they
  // aren't in use on worker threads, we store them separately.
  class Internal : public std::tr1::enable_shared_from_this<Internal> {
   public:
    Internal(EventManager* em, int fd);
    ~Internal();

    pthread_mutex_t _mutex;
    EventManager *_em;
    int _fd;
    function<void(shared_ptr<TcpSocket>)> _callback;

    void shutdown();
    void onAccept();

    static void cleanup(shared_ptr<Internal> ptr) {
      ptr.reset();
    }
  };
  shared_ptr<Internal> _internal;

  TcpListenSocket(EventManager* em, int fd);

 private:
  // Bad constructors not implemented.
  TcpListenSocket(const TcpListenSocket&);
};
}
#endif
