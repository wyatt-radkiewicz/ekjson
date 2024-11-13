<p align="center"><img align="center" width=200 src="docs/icon.png"/></p>
<p align="center"><b><i>ekjson</i></b>  <b>ek</b>lips3d <b>json</b> parser</p>

### Table of Contents
 1. [About](#About)
 1. [How to Build](#How-to-Build)
 1. [How to Use ekjson](#How-to-Use-ekjson)

# About
[![C/C++ CI](https://github.com/wyatt-radkiewicz/ekjson/actions/workflows/test.yml/badge.svg)](https://github.com/wyatt-radkiewicz/ekjson/actions/workflows/test.yml)

ekjson is meant to be a low overhead, no frills parser in C. It has main design
goals of being fast, simple, freestanding, and not including more features than
it needs too, all the while making sure that there is still enough included to
get started quickly.

### Features
 - Fast implementation
 - Build options
 - In-place string compare and copy from JSON source
 - Exact integer and float parsers
 - Under 2000 LOC (1500 without comments)

# How to Build
#### In your project
If you want to just use ekjson in your current project, just drop ekjson.h and
ekjson.c to somewhere in the project and get coding.

#### Examples
If you want to see or build the examples you can run
```
make example_[example_name]
```
Where \[example_name\] is the name of a directory in examples/. Currently there
are
 - example_value
 - example_object
 - example_array
 - example_config

#### Tester
First, run ```./configure``` to get dependencies for the tester (and benchmark).
Then run ```make dbg``` or ```make rel``` to make debug and release versions of
the tester. The tester will be put in the dbg/ or rel/ directories respectivly.
To run these testers once their built, you can run
```
rel/test
```
And if you want to test speed of certain functions, you can run
```
rel/test speed
```

#### Benchmark
First, run ```./configure``` in the root directory if you haven't already.
Then go to the benchmark/ directory and run ```./make_deps.sh```. This will make
the other json parsers that will be benchmarked against. Then you can run
```
make parse
```
To test a parsing benchmark of samples/512KB.json, or you can run:
```
make float
```
To test the benchmarker for the float parser.

# How to Use ekjson


### Further Reading
### License
<img width=400 src="docs/LICENSE.jpg"/>

[GLWTSPL](/LICENSE)
