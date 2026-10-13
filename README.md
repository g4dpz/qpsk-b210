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

Done (tasks 1 to 10 of 19), all tested offline:

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

- the `mpsk` command line: a hand-written option parser, and `mpsk loopback` for single runs, Eb/N0 sweeps and sample files

Not done: the echo test, the UHD radio layer, and 8-PSK. `mpsk tx`, `rx`, `echo-init`, `echo-resp` and `echo-sim`
exist but only say they are not available yet (without UHD: "built without UHD").

The measured offline results are below. None of this has touched a radio.

The requirements, design and task list are in
`.kiro/specs/mpsk-b200mini-transceiver/`.

## Measured results so far (stage 1, QPSK, offline)

From `mpsk loopback` on the stage 1 checkpoint (task 10): 1 MS/s, 4 samples per symbol (250 ksym/s), roll-off 0.35,
128-byte payloads unless stated, Release build. "High SNR" is Es/N0 = 25 dB, where the ideal BER is far below
anything measurable, so any bit error would be the receiver's. This is the software chain only: no hardware
effects (oscillator phase noise, converter limits, I/Q imbalance, multipath) are simulated.

| Case | Frames delivered | Bits compared | Bit errors |
|---|---|---|---|
| No impairments | 100 of 100 | 102 400 | 0 |
| No impairments, 8-byte payloads | 500 of 500 | 32 000 | 0 |
| Carrier offset +250 Hz, -250 Hz (1e-3 of the symbol rate), random phase, fractional timing | 50 of 50 each | 51 200 each | 0 |
| Carrier offset +2.5 kHz, -2.5 kHz (1e-2 of the symbol rate), random phase, fractional timing | 50 of 50 each | 51 200 each | 0 |
| Carrier offset +7 kHz (the reliable limit is 7.5 kHz) | 50 of 50 | 51 200 | 0 |
| Carrier offset +9 kHz | 0 of 10 | none | none found: outside the acquisition range |
| 20 Hz offset, 5 ppm clock offset, 200 Hz/s drift, phase walk 2 rad/√s, 100 frames (0.3 s) | 100 of 100 | 102 400 | 0 |
| 20 Hz offset, +200 ppm or -200 ppm clock offset, 100 frames | 100 of 100 each | 102 400 each | 0 |
| Frequency drift 20 kHz/s (the offset sweeps 0 to 6 kHz during the capture) | 100 of 100 | 102 400 | 0 |
| Phase walk 10 rad/√s | 100 of 100 | 102 400 | 0 |
| Delay of 0, 1234.5, 5000.3 and 30 000.6 samples (up to nearly 11 frames), each at amplitudes of +20, 0, -20 and -40 dB (60 dB range) | all 16 cases found every frame (10 or 3 frames each) | 3072 to 10 240 each | 0 |

The receiver's carrier offset reading matched the applied offset to within 0.2 Hz in every carrier offset case above.
The EVM-based SNR reads 24.6 dB for Es/N0 = 25 dB, independent of the amplitude and the delay.

AWGN sweep (`mpsk loopback --sweep 0:12:1 --max-bits 3e7 --frames 100`, 18 s). "Loss" is the extra Eb/N0 the
receiver needed over ideal QPSK theory to reach the measured BER:

| Eb/N0 | BER measured | Errors / bits compared | Theory | Loss | Frames delivered |
|---|---|---|---|---|---|
| 0 dB | 8.8e-2 | 1534 / 17 408 | 7.9e-2 | 0.39 dB | 17 of 100 |
| 1 dB | 6.3e-2 | 2305 / 36 864 | 5.6e-2 | 0.29 dB | 36 of 100 |
| 2 dB | 4.2e-2 | 2684 / 64 512 | 3.8e-2 | 0.24 dB | 63 of 100 |
| 3 dB | 2.6e-2 | 2146 / 83 968 | 2.3e-2 | 0.21 dB | 82 of 100 |
| 4 dB | 1.3e-2 | 1229 / 93 184 | 1.25e-2 | 0.08 dB | 91 of 100 |
| 5 dB | 7.2e-3 | 720 / 100 352 | 6.0e-3 | 0.23 dB | 98 of 100 |
| 6 dB | 2.5e-3 | 257 / 101 376 | 2.4e-3 | 0.06 dB | 99 of 100 |
| 7 dB | 8.1e-4 | 165 / 204 800 | 7.7e-4 | 0.03 dB | all |
| 8 dB | 2.3e-4 | 119 / 512 000 | 1.9e-4 | 0.13 dB | all |
| 9 dB | 3.9e-5 | 100 / 2.56 M | 3.4e-5 | 0.08 dB | all |
| 10 dB | 5.6e-6 | 100 / 17.7 M | 3.9e-6 | 0.16 dB | all |
| 11 dB | 2.3e-7 | 7 / 30 M | 2.6e-7 | not measured (7 errors) | all |
| 12 dB | none seen | 0 / 30 M | 9.0e-9 | not measured | all |

The loss is 0.4 dB or less at every point that collected its errors, and about 0.1 dB from 4 dB up. The BER is over
delivered frames. The header has no error protection, so at low Eb/N0 whole frames are lost to it: at 0 dB only 17 of
100 frames get through. That is a property of the framing, not the payload BER.

Two things to know about these numbers. First, the receiver's DC blocker cannot tell a station's DC offset from the DC of its
own bursts, so in a dense train of short bursts it removes a little of the signal: in a noiseless train of 500 8-byte frames the
EVM rises from 0.8% in the first frame to about 4% by the hundredth and stays there (still no bit errors), and a train of 128-byte
frames averages an EVM SNR of 37 dB instead of the 40 dB of a single frame. The echo test sends one frame every 200 ms, so this
will matter much less there; I have not measured it. Second, none of this says how fast the receiver runs on a Raspberry Pi or
how it behaves with a real radio.

## Using it

The offline loopback needs no hardware:

```sh
build/src/app/mpsk --help
build/src/app/mpsk loopback --frames 20                      # clean channel, one line per frame
build/src/app/mpsk loopback --ebn0 8 --cfo-hz 1500 --delay-samples 700.5 --amp-db -30
build/src/app/mpsk loopback --sweep 0:10:2 --max-bits 2e6    # BER against Eb/N0, with theory
build/src/app/mpsk loopback --frames 3 --tx-out tx.cf32 --rx-out rx.cf32
build/src/app/mpsk loopback --help                           # every option, with defaults and ranges
```

A run exits 0 whenever it completes; the report carries the result. A bad option exits 2 and names it.
`--mod 8psk` is refused until stage 2.

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
