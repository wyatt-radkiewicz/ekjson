#!/bin/bash
git submodule update --init

cd jjson
git submodule update --init
cmake -DCMAKE_BUILD_TYPE="Release" .
make
cd ..

cd json-c
cmake -DCMAKE_BUILD_TYPE="Release" .
make
cd ..

