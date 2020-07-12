# gdb-msvc

gdb-msvc is a series of patches for GDB (and bfd) for easier debugging of Microsoft Visual C++ (MSVC) binaries.

## Main features

The main features in this patchset are:

* MSVC demangling support (using [LLVM](https://llvm.org/))
* PDB debug symbol loading (using [radare2](https://github.com/radareorg/radare2)'s libr)

(Note: we can't currently use LLVM for PDB loading as LLVM doesn't yet expose its PDB functions as a C API and bfd is written in C, not C++.)

## Building

* `mkdir build && cd build`
* `../configure --target=i686-w64-mingw32 <other configure flags>`
* `make` (and `make install`)

Packages:

* Arch Linux: [gdb-msvc-git](https://aur.archlinux.org/packages/gdb-msvc-git/) (AUR)

## License

Most of the code is available under the terms of the [GNU GPLv3 license](/gdb/COPYING). See [the original README](/README-GDB) and license notices in source files for details.

By contributing you agree to make your code available under the same license.
