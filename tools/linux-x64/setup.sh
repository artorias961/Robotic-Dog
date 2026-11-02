#!/usr/bin/env bash
set -euo pipefail

if [[ $(uname -s) != Linux || $(uname -m) != x86_64 ]]; then
    echo "These tools require Intel/AMD x86-64 Linux." >&2
    exit 1
fi

# GDB in this pinned toolchain requires GLIBC_2.36.
if ! glibc=$(getconf GNU_LIBC_VERSION 2>/dev/null); then
    echo "This toolchain requires glibc 2.36 or newer (not musl/Alpine)." >&2
    exit 1
fi
version=${glibc##* }
major=${version%%.*}
minor=${version#*.}
minor=${minor%%.*}
if (( major < 2 || (major == 2 && minor < 36) )); then
    echo "This toolchain requires glibc 2.36 or newer; found $glibc." >&2
    exit 1
fi
if ! command -v curl >/dev/null && ! command -v wget >/dev/null; then
    echo "Install curl or wget to download the tools, then rerun this script." >&2
    exit 1
fi

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
mkdir -p "$root/archives"

verify() {
    local actual
    actual=$(sha256sum "$1")
    [[ ${actual%% *} == "$2" ]]
}

install_package() {
    local repo=$1 tag=$2 archive=$3 folder=$4 sha256=$5
    local dest="$root/archives/$archive"
    local url="https://github.com/$repo/releases/download/$tag/$archive"

    if [[ ! -f "$dest" ]]; then
        echo "Downloading $archive..."
        if command -v curl >/dev/null; then
            curl --fail --location --silent --show-error --retry 3 "$url" -o "$dest.part"
        else
            wget --https-only -q "$url" -O "$dest.part"
        fi
        if ! verify "$dest.part" "$sha256"; then
            echo "Checksum mismatch for $archive; refusing to extract it." >&2
            exit 1
        fi
        mv -- "$dest.part" "$dest"
    fi
    if ! verify "$dest" "$sha256"; then
        echo "Checksum mismatch for $dest; remove that cached archive and retry." >&2
        exit 1
    fi

    # Extract inside Linux to preserve symbolic links and executable permissions.
    echo "Verified $archive; extracting..."
    tar -xzf "$dest" -C "$root"
    if [[ ! -d "$root/$folder" ]]; then
        echo "Expected package folder missing: $folder" >&2
        exit 1
    fi
}

install_package xpack-dev-tools/gcc-xpack v15.2.0-1 \
    xpack-gcc-15.2.0-1-linux-x64.tar.gz xpack-gcc-15.2.0-1 \
    f6dcc3815299a23b2b4372223c4ca5f1b5a0543ed097d57eb67e0753b7c484e0
install_package xpack-dev-tools/ninja-build-xpack v1.13.1-1 \
    xpack-ninja-build-1.13.1-1-linux-x64.tar.gz xpack-ninja-build-1.13.1-1 \
    e48e5ff6e3db4844670b74a7adc1270d8bc1dc661e2a36a134daa199a9b07c10
install_package xpack-dev-tools/cmake-xpack v3.31.9-1 \
    xpack-cmake-3.31.9-1-linux-x64.tar.gz xpack-cmake-3.31.9-1 \
    76870fcbef9c618bff4d61b4b4dfffd8780aef7ea46a8478b2c81936c14106b6

"$root/xpack-gcc-15.2.0-1/bin/g++" --version
"$root/xpack-gcc-15.2.0-1/bin/gdb" --version
"$root/xpack-ninja-build-1.13.1-1/bin/ninja" --version
"$root/xpack-cmake-3.31.9-1/bin/cmake" --version

printf '\nLinux toolchain installed. From the project root, run:\n'
printf '  ./tools/linux-x64/xpack-cmake-3.31.9-1/bin/cmake --preset linux\n'
printf '  ./tools/linux-x64/xpack-cmake-3.31.9-1/bin/cmake --build --preset linux\n'
