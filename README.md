# Robotic-Dog

Welcome to **Robotic-Dog**, a quadruped robot whose software is written in plain C++, without ROS. The computer software runs on Linux, and each leg is driven by a Teensy 4.1 running Zephyr RTOS firmware.

## Repositories and wikis

| Repository | What it holds | Wiki |
|---|---|---|
| [Robotic-Dog](https://github.com/artorias961/Robotic-Dog) (this repo) | The main project: computer software, tools, and installers | [Robotic-Dog wiki](https://github.com/artorias961/Robotic-Dog/wiki): plan, requirements, design, and test plan |
| [Robotic-Dog-Leg](https://github.com/artorias961/Robotic-Dog-Leg), included at `submodule/legs` | The leg firmware for the Teensy 4.1 (Zephyr RTOS, C++20) | [Robotic-Dog-Leg wiki](https://github.com/artorias961/Robotic-Dog-Leg/wiki): setup, build, and flash guides |
| [Robotic-Dog-Electrical](https://github.com/artorias961/Robotic-Dog-Electrical), included at `submodule/Electrical` | The schematics, PCB designs, and electrical engineering documentation | [Robotic-Dog-Electrical wiki](https://github.com/artorias961/Robotic-Dog-Electrical/wiki): electrical design, PCB layout, assembly, and testing |

## Fresh clone

> [!IMPORTANT]
> Clone into a folder with **no spaces** in its path, for example `C:\dev` or `~/dev`. Zephyr cannot build the leg firmware otherwise.

```sh
git clone --recurse-submodules https://github.com/artorias961/Robotic-Dog.git
cd Robotic-Dog
```

`--recurse-submodules` also downloads the leg firmware into `submodule/legs` and the electrical hardware files into `submodule/Electrical`. If either folder is empty because you cloned without submodules, run:

```sh
git submodule update --init --recursive
```

To work on the leg firmware or electrical hardware, run `git switch main` inside the corresponding submodule folder first, because submodules are initially checked out at a fixed commit rather than on a branch ;)

Next, follow [00 ‐ Installation and Setup](https://github.com/artorias961/Robotic-Dog-Leg/wiki/00-%E2%80%90-Installation-and-Setup) in the Robotic-Dog-Leg wiki.
