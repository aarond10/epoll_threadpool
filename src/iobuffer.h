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
#ifndef _EPOLL_THREADPOOL_IOBUFFER_H_
#define _EPOLL_THREADPOOL_IOBUFFER_H_

#include <stdlib.h>
#include <string.h>
#include <deque>
#include <vector>

namespace epoll_threadpool {

using std::deque;
using std::vector;

/**
 * Stores blocks of data in potentially discontinuous blocks of memory.
 * The user can request these discontinuous blocks are pulled down into
 * continuous RAM for reading off.
 */
class IOBuffer {
 public:
  IOBuffer() : _size(0) {}

  /**
   * Convenience constructor for creating an IOBuffer out of a character
   * array. This *will* copy the data so avoid it if possible.
   */
  IOBuffer(const char *data, size_t len) : _size(0) {
    append(data, len);
  }

  virtual ~IOBuffer() {
    for (int i = 0; i < _blocks.size(); i++) {
      delete _blocks[i];
    }
  }

  /**
   * Appends another IOBuffer instance to this IOBuffer. All data from the
   * first buffer will be appended to this buffer without memcpying the
   * data itself. This class will take ownership of the IOBuffer instance.
   */
  void append(IOBuffer *data) {
    for (int i = 0; i < data->_blocks.size(); i++) {
      _blocks.push_back(data->_blocks[i]);
    }
    _size += data->_size;
    data->_blocks.clear();
    delete data;
  }

  /**
   * Appends a given vector of data to the IOBuffer. Ownership of the vector
   * is taken by the class, avoiding any data copies.
   */
  void append(vector<char> *data) {
    _size += data->size();
    _blocks.push_back(data);
  }

  /**
   * Convenience method that allows appending of arbitrary data types.
   * @param data a pointer to an array of one or more elements of type T.
   * @param len the size of the array of data types T.
   */
  template<class T>
  void append(T *data, size_t len) {
    vector<char> *d = new vector<char>();
    d->resize(sizeof(T)*len);
    memcpy(&((*d)[0]), data, sizeof(T)*len);
    append(d);
  }
  
  /**
   * Equivalent to the previous append() method but required to use this class
   * as a destination buffer for msgpack::pack().
   */
  void write(const char *data, int len) {
    append(data, (size_t)len);
  }

  /**
   * Returns the current length in bytes of all blocks combined.
   */
  size_t size() const { return _size; }

  /**
   * Ensures that the first n bytes are stored in contiguous memory.
   * Returns a pointer to the start of the memory on success, NULL on error.
   */
  const char *pulldown(size_t bytes) {
    if (bytes > _size || _size == 0) {
      return NULL;
    }
    if (bytes > _blocks[0]->size()) {
      vector<char> *pulldown_block = _blocks[0];
      _blocks.pop_front();
      while (pulldown_block->size() < bytes) {
        pulldown_block->insert(
            pulldown_block->end(), _blocks[0]->begin(), _blocks[0]->end());
        delete _blocks[0];
        _blocks.pop_front();
      }
      _blocks.push_front(pulldown_block);
    }
    return &_blocks[0]->at(0);
  }

  /**
   * Consumes the first n bytes of the buffer.
   * Takes care of freeing memory, etc.
   * Returns false if asked to consume more data than is available.
   */
  bool consume(size_t bytes) {
    if (bytes > _size || _size == 0) {
      return false;
    }
    while (!_blocks.empty() && bytes >= _blocks[0]->size()) {
      _size -= _blocks[0]->size();
      bytes -= _blocks[0]->size();
      delete _blocks[0];
      _blocks.pop_front();
    }
    if (_blocks.empty()) {
      return true;
    }
    if (bytes) {
      memmove(&_blocks[0]->at(0), &_blocks[0]->at(bytes), _blocks[0]->size() - bytes);
      _blocks[0]->resize(_blocks[0]->size() - bytes);
      _size -= bytes;
    }
    return true;
  }

 private:
  size_t _size;
  deque<vector<char>*> _blocks;

 private:
  IOBuffer(const IOBuffer&);
};
}

#endif
