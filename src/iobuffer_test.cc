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
#include "iobuffer.h"
#include <gtest/gtest.h>
#include <string>

using epoll_threadpool::IOBuffer;
using std::string;

TEST(IOBufferTest, AppendBytes) {
  IOBuffer buf;
  const char *a = "abc", *b = "def";

  buf.append(a, 3);
  buf.append(b, 3);
  ASSERT_EQ(6, buf.size());
  ASSERT_EQ(NULL, buf.pulldown(7));
  ASSERT_EQ(string("abcdef"), string(buf.pulldown(6), 6));
  ASSERT_EQ(true, buf.consume(1));
  ASSERT_EQ(5, buf.size());
  ASSERT_EQ(NULL, buf.pulldown(6));
  ASSERT_EQ(string("bcdef"), string(buf.pulldown(5), 5));
  ASSERT_EQ(true, buf.consume(3));
  ASSERT_EQ(2, buf.size());
  ASSERT_EQ(string("ef"), string(buf.pulldown(2), 2));
  ASSERT_EQ(NULL, buf.pulldown(3));
}

TEST(IOBufferTest, ConsumeBeforePullDown) {
  IOBuffer buf;
  const char *a = "abc", *b = "def";

  buf.append(a, 3);
  buf.append(b, 3);
  ASSERT_EQ(6, buf.size());
  ASSERT_EQ(true, buf.consume(1));
  ASSERT_EQ(5, buf.size());
  ASSERT_EQ(NULL, buf.pulldown(6));
  ASSERT_EQ(string("bcdef"), string(buf.pulldown(5), 5));
  ASSERT_EQ(true, buf.consume(5));
  ASSERT_EQ(0, buf.size());
  ASSERT_EQ(NULL, buf.pulldown(1));
  ASSERT_EQ(NULL, buf.pulldown(0));
}
