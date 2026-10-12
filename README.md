# qpsk-b210: M-PSK transceiver for the USRP B200-mini (work in progress)

A from-scratch C++17 transceiver for the Ettus USRP B200-mini, built on UHD.
It is built in two stages: QPSK first, then 8-PSK added on top. A half-duplex
echo test between two radios measures both directions of the link and the
round-trip delay.

This branch is separate from `main`, which holds the earlier QPSK codec. Its
files were removed from this branch only, and can be brought back one at a time
with `git checkout origin/main -- <path>`.

## Status

This is not usable as a radio yet. Nothing here has been run on hardware.

Done (tasks 1 to 6 of 19), all tested offline:

- build system, test harness, and UHD discovery that prefers the Homebrew install
- Gray-coded M-PSK constellation, PRBS, CRCs, scrambler
- root-raised-cosine pulse shaping and a streaming FIR filter
- preamble, header and payload framing, with the echo trailer
- transmit chain (modulator) and `cf32` sample file I/O
- receive front end (DC blocker and matched filter) and preamble acquisition: frame detection, carrier frequency (reliable to 7.5 kHz), timing, phase and level
- channel building blocks for the offline simulator: fractional delay, carrier offset, calibrated noise

Not done: the tracking loops and demodulator, the channel model and loopback driver, the echo test, the
UHD radio layer, the command-line modes, and 8-PSK. The `mpsk` executable is a
stub that reports the UHD versions it was built against.

The requirements, design and task list are in
`.kiro/specs/mpsk-b200mini-transceiver/`.

## Building and testing

Needs CMake 3.16 or newer and a C++17 compiler. UHD is optional.

```sh
# With UHD (the Homebrew install under /opt/homebrew is preferred)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure

# Without UHD: DSP, simulation and link layers only
cmake -S . -B build -DMPSK_WITH_UHD=OFF
```

There are no third-party dependencies beyond UHD itself.

## Continuous integration

GitHub Actions builds and tests every push to `main` and `feature/**`, and every
pull request into `main`, on Ubuntu and macOS, with warnings treated as errors.
It covers the software-only build (Release and Debug) and a build against UHD
(4.6 from apt on Ubuntu, 4.10 from Homebrew on macOS). The runners have no
radio, so CI runs the offline tests only. To reproduce a CI build locally, add
`-DMPSK_WARNINGS_AS_ERRORS=ON` to the cmake command.
