#!/bin/sh

mkdir -p bin/
g++ tests.cpp -lgtest -o bin/tests
bin/tests && exit 0 || exit 1
