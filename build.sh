#!/bin/bash

if [ "$1" == "br" ]; then
  rm -rf out
  mkdir out
  cd out
  cmake ..
  make
  ./chip8
elif [ "$1" == "b" ]; then
  rm -rf out
  mkdir out
  cd out
  cmake ..
  make
elif [ "$1" == "r" ]; then
  ./out/chip8
elif [ "$1" == "d" ]; then
  rm -rf out
  mkdir out
  cd out
  cmake -DCMAKE_BUILD_TYPE=DEBUG ..
  cmake --build .
  make
fi
