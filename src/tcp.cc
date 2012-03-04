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
#include "tcp.h"
#include "notification.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <glog/logging.h>

namespace epoll_threadpool {

using std::tr1::function;
using std::tr1::bind;

TcpSocket::TcpSocket(EventManager* em, int fd)
    : _internal(new Internal(em, fd)) {
}

TcpSocket::~TcpSocket() {
  _internal->_disconnectCallback = NULL;
  _internal->disconnect();
}

TcpSocket::Internal::Internal(EventManager* em, int fd)
    : _em(em), _fd(fd), _isStarted(false) {
  pthread_mutex_init(&_mutex, 0);
  fcntl(_fd, F_SETFL, O_NONBLOCK);
  int err;
  socklen_t size = sizeof(_maxSendSize); 
  if ((err = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, 
                        (char*) &_maxSendSize, &size)) != 0) {
    LOG(WARNING) << "Unable to determine maximum send size. Assuming 4k.";
    _maxSendSize = 4096;
  }
  size = sizeof(_maxReceiveSize); 
  if ((err = getsockopt(fd, SOL_SOCKET, SO_RCVBUF, 
                        (char*) &_maxReceiveSize, &size)) != 0) {
    LOG(WARNING) << "Unable to determine maximum receive size. Assuming 4k.";
    _maxReceiveSize = 4096;
  }
}

TcpSocket::Internal::~Internal() {
  disconnect();
  pthread_mutex_destroy(&_mutex);
}

void TcpSocket::start() {
  _internal->start();
}

void TcpSocket::Internal::start() {
  pthread_mutex_lock(&_mutex);
  if (!_isStarted) {
    _isStarted = true;
    if (_fd > 0) {
      _em->watchFd(_fd, EventManager::EM_READ,
          bind(&TcpSocket::Internal::onReceive, shared_from_this()));
      _em->watchFd(_fd, EventManager::EM_WRITE,
          bind(&TcpSocket::Internal::onCanSend, shared_from_this()));

      // We trigger the receive handler to handle the case where we got
      // disconnected before start() completed.
      _em->enqueue(
          bind(&TcpSocket::Internal::onReceive, shared_from_this()));
      _em->enqueue(
          bind(&TcpSocket::Internal::onCanSend, shared_from_this()));
    } else {
      DLOG(INFO) << "disconnected in start()";
      if (_disconnectCallback) {
        _em->enqueue(_disconnectCallback);
        _disconnectCallback = NULL;
      }
    }
  }
  pthread_mutex_unlock(&_mutex);
}

shared_ptr<TcpSocket> TcpSocket::connect(
    EventManager* em, string host, uint16_t port) {
  struct sockaddr_in sa;
  int fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

  if (fd == -1) {
    LOG(ERROR) << "can not create socket";
    return shared_ptr<TcpSocket>();
  }

  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  if (inet_pton(AF_INET, host.c_str(), &sa.sin_addr) <= 0) {
    LOG(ERROR) << "Failed to resolve address: " << host;
    close(fd);
    return shared_ptr<TcpSocket>();
  }

  if (::connect(fd, (struct sockaddr*)&sa, sizeof(sa)) == -1) {
    close(fd);
    return shared_ptr<TcpSocket>();
  }

  return shared_ptr<TcpSocket>(new TcpSocket(em, fd));
}

void TcpSocket::write(IOBuffer* data) {
  _internal->write(data);
}

void TcpSocket::disconnect() {
  _internal->disconnect();
}

bool TcpSocket::isDisconnected() const {
  return _internal->_fd == -1;
}

void TcpSocket::setReceiveCallback(function<void(IOBuffer*)> callback) {
  pthread_mutex_lock(&_internal->_mutex);
  _internal->_recvCallback = callback;
  pthread_mutex_unlock(&_internal->_mutex);
}

void TcpSocket::setDisconnectCallback(function<void()> callback) {
  pthread_mutex_lock(&_internal->_mutex);
  _internal->_disconnectCallback = callback;
  pthread_mutex_unlock(&_internal->_mutex);
}

void TcpSocket::Internal::write(IOBuffer* data) {
  pthread_mutex_lock(&_mutex);
  bool wasBufferEmpty = (_sendBuffer.size() == 0);
  _sendBuffer.append(data);
  if (_fd >= 0 && _isStarted && wasBufferEmpty) {
    _em->watchFd(_fd, EventManager::EM_WRITE,
      bind(&TcpSocket::Internal::onCanSend, shared_from_this()));
  }
  pthread_mutex_unlock(&_mutex);
}

void TcpSocket::Internal::disconnect() {
  pthread_mutex_lock(&_mutex);
  if (_fd > 0) {
    _em->removeFd(_fd, EventManager::EM_READ);
    _em->removeFd(_fd, EventManager::EM_WRITE);
    _em->removeFd(_fd, EventManager::EM_ERROR);
    ::shutdown(_fd, SHUT_RDWR);
    ::close(_fd);
    _fd = -1;
    if (_disconnectCallback) {
      _em->enqueue(_disconnectCallback);
      _disconnectCallback = NULL;
    }
  }
  pthread_mutex_unlock(&_mutex);
}

void TcpSocket::Internal::onReceive() {
  pthread_mutex_lock(&_mutex);
  while (_fd > 0) {
    vector<char>* buf = new vector<char>();
    buf->resize(_maxReceiveSize);

    int r = ::recv(_fd, &(*buf)[0], buf->size(), 0);
    if (r > 0) {
      buf->resize(r);
      _recvBuffer.append(buf);
      if (_recvCallback) {
        _recvCallback(&_recvBuffer);
      }
    } else {
      if (r < 0) {
        delete buf;
        if (errno != EAGAIN) {
          LOG(INFO) << "Read error. (" << errno << "). Disconnecting fd " << _fd;
          _em->enqueue(bind(
              &TcpSocket::Internal::disconnect, shared_from_this()));
        }
      } else {
        delete buf;
        _em->enqueue(bind(
            &TcpSocket::Internal::disconnect, shared_from_this()));
      }
      break;
    }
  }
  pthread_mutex_unlock(&_mutex);
}

void TcpSocket::Internal::onCanSend() {
  pthread_mutex_lock(&_mutex);
  if (_fd > 0) {
    while (_sendBuffer.size()) {
      int sz = (_sendBuffer.size() > _maxSendSize) ? 
                   _maxSendSize : _sendBuffer.size();
      const char* buf = _sendBuffer.pulldown(sz);
      if (buf) {
        int r = ::send(_fd, buf, sz, MSG_NOSIGNAL);
        if (r > 0) {
          _sendBuffer.consume(r);
        } else if (r < 0) {
          if (errno != EAGAIN) {
            LOG(INFO) << "Write error. (" << errno << "). Disconnecting fd " << _fd;
            _em->enqueue(bind(
                &TcpSocket::Internal::disconnect, shared_from_this()));
          }
          pthread_mutex_unlock(&_mutex);
          return;
        } else {
          _em->enqueue(bind(
              &TcpSocket::Internal::disconnect, shared_from_this()));
        }
      }
    }
    _em->removeFd(_fd, EventManager::EM_WRITE);
  }
  pthread_mutex_unlock(&_mutex);
}

TcpListenSocket::TcpListenSocket(EventManager* em, int fd)
    : _internal(new Internal(em, fd)) {
  em->watchFd(fd, EventManager::EM_READ, bind(
      &TcpListenSocket::Internal::onAccept, _internal));
}

TcpListenSocket::~TcpListenSocket() {
  _internal->shutdown();
}

TcpListenSocket::Internal::Internal(EventManager* em, int fd)
    : _em(em), _fd(fd) {
  pthread_mutex_init(&_mutex, 0);
  fcntl(_fd, F_SETFL, O_NONBLOCK);
}

TcpListenSocket::Internal::~Internal() {
  pthread_mutex_destroy(&_mutex);
}

void TcpListenSocket::Internal::shutdown() {
  pthread_mutex_lock(&_mutex);
  _em->removeFd(_fd, EventManager::EM_READ);
  ::shutdown(_fd, SHUT_RDWR);
  ::close(_fd);
  _fd = -1;
  pthread_mutex_unlock(&_mutex);
}

shared_ptr<TcpListenSocket> TcpListenSocket::create(
    EventManager* em, uint16_t port) {
  struct sockaddr_in sa;
  int fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

  if (fd == -1) {
    LOG(ERROR) << "can not create socket";
    return shared_ptr<TcpListenSocket>();
  }

  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  sa.sin_addr.s_addr = INADDR_ANY;

  if (::bind(fd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
    LOG(ERROR) << "error bind failed";
    close(fd);
    return shared_ptr<TcpListenSocket>();
  }

  if (::listen(fd, 5) == -1) {
    LOG(ERROR) << "error listen failed";
    close(fd);
    return shared_ptr<TcpListenSocket>();
  }

  return shared_ptr<TcpListenSocket>(new TcpListenSocket(em, fd));
}

void TcpListenSocket::setAcceptCallback(
    function<void(shared_ptr<TcpSocket>)> callback) {
  pthread_mutex_lock(&_internal->_mutex);
  _internal->_callback = callback;
  pthread_mutex_unlock(&_internal->_mutex);
}

void TcpListenSocket::Internal::onAccept() {
  pthread_mutex_lock(&_mutex);
  if (_fd > 0) {
    int fd = ::accept(_fd, NULL, NULL);
    pthread_mutex_unlock(&_mutex);
    if (fd > 0) {
      shared_ptr<TcpSocket> s(new TcpSocket(_em, fd));
      if (_callback) {
        _callback(s);
        // We separate registration to give AcceptCallback a chance to set up
        // receive / disconnect callbacks for the socket. The callback must
        // call start() on the socket to begin receiving traffic.
      }
    }
  } else {
    pthread_mutex_unlock(&_mutex);
  }
}
}
