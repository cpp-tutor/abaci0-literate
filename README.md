# A Literate Program Implementing a Subset of the Abaci Programming Language

## About

The file `Abaci0.md` is a human-readable manuscript intended to be a full description of how this compiler was written from scratch, as well as describing the basics of how it operates. This file and all of the contents of directory `src`, plus the `CMakeLists.txt` CMake script, were machine-extracted from the master literate source file `Abaci0.lit`.

## Pre-requisites

To build this project requires:

* A C++ compiler, tested only with `g++` version 12.2.0 and `clang++` version 14.0.6
* The LLVM development libraries, built against version 14.0
* The Boost headers, specifically Boost Spirit X3 in Boost 1.74
* Headers and link-library of `libfmt`, built against version 10.2.1
* Python version 3, tested with version 3.11.2

This project was developed using Debian current stable (Bookworm).

## Building

To create the executable `abaci0` in a `build` sub-directory of the project source files:

```bash
mkdir build && cd build
cmake ..
make
```

Run `./abaci0` without arguments to enter an interactive session, or with a source filename as the single argument to execute a script.

To recreate the manuscript and sources:

```bash
python3 literate.py Abaci0.lit
```

## Roadmap

Release 1.0.0 is believed to be feature-complete with no known defects. (Please raise issues or submit pull requests.)

Future development of this project is likely to be fixes only, no major language enhancements or additions are planned. I am interested in adding support for the expectation operator of Spirit X3 (`>` instead of `>>`), however this would most likely involve manual testing of each rule as the changes are made to avoid breakages.

## Version history

* **0.8.0** (2024-Apr-09) Initial release, believed fully working but not complete (no class instances or method calls).

* **0.8.2** (2024-Apr-17) Improvements to processed manuscript and minor changes to code.

* **0.8.3** (2024-Apr-18) Allow UTF-8 identifiers, and UTF-8 tokens in `parser/Keywords.hpp`.

* **0.8.6** (2024-Apr-22) Corrections to source manuscript and minor changes to code.

* **0.8.9** (2024-Apr-25) Factorial and Fibonacci functions now generate correct code, updates made to source manuscript.

* **0.9.0** (2024-May-09) Dot selection for data members and methods, this pointer.

* **0.9.1** (2024-May-22) Fixes for VS2022, improved name mangling, self-assignment.

* **0.9.2** (2024-May-25) User input and type conversions.

* **1.0.0** (2024-May-27) User messages now have their own header file. Small changes to front-end with improved error diagnostics for defective code.

* **1.0.1** (2024-Jun-03) Functions and methods can now return object values, which allows for creation of factory functions.

* **1.0.2** (2024-Jun-22) Minor code changes related to this pointer and return value tracking.

## License

All writing and source code released under a Creative Commmons License, &copy;2023-24 Richard Spencer
