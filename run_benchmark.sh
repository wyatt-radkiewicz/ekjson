#!/bin/bash

make && ./build/test
if [ $? != 0 ]; then
	echo "tests/compilation failed"
	exit
fi

cd benchmark
make
./build/benchmark samples/512KB.json

