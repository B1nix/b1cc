CXX ?= c++
CXXFLAGS ?= -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -O2

SRCS = $(wildcard src/*.cpp)
OBJS = $(SRCS:src/%.cpp=build/%.o)

all: build/b1cc

build/b1cc: $(OBJS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@

build/%.o: src/%.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: build/b1cc
	./test.sh

clean:
	rm -rf build

.PHONY: all test clean
