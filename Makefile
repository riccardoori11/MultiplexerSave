CXX ?= g++
CPPFLAGS += -Iinclude
CXXFLAGS ?= -std=c++20 -O2 -g -Wall -Wextra -Wpedantic -Wconversion -Wshadow
PREFIX ?= $(HOME)/.local
SOURCES := src/main.cpp src/process.cpp src/project.cpp
HEADERS := $(wildcard include/work/*.hpp)

.PHONY: all install clean
all: build/work

build/work: $(SOURCES) $(HEADERS)
	mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) $(LDFLAGS) -o $@

install: build/work
	install -d "$(DESTDIR)$(PREFIX)/bin"
	install -m 755 build/work "$(DESTDIR)$(PREFIX)/bin/work"

clean:
	rm -rf build
