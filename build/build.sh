#!/bin/sh

if [ -d "globl" ]; then
	rm -rf globl
fi

mkdir globl
cd globl

rm -rf *

cmake -DCMAKE_C_COMPILER=clang ..
make -j8
