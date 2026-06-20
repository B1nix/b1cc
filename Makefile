CXX ?= c++
CXXFLAGS ?= -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -O2

all: build/b1cc

build/b1cc: src/b1cc.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $< -o $@

test: build/b1cc
	./test.sh

clean:
	rm -rf build

.PHONY: all test clean
