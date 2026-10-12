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

Done (tasks 1 to 5 of 19), all tested offline:

- build system, test harness, and UHD discovery that prefers the Homebrew install
- Gray-coded M-PSK constellation, PRBS, CRCs, scrambler
- root-raised-cosine pulse shaping and a streaming FIR filter
- preamble, header and payload framing, with the echo trailer
- transmit chain (modulator) and `cf32` sample file I/O

Not done: the receiver, the channel simulator and loopback, the echo test, the
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
