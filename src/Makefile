CPPFLAGS = -g
LDFLAGS = -lpthread -lstdc++ -lglog -lgtest -lgtest_main

all: iobuffer_test eventmanager_test tcp_test

clean:
	rm -f *.o iobuffer_test eventmanager_test tcp_test

iobuffer_test: iobuffer_test.o
	g++ -o $@ $^ $(LDFLAGS)

eventmanager_test: eventmanager.o eventmanager_test.o
	g++ -o $@ $^ $(LDFLAGS)

tcp_test: eventmanager.o tcp.o tcp_test.o
	g++ -o $@ $^ $(LDFLAGS)

test: all
	valgrind -v --track-origins=yes --leak-check=full --show-reachable=yes ./iobuffer_test --gtest_break_on_failure 
	valgrind -v --track-origins=yes --leak-check=full --show-reachable=yes ./eventmanager_test --gtest_break_on_failure 
	valgrind -v --track-origins=yes --leak-check=full --show-reachable=yes ./tcp_test --gtest_break_on_failure 

long_test: all
	valgrind -v --track-origins=yes --leak-check=full --show-reachable=yes ./iobuffer_test --gtest_repeat=1000 --gtest_break_on_failure 
	valgrind -v --track-origins=yes --leak-check=full --show-reachable=yes ./eventmanager_test --gtest_repeat=1000 --gtest_break_on_failure 
	valgrind -v --track-origins=yes --leak-check=full --show-reachable=yes ./tcp_test --gtest_repeat=1000 --gtest_break_on_failure 

