#!/usr/bin/env bash
# =============================================================================
#  Robotic Dog - Zephyr + west installer for Linux (Ubuntu 22.04+ / Debian)
#
#  Usage:  bash tools/zephyr_west/installer_west.sh [workspace-folder]
#          default workspace: ~/zephyr
#
#  Installs: build tools (apt), Teensy USB permissions, a Python virtual
#  environment with west, the Zephyr source pinned to ZEPHYR_VERSION, Zephyr's
#  Python packages, and the ARM toolchain from the Zephyr SDK (all the
#  Teensy 4.1 needs).
#
#  Python: Zephyr v4.4.0 needs 3.12+. Uses the system Python (or another you have, e.g.
#  conda / miniforge) and checks that the virtual environment really works.
#
#  Safe to re-run: finished steps are skipped.
# =============================================================================
set -euo pipefail

# ---- Settings: keep these the same for the whole team ------------------------
ZEPHYR_VERSION="v4.4.0"          # Zephyr release everyone builds against
TOOLCHAIN="arm-zephyr-eabi"      # Teensy 4.1 = ARM Cortex-M7
WORKSPACE="$(realpath -m "${1:-$HOME/zephyr}")"

step()       { printf '\n\033[1;34m==> %s\033[0m\n' "$*"; }
warn()       { printf '\033[1;33mWARNING: %s\033[0m\n' "$*"; }
fail()       { printf '\n\033[1;31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }
version_ge() { printf '%s\n%s\n' "$2" "$1" | sort -V -C; }   # is $1 >= $2 ?

echo "Zephyr $ZEPHYR_VERSION workspace: $WORKSPACE"

# ---- 0. Sanity checks --------------------------------------------------------
[[ "$(uname -s)" == "Linux" ]] || fail "This script is for Linux. On Windows run installer_west.bat."
[[ $EUID -ne 0 ]]              || fail "Run as your normal user (not with sudo); the script asks for sudo when needed."
[[ "$WORKSPACE" != *" "* ]]    || fail "The workspace path must not contain spaces: $WORKSPACE"
case "$WORKSPACE" in
    *OneDrive*|*Dropbox*) fail "Put the workspace outside cloud-synced folders, e.g. ~/zephyr" ;;
esac

# ---- 1. Build tools ----------------------------------------------------------
step "1/6 Build tools (apt, needs sudo)"
if command -v apt-get >/dev/null 2>&1; then
    sudo apt-get update
    sudo apt-get install -y --no-install-recommends git cmake ninja-build gperf \
        ccache dfu-util device-tree-compiler wget \
        python3-dev python3-pip python3-setuptools python3-tk python3-wheel python3-venv \
        xz-utils file make gcc libsdl2-dev libmagic1
    # The multilib packages only exist on x86_64 hosts
    if [[ "$(uname -m)" == "x86_64" ]]; then
        sudo apt-get install -y --no-install-recommends gcc-multilib g++-multilib
    fi
    # Command-line Teensy loader, used to flash the board
    sudo apt-get install -y --no-install-recommends teensy-loader-cli \
        || warn "teensy-loader-cli package not available; get it from https://github.com/PaulStoffregen/teensy_loader_cli"
else
    warn "apt-get not found: install the Zephyr host dependencies for your distro by hand, then re-run."
fi

for tool in git cmake ninja gperf dtc wget; do
    command -v "$tool" >/dev/null 2>&1 || fail "'$tool' not found on PATH."
done
cmake_ver="$(cmake --version | awk 'NR==1 {print $3}')"
version_ge "$cmake_ver" "3.20.5" \
    || fail "CMake $cmake_ver is too old (need 3.20.5+). On Ubuntu older than 22.04, add the Kitware apt repository first."
echo "   cmake $cmake_ver - OK"

# ---- 2. Teensy USB permissions -----------------------------------------------
step "2/6 Teensy USB permissions (udev rules)"
RULES=/etc/udev/rules.d/00-teensy.rules
if [[ -f "$RULES" ]]; then
    echo "   $RULES already installed."
elif wget -qO /tmp/00-teensy.rules https://www.pjrc.com/teensy/00-teensy.rules; then
    sudo install -m 0644 /tmp/00-teensy.rules "$RULES"
    sudo udevadm control --reload-rules || true
    sudo udevadm trigger || true
    echo "   Installed. Unplug and replug the Teensy."
else
    warn "Could not download PJRC's udev rules, so flashing may need sudo. Get them from https://www.pjrc.com/teensy/00-teensy.rules"
fi

# ---- 3. Python virtual environment + west ------------------------------------
step "3/6 Python, virtual environment and west"
VENV="$WORKSPACE/.venv"
# Usable Python: 3.12+ (Zephyr v4.4.0 minimum) with venv/ensurepip (pip in the venv), ssl and ctypes
py_ok()   { "$1" -c 'import sys, venv, ensurepip, ssl, ctypes; sys.exit(0 if sys.version_info >= (3, 12) else 1)' >/dev/null 2>&1; }
venv_ok() { [[ -x "$VENV/bin/python" ]] && "$VENV/bin/python" -c 'import sys, ssl, ctypes; sys.exit(0 if sys.version_info >= (3, 12) else 1)' >/dev/null 2>&1; }

mkdir -p "$WORKSPACE"
if venv_ok; then
    echo "   Using the existing virtual environment in $VENV"
else
    if [[ -d "$VENV" ]]; then
        warn "The existing virtual environment is broken or older than Python 3.12, recreating it."
        rm -rf "$VENV"
    fi
    # System Python first (the apt packages above are for it), then any python3
    # on PATH (e.g. an activated conda / miniforge), then the usual conda folders.
    PY=""
    for cand in /usr/bin/python3 /usr/bin/python3.13 /usr/bin/python3.12 \
                "$(command -v python3 || true)" "$(command -v python3.13 || true)" "$(command -v python3.12 || true)" \
                "$HOME/miniforge3/bin/python3" "$HOME/miniconda3/bin/python3" "$HOME/anaconda3/bin/python3"; do
        if [[ -n "$cand" && -x "$cand" && "$cand" != "$VENV/"* ]] && py_ok "$cand"; then
            PY="$cand"
            break
        fi
    done
    [[ -n "$PY" ]] || fail "No usable Python 3.12+ found (Zephyr v4.4.0 needs it, with the venv, ssl and ctypes modules).
       Ubuntu 24.04 ships Python 3.12. On older systems install python3.12 and python3.12-venv, or use miniforge."
    if [[ -n "${CONDA_PREFIX:-}" && "$PY" == /usr/bin/python3 ]]; then
        echo "   conda is active, but using the system Python so the venv does not depend on conda."
    fi
    echo "   Using $PY ($("$PY" --version 2>&1))"
    "$PY" -m venv "$VENV"
    venv_ok || fail "The new virtual environment cannot load ssl/ctypes. Remove $VENV and check your Python install."
fi
set +u
# shellcheck disable=SC1091
source "$VENV/bin/activate"
set -u
python -m pip install --upgrade pip west
west --version

# ---- 4. Zephyr source --------------------------------------------------------
step "4/6 Zephyr $ZEPHYR_VERSION source (the first download takes a while)"
cd "$WORKSPACE"
if [[ -d .west ]]; then
    echo "   Workspace already initialised."
else
    west init --mr "$ZEPHYR_VERSION" "$WORKSPACE"
fi
if ! git -C "$WORKSPACE/zephyr" tag --points-at HEAD 2>/dev/null | grep -x "$ZEPHYR_VERSION" >/dev/null; then
    warn "zephyr/ is not checked out at $ZEPHYR_VERSION, the version this script pins."
    warn "To switch: git -C \"$WORKSPACE/zephyr\" checkout $ZEPHYR_VERSION   then re-run this script."
fi
west update
west zephyr-export

# ---- 5. Zephyr Python packages -----------------------------------------------
step "5/6 Zephyr Python packages"
if ! west packages pip --install; then
    warn "'west packages' not available in this Zephyr version, using requirements.txt"
    python -m pip install -r "$WORKSPACE/zephyr/scripts/requirements.txt"
fi

# ---- 6. Zephyr SDK -----------------------------------------------------------
step "6/6 Zephyr SDK ($TOOLCHAIN toolchain only)"
sdk_ver=""
[[ -f "$WORKSPACE/zephyr/SDK_VERSION" ]] && sdk_ver="$(tr -d '[:space:]' < "$WORKSPACE/zephyr/SDK_VERSION")"
if [[ -n "$sdk_ver" && -d "$HOME/zephyr-sdk-$sdk_ver" ]]; then
    echo "   Zephyr SDK $sdk_ver already installed in ~/zephyr-sdk-$sdk_ver"
else
    (cd "$WORKSPACE/zephyr" && west sdk install -t "$TOOLCHAIN")
fi

# ---- Done --------------------------------------------------------------------
cat <<DONE

=============================================================================
 Done.  In every new terminal, activate the environment first:
     source $VENV/bin/activate

 Test build for the Teensy 4.1:
     cd $WORKSPACE/zephyr
     west build -p always -b teensy41 samples/basic/blinky
     press the Teensy button, then:  west flash
=============================================================================
DONE
