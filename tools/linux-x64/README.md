# Linux Intel/AMD x86-64 toolchain

From the project root, run this once **inside Linux**:

```sh
bash tools/linux-x64/setup.sh
```

The script downloads about 206 MB from pinned upstream GitHub releases, verifies
SHA-256 checksums, and extracts these local tools under this folder:

- GCC 15.2.0 and GDB 16.3 (xPack GCC 15.2.0-1)
- Ninja 1.13.1 (xPack Ninja Build 1.13.1-1)
- CMake 3.31.9 (xPack CMake 3.31.9-1)

The repository contains only this guide and the setup script in this folder.
Downloaded archives and extracted packages are ignored by Git. Git LFS is not
required. Later setup runs reuse verified downloads. No administrator rights
are needed to install the tools themselves.

Then select the `linux` configure/build presets in VS Code, or run:

```sh
./tools/linux-x64/xpack-cmake-3.31.9-1/bin/cmake --preset linux
./tools/linux-x64/xpack-cmake-3.31.9-1/bin/cmake --build --preset linux
./build/linux/RoboticDog
```

The Linux preset and VS Code debugger already point to these package folders.
The project requires C++20. The executable links the GCC/C++ runtimes statically,
but still uses the Linux system C library.

## Requirements

- Intel/AMD x86-64 Linux with glibc 2.36 or newer (not Windows, ARM, or Alpine/musl).
- Bash, curl or wget, tar/gzip, sha256sum, compatible runtime libraries, and internet
  access for the first download.
- System C library development headers and startup files; on Ubuntu/Debian these
  are supplied by `libc6-dev`. Install them through the package manager if missing.
- VS Code C/C++ and CMake Tools extensions for the editor build/debug workflow.

No separately installed GCC, GDB, Ninja, or CMake is needed after setup. A
headless machine cannot open an external graphical terminal; disable
`externalConsole` in the launch configuration when debugging headlessly.

The toolchain was verified in an Ubuntu 24.04 x86-64 QEMU VM with glibc 2.39 and
libc6-dev, without system compiler/debugger/build tools: configuration, build,
Hello World execution, and a GDB breakpoint at main passed. A fresh Linux build
is needed after copying/cloning; build caches are machine-specific.

## Sources

- https://github.com/xpack-dev-tools/gcc-xpack/releases/tag/v15.2.0-1
- https://github.com/xpack-dev-tools/ninja-build-xpack/releases/tag/v1.13.1-1
- https://github.com/xpack-dev-tools/cmake-xpack/releases/tag/v3.31.9-1

Upstream license information is included in each downloaded package. The script
pins the SHA-256 digests published with those releases.
