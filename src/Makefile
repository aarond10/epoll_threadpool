CPPFLAGS = -O3
LDFLAGS = -lpthread -lstdc++ -lglog -lgtest -lgtest_main

all: iobuffer_test eventmanager_test future_test tcp_test lib include

clean:
	rm -f *.o eventmanager_test future_test iobuffer_test tcp_test 
	rm -rf lib include

eventmanager_test: eventmanager.o eventmanager_test.o
	g++ -o $@ $^ $(LDFLAGS)

future_test: future_test.o eventmanager.o
	g++ -o $@ $^ $(LDFLAGS)

iobuffer_test: iobuffer_test.o
	g++ -o $@ $^ $(LDFLAGS)

tcp_test: eventmanager.o tcp.o tcp_test.o
	g++ -o $@ $^ $(LDFLAGS)

.PHONY: lib
lib: eventmanager.o tcp.o
	mkdir -p lib
	ar cr lib/libepoll_threadpool.a $^

.PHONY: include
include: barrier.h eventmanager.h future.h iobuffer.h notification.h tcp.h
	mkdir -p include/epoll_threadpool
	cp $^ include/epoll_threadpool

test: all
	valgrind -v --track-origins=yes --leak-check=full --show-reachable=yes ./eventmanager_test --gtest_break_on_failure 
	valgrind -v --track-origins=yes --leak-check=full --show-reachable=yes ./future_test --gtest_break_on_failure 
	valgrind -v --track-origins=yes --leak-check=full --show-reachable=yes ./iobuffer_test --gtest_break_on_failure 
	valgrind -v --track-origins=yes --leak-check=full --show-reachable=yes ./tcp_test --gtest_break_on_failure 

long_test: all
	valgrind -v --track-origins=yes --leak-check=full --show-reachable=yes ./eventmanager_test --gtest_repeat=1000 --gtest_break_on_failure 
	valgrind -v --track-origins=yes --leak-check=full --show-reachable=yes ./future_test --gtest_repeat=1000 --gtest_break_on_failure 
	valgrind -v --track-origins=yes --leak-check=full --show-reachable=yes ./iobuffer_test --gtest_repeat=1000 --gtest_break_on_failure 
	valgrind -v --track-origins=yes --leak-check=full --show-reachable=yes ./tcp_test --gtest_repeat=1000 --gtest_break_on_failure 

