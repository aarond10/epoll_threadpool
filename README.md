EventManager 
============

This is a simple epoll() based thread-pool implementation for linux designed
to be fast and very easy to use.

How do I use it?
----------------

Basic event queing is as simple as:

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

Dependencies
------------

The library depends on the following packages:

  - A recent version of g++ (with std::tr1 support).
  - pthread
  - [glog](http://code.google.com/p/google-glog/) - used for debug logging
  - [googletest](http://code.google.com/p/googletest/) - for unit tests

