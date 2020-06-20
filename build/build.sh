#!/bin/sh

if [ -d "globl" ]; then
	rm -rf globl
fi

mkdir globl
cd globl

rm -rf *

cmake -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ ..
make -j8
