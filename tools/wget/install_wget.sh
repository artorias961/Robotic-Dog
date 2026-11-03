#!/usr/bin/env bash
# Install GNU Wget through the Linux distribution's package manager.
set -euo pipefail

if [[ ${1:-} == --help || ${1:-} == -h ]]; then
    printf 'Usage: bash install_wget.sh\nInstalls Wget and CA certificates on Linux. May request sudo privileges.\n'
    exit 0
fi
if (( $# > 0 )); then
    echo "Unknown argument: $1" >&2
    exit 2
fi
if [[ $(uname -s) != Linux ]]; then
    echo "This script supports Linux. On Windows, run install_wget.bat." >&2
    exit 1
fi
if command -v wget >/dev/null 2>&1 && wget --version >/dev/null 2>&1; then
    echo "Wget is already installed."
    wget --version
    exit 0
fi

as_root() {
    if (( EUID == 0 )); then
        "$@"
    elif command -v sudo >/dev/null 2>&1; then
        sudo "$@"
    else
        echo "Installation needs root privileges. Run this script as root or install sudo." >&2
        exit 1
    fi
}

if command -v apt-get >/dev/null 2>&1; then
    as_root apt-get update
    as_root apt-get install -y wget ca-certificates
elif command -v dnf >/dev/null 2>&1; then
    as_root dnf install -y wget ca-certificates
elif command -v yum >/dev/null 2>&1; then
    as_root yum install -y wget ca-certificates
elif command -v pacman >/dev/null 2>&1; then
    as_root pacman -S --needed --noconfirm wget ca-certificates
elif command -v zypper >/dev/null 2>&1; then
    as_root zypper --non-interactive install wget ca-certificates
elif command -v apk >/dev/null 2>&1; then
    as_root apk add --no-cache wget ca-certificates
else
    echo "Unsupported package manager. Install the wget package for your distribution." >&2
    exit 1
fi

if ! command -v wget >/dev/null 2>&1; then
    echo "Installation finished, but wget is not on PATH." >&2
    exit 1
fi
wget --version
echo "Wget installation complete."
