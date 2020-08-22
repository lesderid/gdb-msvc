# gdb-msvc

![CI](https://github.com/lesderid/gdb-msvc/workflows/CI/badge.svg)

gdb-msvc is a series of patches for GDB (and its dependencies) for easier debugging of Microsoft Visual C++ (MSVC) binaries.

## Features

The main features of this patchset are:

* MSVC demangling support
* PDB debug symbol loading

These features are implemented using libraries from [LLVM](https://llvm.org/).

## Building

* `mkdir build && cd build`
* `../configure --target=i686-w64-mingw32 <other configure flags>`
* `make` (and `make install`)

Packages:

* Arch Linux: [gdb-msvc-git](https://aur.archlinux.org/packages/gdb-msvc-git/) (AUR)

## License

Most of the code is available under the terms of the [GNU GPLv3 license](/gdb/COPYING). See [the original README](/README-GDB) and license notices in source files for details.

By contributing you agree to make your code available under the same license.
