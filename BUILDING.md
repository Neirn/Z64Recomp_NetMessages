# Building
---

## Recomp Extlib and Server

The building process for this project's non-nrm components is finicky and 
coupled to vcpkg and LLVM.

It will (hopefully) be made less fragile at a later point.

### Dependencies

* [vcpkg](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started)
* CMake (3.21 or later)
* LLVM 19.1.0
  * If you are using clang on Windows, ensure you are using clang-cl.exe

### Building the Server and Client with CMake

Ensure that you have vcpkg installed, that VCPKG_ROOT is set to the directory
you cloned the vcpkg repo into. Also ensure that the directory with the vcpkg
executable is in your path.

These commands will build the release version of the targets. To build as debug
or other variants, change the value of DCMAKE_BUILD_TYPE.

### Windows (Powershell)

Building has only been tested with the defines 
`DCMAKE_CXX_COMPILER=clang-cl DCMAKE_C_COMPILER=clang-cl`.

```powershell
cmake -DCMAKE_BUILD_TYPE=Release -DVCPKG_TARGET_TRIPLET=x64-windows-static-md -DVCPKG_HOST_TRIPLET=x64-windows-static-md -DCMAKE_SHARED_LIBRARY_PREFIX_CXX= -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DCMAKE_INSTALL_PREFIX=install/windows-x64 -S . -B build/windows-x64 -G Ninja
```

```powershell
cmake --build build/windows-x64
```

### MacOS

These instructions are for ARM MacOS machines. To build for Intel machines,
change CMAKE_OSX_ARCHITECTURES to x86_64.

```zsh
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_SHARED_LIBRARY_PREFIX_CXX= -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DCMAKE_INSTALL_PREFIX=install/macos -S . -B build/macos -G Ninja
```

```zsh
cmake --build build/macos
```

### Linux (Untested)

```bash
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_SHARED_LIBRARY_PREFIX_CXX= -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DCMAKE_INSTALL_PREFIX=install/linux -S . -B build/linux -G Ninja
```

```bash
cmake --build build/linux
```

## Recomp Mod (nrm)

### Writing mods
See [this document](https://hackmd.io/fMDiGEJ9TBSjomuZZOgzNg) for an explanation of the modding framework, including how to write function patches and perform interop between different mods.

### Tools
You'll need to install `clang` and `make` to build this template.
* On Windows, using [chocolatey](https://chocolatey.org/) to install both is recoootended. The packages are `llvm` and `make` respectively.
  * The LLVM 19.1.0 [llvm-project](https://github.com/llvm/llvm-project) release binary, which is also what chocolatey provides, does not support MIPS correctly. The solution is to install 18.1.8 instead, which can be done in chocolatey by specifying `--version 18.1.8` or by downloading the 18.1.8 release directly.
* On Linux, these can both be installed using your distro's package manager. You may also need to install your distro's package for the `lld` linker. On Debian/Ubuntu based distros this will be the `lld` package.
* On MacOS, these can both be installed using Homebrew. Apple clang won't work, as you need a mips target for building the mod code.

On Linux and MacOS, you'll need to also ensure that you have the `zip` utility installed.

You'll also need to grab a build of the `RecompModTool` utility from the releases of [N64Recomp](https://github.com/N64Recomp/N64Recomp). You can also build it yourself from that repo if desired.

### Building
* First, run `make` (with an optional job count) to build the mod code itself.
* Next, run the `RecompModTool` utility with `mod.toml` as the first argument and the build dir (`build/mod` in the case of this mod) as the second argument.
  * This will produce your mod's `.nrm` file in the build folder.
  * If you're on MacOS, you may need to specify the path to the `clang` and `ld.lld` binaries using the `CC` and `LD` environment variables, respectively.
