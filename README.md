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

Done (tasks 1 to 8 of 19), all tested offline:

- build system, test harness, and UHD discovery that prefers the Homebrew install
- Gray-coded M-PSK constellation, PRBS, CRCs, scrambler
- root-raised-cosine pulse shaping and a streaming FIR filter
- preamble, header and payload framing, with the echo trailer
- transmit chain (modulator) and `cf32` sample file I/O
- receive front end (DC blocker and matched filter) and preamble acquisition: frame detection, carrier frequency (reliable to 7.5 kHz), timing, phase and level
- receive back end: cubic Farrow interpolator, decision-directed carrier loop, Gardner timing loop (detector gain measured at start-up), and the demodulator that decodes the header and payload
- metrics: EVM and EVM-based SNR, BER against known bits, residual frequency offset, level and clipping count
- the offline channel: delay (whole, fractional, longer than a frame), gain, carrier offset and phase, slow drift (frequency ramp and random-walk phase), sample-clock offset, and noise set from Es/N0
- a block receiver that finds and decodes every frame in a record
- one-way loopback (modulator, channel, receiver) with per-frame reports and aggregate BER, and the AWGN sweep with theory for comparison

Not done: the command-line modes (the loopback and the sweep are libraries and tests for now), the echo test, the
UHD radio layer, and 8-PSK. The `mpsk` executable is a
stub that reports the UHD versions it was built against.

Measured offline, QPSK with the defaults: the loopback is error-free with no impairments (over 20 000 bits),
and with carrier offsets of 250 Hz and 2.5 kHz, delays up to many frames, and a 60 dB range of levels. The AWGN
sweep from 0 to 10 dB Eb/N0 sits within 0.4 dB of ideal QPSK theory (about 0.1 dB from 4 dB up). At 0 dB Eb/N0
only 17% of frames get through, because the header has no error protection. The table is in the design.
None of this has touched a radio.

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
