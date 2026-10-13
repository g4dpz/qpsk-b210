# Implementation Plan

Work is split into two stages (Requirement 1.7). Stage 1 delivers the whole system with QPSK only. Stage 2 adds 8-PSK. Do not start stage 2 until the stage 1 checkpoint passes.

Each task builds on the ones before it. Build and run the tests at the end of every task. Hardware-only behaviour cannot be verified on this machine, because no USRP is attached; tasks say so where it applies.

## Stage 1: QPSK

- [x] 1. Project scaffolding and test harness
  - Create the directory layout from the design: `src/dsp`, `src/sim`, `src/link`, `src/radio`, `src/app`, `tests`, `cmake`.
  - Write the top-level `CMakeLists.txt`: CMake 3.16, C++17, `-Wall -Wextra`, libraries `mpsk_dsp`, `mpsk_sim`, `mpsk_link`, the `mpsk` executable, and CTest.
  - Add the `MPSK_WITH_UHD` option (default ON). When ON, find UHD with `HINTS /opt/homebrew` and `NO_DEFAULT_PATH` first, fall back to a default search with a warning, and print the UHD version and path at configure time.
  - Write the minimal test harness (`CHECK`, `CHECK_NEAR`, a test runner that returns non-zero on failure) and register one placeholder test with CTest.
  - Update `.gitignore` for `build/` and sample files.
  - Verify: configure with and without UHD, build, and run `ctest`.
  - _Requirements: 10.1, 10.2, 10.3, 10.4, 10.5, 10.6_

- [x] 2. Constellation, Gray coding, and bit utilities
  - [x] 2.1 Implement the generic M-PSK constellation and Gray mapper/demapper, with `supported_orders()` returning `{4}`. QPSK uses `φ₀ = π/4`. Asking for any other order fails with a clear message.
    - Explain the Gray code construction in comments.
    - _Requirements: 1.1, 1.3, 1.4, 1.5, 1.8_
  - [x] 2.2 Implement the PRBS generator (seed and frame counter select the sequence), CRC-16, CRC-32, and the additive scrambler (`x⁷ + x⁴ + 1`).
    - _Requirements: 2.1, 4.2_
  - [x] 2.3 Unit tests: unit average power; adjacent QPSK points differ by one bit; mapper/demapper round trip; known CRC vectors; scrambler round trip; PRBS repeatability.
    - _Requirements: 1.3, 1.4, 10.6_

- [x] 3. Pulse shaping
  - [x] 3.1 Implement the RRC tap generator from the closed form, including the `t = 0` and `t = ±T/(4β)` points, normalized to unit energy. Document the formula in comments.
    - _Requirements: 2.4, 2.5, 2.6, 10.7_
  - [x] 3.2 Implement a streaming complex FIR filter and the upsample-and-filter path.
  - [x] 3.3 Unit tests: taps are symmetric with unit energy; the two singular taps are finite and close to their neighbours; two cascaded RRC filters give near-zero inter-symbol interference at symbol instants.
    - _Requirements: 2.6, 10.6_

- [x] 4. Framing
  - [x] 4.1 Implement the 64-symbol BPSK preamble (m-sequence `x⁶ + x⁵ + 1` plus one chip).
  - [x] 4.2 Implement frame build and parse: 9-byte header (magic/version, type, modulation code, counter, length, CRC-16), scrambled BPSK header, M-PSK payload with CRC-32, and the 12-byte echo trailer.
    - _Requirements: 2.2, 2.3, 8.7_
  - [x] 4.3 Unit tests: build and parse round trip for data and echo frames; a corrupted header is rejected; a corrupted payload is flagged; preamble autocorrelation sidelobes are small.
    - _Requirements: 4.3, 10.6_

- [x] 5. Transmit chain
  - Implement the modulator: bits to frame to symbols to upsample and RRC filter to scaled samples with guard intervals. Default output scale 0.5, configurable.
  - Implement `cf32` sample file writing and reading (raw interleaved little-endian `float32` I/Q).
  - Unit test: a frame has the expected length and peak level, and a file write/read round trip is exact.
  - _Requirements: 2.1, 2.2, 2.7, 3.8_

- [x] 6. Receive front end: detection and estimation
  - [x] 6.1 Implement the DC blocker and the RRC matched filter stage. The DC blocker must prime from the mean of the first 256 samples after a reset, not from zero (leaves 76% of a DC offset across the first 2.7 ms frame) and not from a single sample (adds noise, up to 6 dB on a short record); see the design. Test a reset followed immediately by a frame carrying a DC offset.
    - _Requirements: 3.1, 3.2_
  - [x] 6.2 Implement the carrier-offset-tolerant preamble detector: four 16-symbol partial correlations combined non-coherently and normalized by received energy, with a configurable threshold and peak search, followed by a coherent verification stage (one threshold on the non-coherent statistic cannot both catch the edge of the frequency range and stay quiet on noise). Set the default threshold with two measured figures in mind: sidelobes of the statistic reach about 0.14 of the peak, and the peak itself drops 3.9 dB at the +-7.8 kHz edge of the acquisition range.
    - _Requirements: 3.5, 9.1, 9.3, 9.6_
  - [x] 6.3 Implement the preamble estimators: Luise-Reggiannini frequency estimate (limit ±Rs/32), parabolic fractional timing, and absolute phase and amplitude from the coherent peak.
    - _Requirements: 3.3, 3.4, 3.6, 7.3, 7.4, 9.4_
  - [x] 6.4 Unit tests: with a known offset, delay, and phase, the estimates land within tolerance; the detector works across a 40 dB level range and at offsets up to the edge of the acquisition range; the false-alarm count on pure noise stays below the target at the default threshold.
    - _Requirements: 9.3, 9.6, 10.6_

- [x] 7. Receive back end: tracking, demodulation, metrics
  - [x] 7.1 Implement the cubic Farrow interpolator, the Gardner timing detector and loop, and the decision-directed carrier loop with gains derived from damping and loop bandwidth. Document the derivation in comments.
    - _Requirements: 3.3, 3.4, 7.4, 7.5, 10.7_
  - [x] 7.2 Implement the demodulator: header decode (BPSK) with CRC check, payload decode, descramble, and CRC-32. A failed header reports the frame as lost.
    - _Requirements: 3.7, 4.3_
  - [x] 7.3 Implement metrics: EVM and SNR estimate, BER against known bits, residual frequency offset (coarse plus loop state), level in dBFS, and the clipping count.
    - _Requirements: 4.1, 4.2, 7.11, 7.12_
  - [x] 7.4 Unit tests: a clean modulated frame decodes with zero errors; EVM of a clean frame is near zero; BER counts match injected bit errors; the clipping count triggers on a saturated signal.
    - _Requirements: 4.1, 4.2, 10.6_

- [x] 8. Channel simulator and one-way loopback
  - [x] 8.1 Implement the channel model in the order from the design: bulk delay (integer and fractional), amplitude scale, carrier offset, phase offset and slow drift, sample-clock offset in ppm, then AWGN with `σ² = Pₛ · sps / (Eₛ/N₀)`.
    - _Requirements: 6.2, 6.5, 6.6_
  - [x] 8.2 Implement the one-way loopback driver: modulator, channel, receiver, with per-frame reports and aggregate BER.
    - _Requirements: 6.1, 4.1_
  - [x] 8.3 Integration tests with QPSK: noiseless BER exactly zero over at least 10,000 bits; carrier offsets of 1e-3 and 1e-2 of the symbol rate with random phase and fractional timing; a small offset below 1e-4 with a few ppm of clock offset and slow drift; delays of zero, a fraction of a frame, and over one frame with amplitude scaled across at least 40 dB.
    - _Requirements: 6.3, 6.4, 6.5, 6.6_
  - [x] 8.4 Add the AWGN sweep from 0 to 12 dB `Eb/N0`, with fixed seeds, running each point until 100 errors or a bit cap. Check it is monotonic and within about 1 dB of the QPSK theory table.
    - _Requirements: 6.8_

- [x] 9. Command-line interface
  - Write the hand-written option parser (`--name value`, per-subcommand `--help`, errors that name the bad option).
  - Create the `mpsk` executable with subcommands `loopback` and `echo-sim`, and stubs for `tx`, `rx`, `echo-init`, `echo-resp` that print "built without UHD" when UHD is off.
  - Wire the common, receiver, and simulation options from the design. Reject `--mod 8psk` in stage 1 with a clear message.
  - Unit tests for the parser: defaults, overrides, unknown option, missing value.
  - Done as: `loopback` works, with single runs, `--sweep`, and `--tx-out`/`--rx-out`. `echo-sim` and the four hardware commands are declared and answer `--help`; they exit 1 with a message until their tasks (11, 12, 13) land. `mpsk version` replaces the bare `mpsk` that CI used to run.
  - _Requirements: 1.1, 1.2, 1.8, 10.4_

- [x] 10. Checkpoint: DSP and loopback
  - Build from scratch, with no warnings. Run `ctest`, then run the `loopback` subcommand for the noiseless, impairment, delay, and AWGN cases with QPSK, and record the measured numbers for the README.
  - If anything fails, fix it before moving on. Ask the user if a question comes up.
  - Done: clean builds from scratch with and without UHD, zero warnings; 27 and 26 tests pass; the Release binary links `/opt/homebrew/lib/libuhd` and the software-only binary links no UHD or Boost. Results are in the README. One finding, documented in the design and not fixed: the DC blocker removes the genuine DC of a dense train of short bursts (EVM up to about 4%, no bit errors).

- [ ] 10A. Narrowband profile for the QO-100 narrowband transponder (2000 Bd, roll-off 0.25)
  - Added after task 10. Requirement 12; design section "Narrowband profile". Do before task 11, because the echo timing and the simulated radio depend on the rates.
  - [ ] 10A.1 Add `--baud` (symbol rate; the sample rate becomes baud times samples per symbol) and a `--profile qo100-nb` preset to the CLI: QPSK, 2000 Bd, roll-off 0.25, 4 samples per symbol, span 21 if 10A.2 confirms it. An explicit option overrides the preset. Update the loopback header line and the tests.
    - _Requirements: 12.1_
  - [ ] 10A.2 Measure the occupied bandwidth of the modulator output (a function in the DSP library, not just a test helper) and add a test: the −26 dB width at the profile is at most 2.7 kHz and the 99% width is reported; run it for spans 11 and 21 and record both. Check the receiver and loops with span 21 (loopback at the profile, clean and with noise) before adopting it.
    - _Requirements: 12.2_
  - [ ] 10A.3 Make the DC blocker's cutoff a frequency in hertz and its priming window a time in milliseconds, both derived from the sample rate. Keep the 1 MS/s behaviour unchanged (same test results). Re-measure the burst-train effect found in task 10 at 8 kS/s.
    - _Requirements: 12.4_
  - [ ] 10A.4 Wide-range frequency acquisition without changing the frame format (a tone ahead of the frame was rejected by the user). Proposed: run the stage-1 detector over frequency hypotheses about 100 Hz apart across a configurable search range (default ±1 kHz), take the best, and let the existing estimators refine it. Confirm the method and the range with the user first. Tests: acquire offsets across the whole search range at the profile, with noise; the false-alarm rate on noise stays at the target with all hypotheses searched; the ordinary path (offset inside ±60 Hz) gives the same results as before; measure the cost.
    - _Requirements: 12.5_
  - [ ] 10A.5 A polyphase resampler (rational ratio, linear phase) for the radio layer, with a compensated group delay. Tests: images and aliases below a stated level, flat passband across the 2.5 kHz signal, a round trip DSP rate to radio rate and back that recovers the signal, and the exact delay.
    - _Requirements: 12.3_
  - [ ] 10A.6 Repeat the task 8 and task 9 results at the profile: noiseless, carrier offsets scaled to the symbol rate, clock offset and drift, delays and levels, and the AWGN sweep against theory. Record them in the README.
    - _Requirements: 12.1, 6.3, 6.4, 6.5, 6.6, 6.8_
  - [ ] 10A.7 A receive option `--invert-iq`, tested by conjugating a clean frame and checking that it decodes only with the option.
    - _Requirements: 12.9_

- [ ] 11. Radio interface, simulated radio, and echo logic
  - [ ] 11.1 Define `IRadio` (`sample_rate`, `now`, `receive`, `send_burst`) and the `RxBlock` and `SendResult` types.
    - _Requirements: 5.4, 8.5_
  - [ ] 11.2 Implement `SimRadio`: a per-node device clock, time-stamped receive blocks, timed bursts that return `Late` when scheduled in the past, and a separate channel per direction.
    - _Requirements: 6.7, 8.6_
  - Satellite profile (Requirement 12): the initiator accepts only echo frames and the responder only data frames, counting the ones they ignore (a station hears its own frames through the satellite about 0.25 s later); the profile defaults are `T_turn` 0.5 s, `--max-range-km` 84 000, timeout about 1.5 s, interval 2 s. `SimRadio` must support a path delay of 0.25 s per hop.
  - [ ] 11.3 Implement the echo initiator: PRBS frames, timed send, receive until echo or timeout, per-exchange report, summary, CSV log.
    - _Requirements: 8.1, 8.3, 8.8, 8.9, 8.10, 8.11, 8.12, 7.15_
  - [ ] 11.4 Implement the echo responder: decode, build an echo (`decoded` mode by default, `reference` mode selectable) with the forward-leg trailer, send at the fixed turnaround, skip and count late echoes, print margin and running counts.
    - _Requirements: 8.1, 8.4, 8.5, 8.6, 8.7, 8.13_
  - [ ] 11.5 Implement half-duplex handling: discard samples during the unit's own transmit window and reset the DC blocker and detector afterwards.
    - _Requirements: 8.2, 8.14_
  - [ ] 11.6 Implement the RTT and one-way delay estimate, with `--rtt-offset-us` calibration.
    - _Requirements: 8.11, 9.7_
  - [ ] 11.7 Wire up the `echo-sim` subcommand. Integration tests with QPSK: zero errors at high SNR; one-way delay within one sample period of the simulated value; a cut channel counts a lost echo; a too-short turnaround counts a skipped echo.
    - _Requirements: 6.7, 8.6, 8.8, 8.11_

- [ ] 12. UHD radio layer
  - [ ] 12.1 Implement device selection: `uhd::device::find` first; zero, several, or busy matches print a clear message and exit non-zero; no exception or backtrace reaches the user. Default `--args type=b200`.
    - _Requirements: 5.8, 5.9, 5.10_
  - Satellite profile: the radio runs at its supported rate through the resampler of 10A.5, tunes with an LO offset (10A.3), and tunes transmit and receive independently (uplink 2.4 GHz band on `TX/RX`, downlink through a downconverter on `RX2`).
  - [ ] 12.2 Implement clock setup in the order from the design: master clock, clock source, wait for `ref_locked` with a timeout, then rate, frequency, gain, bandwidth, antenna. Read back the actual sample rate and exit on a mismatch. A missing `ref_locked` sensor prints a warning and continues. Print the clock source and lock state at startup.
    - _Requirements: 5.1, 5.2, 5.3, 5.12, 5.13, 5.14, 5.15_
  - [ ] 12.3 Implement `UsrpRadio`: `fc32`/`sc16` streamers, timed bursts with start and end of burst, async message handling (`U`, late), continuous receive with time stamps and overflow reporting (`O`), and the Ctrl-C handler.
    - _Requirements: 5.4, 5.5, 5.6, 5.7, 5.11_
  - [ ] 12.4 Verify what can be verified here: build with UHD on; run `otool -L` on `mpsk` and confirm it links `/opt/homebrew/lib/libuhd*.dylib`, not `/usr/local/lib`; confirm the compile used the Homebrew headers; run `mpsk tx` and `mpsk rx` with no radio attached and confirm a readable error and a non-zero exit.
    - _Requirements: 5.8, 10.5_
  - Streaming, timed transmit, and reference lock cannot be tested without hardware. Say so in the task report.

- [ ] 13. Hardware subcommands and safety defaults
  - Implement `tx`, `rx`, `echo-init`, and `echo-resp` on top of `UsrpRadio` and the shared logic, including file output for `tx --out` and file input for `rx --in`.
  - Defaults: TX gain 10 dB, warning above 40 dB, RX gain 30 dB, `TX/RX` antenna for TX and for both echo roles, `RX2` for one-way `rx`. Clamp gains to the device's reported ranges and say so when clamped.
  - Print RX gain, level in dBFS, the clipping warning, and loss-of-signal reports, and reacquire after a gap.
  - File-based `tx` and `rx` round trip is testable offline; the radio paths are not.
  - _Requirements: 2.7, 3.8, 5.2, 7.7, 7.8, 7.11, 7.12, 7.13, 9.5_

- [ ] 14. Stage 1 documentation
  - Write `README.md`: what it is, the 8-PSK interpretation, the B200-mini versus B210 note, Homebrew install and build steps, every option with its default, a worked example for `tx`, `rx`, `loopback`, `echo-sim`, `echo-init`/`echo-resp`, and file I/O, the measured stage 1 results, the DSP chain block by block, and troubleshooting (underflows and overflows, gain and rate limits, USB 3, `uhd_images_downloader`).
  - Add the back-to-back and over-the-air procedures from the design: cabling and the 10 MHz connections, at least 30 dB of attenuation, the −15 dBm RX maximum and +20 dBm TX maximum from the Ettus manual, a note to confirm the reference level and oscillator accuracy against the datasheet, GPSDO lock checked by the user, which unit to start first, the staged order (loopback, conducted, over the air at low power), and the regulatory warning.
  - State plainly what has not been tested on hardware.
  - _Requirements: 1.6, 1.11, 7.13, 7.14, 8.17, 9.2, 9.8, 9.9, 11.1, 11.2, 11.3, 11.4, 11.5, 11.6_

- [ ] 15. Checkpoint: stage 1 complete
  - Clean build with no warnings, all tests pass, loopback and `echo-sim` pass with QPSK, the `otool` check and the no-hardware behaviour confirmed, README results filled in.
  - Ask the user whether to proceed to stage 2, and whether they have run the QPSK hardware tests.

## Stage 2: add 8-PSK

- [ ] 16. Enable 8-PSK in the generic blocks
  - Add 8 to `supported_orders()`, with `φ₀ = 0`, and allow `--mod 8psk`.
  - Extend the constellation tests to 8-PSK: unit power, adjacent points differ by one bit, round trip.
  - Check the frame build and parse code with 8-PSK payloads (352 symbols for the default payload).
  - _Requirements: 1.1, 1.3, 1.4, 1.5, 1.9_

- [ ] 17. Verify and retune the receiver for 8-PSK
  - Run the same integration tests as task 8.3 with 8-PSK: noiseless BER exactly zero; carrier offsets; clock offset and drift; delay and amplitude.
  - Retune only the carrier loop gains and the timing loop bandwidth if needed (decision regions shrink from ±45° to ±22.5°). Do not change framing, pulse shaping, the radio layer, or the echo logic unless a defect is found; record any such defect.
  - If cycle slips occur, add optional pilot symbols to the framer as described in the design, and re-run both modulations.
  - Run the AWGN sweep for 8-PSK and check it against the 8-PSK theory (about 1e-3 at 10 dB, and about 3.4 dB worse than QPSK at 1e-5), within about 1.5 dB.
  - _Requirements: 1.9, 1.10, 6.3, 6.4, 6.5, 6.6, 6.8_

- [ ] 18. 8-PSK echo, defaults, and documentation
  - Run `echo-sim` with 8-PSK and confirm the same results as QPSK. Re-run the QPSK tests to confirm nothing regressed.
  - Make 8-PSK the default modulation once the above passes, and update the tests that assume the default.
  - Update the README: 8-PSK results, the QPSK versus 8-PSK comparison, and the hardware order (QPSK conducted, QPSK over the air, 8-PSK conducted, 8-PSK over the air).
  - _Requirements: 1.2, 1.10, 1.11, 6.7, 11.4_

- [ ] 19. Final checkpoint
  - Clean build with no warnings, all tests pass for both modulations, `otool` check repeated, no-hardware behaviour repeated, README results complete.
  - Report to the user what was verified offline and what remains for the hardware tests.
