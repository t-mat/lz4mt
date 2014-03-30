# lz4mt

lz4mt is platform independent, multi-threading, 
[lz4 stream v1.4](https://docs.google.com/document/d/1gZbUoLw5hRzJ5Q71oPRN6TO4cRMTZur60qip-TE7BhQ/edit?pli=1)
implementation in C++11.

## Building for MSVC2012 / 2013 (Visual Studio Express 2012 / 2013 for Windows Desktop)

 - Run `build.bat` (or `build_vs2013.bat`).
 - Executable file will be created in `platform_msvc2012/` (or `platform_msvc2013/`).

## Building for Linux

 - Run `make`.
 - `./lz4mt` will be created.

## Building for Linux with Clang

 - Run `make CXX=clang++ CC=clang`
 - `./lz4mt` will be created.

## See also

 - [lz4 Extremely Fast Compression algorithm](https://code.google.com/p/lz4/)
