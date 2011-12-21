EventManager 
============

This is a simple epoll() based thread-pool implementation for linux designed
to be fast and very easy to use.

How do I use it?
----------------

Basic event queing is simple:

    using std::tr1::bind;
    EventManager em;

    // Start 10 worker threads
    em.start(10);  

    // Execute a function ASAP
    em.enqueue(bind(&MyFunction));

    // Execute a function in 10 seconds time.
    em.enqueue(bind(&MyFunction), EventManager::currentTime() + 10.0);

    // Stop the event manager's threads. This call is optional. It will be
    // done if required when the EventManager is destroyed.
    em.stop();

The library also supports file descriptors:

    int socketFd = ...;

    using std::tr1::bind;
    EventManager em;
    em.start(10);  

    em.watchFd(socketFd, EPOLLIN, bind(&onReadEvent));
    em.watchFd(socketFd, EPOLLOUT, bind(&onWriteEvent));
    em.watchFd(socketFd, EPOLLHUP, bind(&onDisconnectEvent));

    // ... sleep or do something else ...

    em.removeFd(socketFd, EPOLLIN);
    em.removeFd(socketFd, EPOLLOUT);
    em.removeFd(socketFd, EPOLLHUP);

    em.stop();

All event handlers will be run on the first available EventManager worker 
thread. Blocking functions are strongly discouraged due to the risk of deadlock
related issues. Its perfectly OK for code running on a worker thread to update
the EventManager it is running on with the exception of the stop() function.
The stop() function can only be called from a non-worker thread. Ideally it
should be called from the thread that created the EventManager to begin with.

Can you make it easier?
-----------------------

If you insist. The following addititional classes are provided to allow higher
level coding:

  - TcpSocket - A receive and send buffering TcpSocket proxy. Provides receive and disconnect callbacks.
  - TcpListenSocket - A listening socket that creates TcpSocket instances when someone connects. Provides an "onAccept" callback.
  - IOBuffer - A buffering I/O class that defers memory copying as long as possible to avoid redundant copy operations. This is designed to make dealing with received data significantly easier.
  - Notification - A simple pthread-based notification class for blocking until triggered. This should *not* be used on worker threads if possible but can be used to safely clean up from the main application thread.
  - Future - A proxy object representing a return value that may be set at some stage in the future. This allows for conventional (synchronous) and callback-driven (asynchronous) use cases with minimal fuss.
  - Barrier - Provides a synchronisation point. This object provides a callback that should be triggered exactly N times. On the Nth time, it calls a function and deletes itself. This functionality *may* be rolled into the Future class at some stage (by chaining Futures).

Examples of how to use these classes will eventually arrive here. Until then, there are a bunch of unit tests sitting in the src/ directory that can be used as examples.

Tests
-----

I haven't bothered with any complex build systems. GNU make and valgrind are all that's required to build and test the library. For the paranoid (like me), you can run repeated tests with valgrind memory testing via:

    make long_test

I'm not aware of any bugs but I would certainly be happier with more thorough unit tests. These will also probably appear with time but if you find this library useful, I'd certainly appreciate payback in the form of additional unit test patches. :)


Dependencies
------------

The library depends on the following packages:

  - A recent version of g++ (with std::tr1 support for function, bind, shared_from_this, ...).
  - pthread
  - [glog](http://code.google.com/p/google-glog/) - used for debug logging
  - [googletest](http://code.google.com/p/googletest/) - for unit tests

