# BLock

A simple, customizable, lockscreen that I made because I was bored.

> Currently only true PNG images are supported. If you want to use something
> other than PNGs, install imagemagick and use the `convert` command in a
> wrapper shell script.

Features so far:

- All colours everywhere are customizable
- 4 different background scaling modes
- Built-in support for mulit-monitor setups (using RandR)
- Intergrates seemlessly using PAM
- Has a smol clock that can have any custom format used (or positioned, etc.)

## Requirements

- pkg-config
- libxcb
- libpam-dev
- libcairo-dev
- libxcb-randr
- libev
- libxkbcommon >= 0.5.0
- libxkbcommon-x11 >= 0.5.0

## Running

To start BLock, just run `block <image>`. For full usage, check `block -?`.
