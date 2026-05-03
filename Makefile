PRESET := wsl-gcc

.PHONY: all configure build test clean help

all: build

configure:
	cmake --preset $(PRESET)

build:
	@if [ ! -f build/build.ninja ]; then rm -rf build; $(MAKE) configure; fi
	cmake --build build

test: build
	./build/ordermatch_tests --gtest_color=yes

clean:
	rm -rf build

help:
	@echo Available targets: configure build test clean
