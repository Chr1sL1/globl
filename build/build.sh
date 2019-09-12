#!/bin/sh

if [ -d "globl" ]; then
	rm -rf globl
fi

mkdir globl
cd globl

rm -rf *

cmake  ..
make -j8
