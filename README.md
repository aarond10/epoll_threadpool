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

    // Enqueue a function to run after all functions currently running have
    // completed. (Useful for destroying objects that might be in use at the
    // time you want to delete them.)
    em.enqueueAfter(bind(&MyCleanupFunction));

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

Dependencies
------------

The library depends on the following packages:

  - A recent version of g++ (with std::tr1 support).
  - pthread
  - [glog](http://code.google.com/p/google-glog/) - used for debug logging
  - [googletest](http://code.google.com/p/googletest/) - for unit tests

