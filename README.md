![eklipsed](/eklipsed.jpg)
<p align="center">ek(lips3d) json parser -*ektoml*-</p>

> A really fast, simple, malloc-less, (opinionated) json parser in c

## What is ekjson?
I wanted a simple, malloc-less, and really fast json parser for my text editor
luckyvim. I didn't want this library to be too invasive.
NOTE: This is an opinionated library. Basically the use case I made this library
for is for parsing a toml file into c structures, that's it.

## How to use ekjson?
### How to build:
Just drop the files into your project or make it a git submodule. It's only
2 files.

### How to test:
Just run
```
make && ./build/test
```

## License
[GLWTSPL](/LICENSE)

