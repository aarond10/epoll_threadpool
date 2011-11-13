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

#include <glog/logging.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>

namespace rpc {

using namespace std::tr1;

TcpSocket::TcpSocket(EventManager *em, int fd)
    : _internal(new Internal(em, fd)) {
}

TcpSocket::~TcpSocket() {
  _internal->_disconnectCallback = NULL;
  disconnect();
}

TcpSocket::Internal::Internal(EventManager *em, int fd)
    : _em(em), _fd(fd), _isStarted(false) {
  pthread_mutex_init(&_mutex, 0);
  fcntl(_fd, F_SETFL, O_NONBLOCK);
}
TcpSocket::Internal::~Internal() {
  pthread_mutex_destroy(&_mutex);
}

void TcpSocket::start() {
  _internal->_start();
}

void TcpSocket::Internal::_start() {
  pthread_mutex_lock(&_mutex);
  if (!_isStarted) {
    _isStarted = true;
    if (_fd > 0) {
      _em->watchFd(_fd, EPOLLIN,
          std::tr1::bind(&TcpSocket::Internal::onReceive, shared_from_this()));
      _em->watchFd(_fd, EPOLLRDHUP,
          std::tr1::bind(&TcpSocket::Internal::onDisconnect, shared_from_this()));
      _em->watchFd(_fd, EPOLLOUT,
          std::tr1::bind(&TcpSocket::Internal::onCanSend, shared_from_this()));
      _em->watchFd(_fd, EPOLLHUP,
          std::tr1::bind(&TcpSocket::Internal::onDisconnect, shared_from_this()));

      // We trigger the receive handler to handle the case where we got
      // disconnected before start() completed.
      _em->enqueue(
          std::tr1::bind(&TcpSocket::Internal::onReceive, shared_from_this()));
    } else {
      if (_disconnectCallback) {
        _em->enqueue(_disconnectCallback);
      }
    }
  }
  pthread_mutex_unlock(&_mutex);
}

shared_ptr<TcpSocket> TcpSocket::connect(EventManager *em, string host, uint16_t port) {
  struct sockaddr_in sa;
  int fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

  if(fd == -1) {
    LOG(ERROR) << "can not create socket";
    return shared_ptr<TcpSocket>();
  }

  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  if(inet_pton(AF_INET, host.c_str(), &sa.sin_addr) <= 0) {
    LOG(ERROR) << "Failed to resolve address: " << host;
    close(fd);
    return shared_ptr<TcpSocket>();
  }

  if(::connect(fd, (struct sockaddr*)&sa, sizeof(sa)) == -1) {
    //DLOG(ERROR) << "error connect failed";
    close(fd);
    return shared_ptr<TcpSocket>();
  }

  return shared_ptr<TcpSocket>(new TcpSocket(em, fd));
}

void TcpSocket::write(IOBuffer *data) {
  pthread_mutex_lock(&_internal->_mutex);
  _internal->_sendBuffer.append(data);
  if (_internal->_isStarted) {
    _internal->_onCanSend();
  }
  pthread_mutex_unlock(&_internal->_mutex);
}

void TcpSocket::disconnect() {
  pthread_mutex_lock(&_internal->_mutex);
  _internal->_disconnect();
  pthread_mutex_unlock(&_internal->_mutex);
}

void TcpSocket::Internal::_disconnect() {
  if (_fd > 0) {
    _em->removeFd(_fd, EPOLLIN);
    _em->removeFd(_fd, EPOLLRDHUP);
    _em->removeFd(_fd, EPOLLOUT);
    _em->removeFd(_fd, EPOLLHUP);
    ::shutdown(_fd, SHUT_RDWR);
    ::close(_fd);
    _fd = -1;
    if (_disconnectCallback) {
      _em->enqueue(_disconnectCallback);
    }
  }
}

bool TcpSocket::isDisconnected() const {
  return _internal->_fd == -1;
}

void TcpSocket::setReceiveCallback(std::tr1::function<void(IOBuffer*)> callback) {
  pthread_mutex_lock(&_internal->_mutex);
  _internal->_recvCallback = callback;
  pthread_mutex_unlock(&_internal->_mutex);
}

void TcpSocket::setDisconnectCallback(std::tr1::function<void()> callback) {
  pthread_mutex_lock(&_internal->_mutex);
  _internal->_disconnectCallback = callback;
  pthread_mutex_unlock(&_internal->_mutex);
}

void TcpSocket::Internal::onReceive() {
  pthread_mutex_lock(&_mutex);
  _onReceive();
  pthread_mutex_unlock(&_mutex);
}

void TcpSocket::Internal::onCanSend() {
  pthread_mutex_lock(&_mutex);
  _onCanSend();
  pthread_mutex_unlock(&_mutex);
}

void TcpSocket::Internal::onDisconnect() {
  pthread_mutex_lock(&_mutex);
  _disconnect();
  pthread_mutex_unlock(&_mutex);
}

void TcpSocket::Internal::_onReceive() {
  if (_fd > 0) {
    vector<char> *buf = new vector<char>();
    buf->resize(4096);

    int r = ::read(_fd, &(*buf)[0], buf->size());
    if (r > 0) {
      buf->resize(r);
      _recvBuffer.append(buf);
      if (_recvCallback) {
        _recvCallback(&_recvBuffer);
      }
    } else if (r < 0) {
      delete buf;
      if(errno != EAGAIN) {
        LOG(INFO) << "Read error. (" << errno << "). Disconnecting fd " << _fd;
        _em->enqueue(std::tr1::bind(&TcpSocket::Internal::onDisconnect, shared_from_this()));
      }
    } else {
      delete buf;
      _em->enqueue(std::tr1::bind(&TcpSocket::Internal::onDisconnect, shared_from_this()));
    }
  }
}

void TcpSocket::Internal::_onCanSend() {
  if (_fd > 0 && _sendBuffer.size()) {
    int sz = _sendBuffer.size() > 4096 ? 4096 : _sendBuffer.size();
    const char *buf = _sendBuffer.pulldown(sz);
    if (buf) {
      int r = ::write(_fd, buf, sz);
      if (r > 0) {
        _sendBuffer.consume(r);
      } else {
        LOG(ERROR) << "Write error. Returned " << r << ". Disconnecting.";
        _disconnect();
      }
    }
  }
}

TcpListenSocket::TcpListenSocket(EventManager *em, int fd)
    : _internal(new Internal(em, fd)) {
  em->watchFd(fd, EPOLLIN, std::tr1::bind(&TcpListenSocket::Internal::onAccept, _internal));
}

TcpListenSocket::~TcpListenSocket() {
  _internal->shutdown();
}

TcpListenSocket::Internal::Internal(EventManager *em, int fd)
    : _em(em), _fd(fd) {
  pthread_mutex_init(&_mutex, 0);
  fcntl(_fd, F_SETFL, O_NONBLOCK);
}

TcpListenSocket::Internal::~Internal() {
  pthread_mutex_destroy(&_mutex);
}

void TcpListenSocket::Internal::shutdown() {
  pthread_mutex_lock(&_mutex);
  _em->removeFd(_fd, EPOLLIN);
  ::shutdown(_fd, SHUT_RDWR);
  ::close(_fd);
  _fd = -1;
  pthread_mutex_unlock(&_mutex);
}

shared_ptr<TcpListenSocket> TcpListenSocket::create(EventManager *em, uint16_t port) {
  struct sockaddr_in sa;
  int fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

  if(fd == -1) {
    LOG(ERROR) << "can not create socket";
    return shared_ptr<TcpListenSocket>();
  }

  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  sa.sin_addr.s_addr = INADDR_ANY;

  if(::bind(fd,(struct sockaddr *)&sa, sizeof(sa)) == -1) {
    LOG(ERROR) << "error bind failed";
    close(fd);
    return shared_ptr<TcpListenSocket>();
  }

  if(::listen(fd, 5) == -1) {
    LOG(ERROR) << "error listen failed";
    close(fd);
    return shared_ptr<TcpListenSocket>();
  }

  return shared_ptr<TcpListenSocket>(new TcpListenSocket(em, fd));
}

void TcpListenSocket::setAcceptCallback(
    std::tr1::function<void(shared_ptr<TcpSocket>)> callback) {
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
