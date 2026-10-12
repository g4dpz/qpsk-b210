# Requirements Document

## Introduction

This feature is a C++17 project built on the Ettus UHD library that transmits and receives M-PSK signals using a USRP B200-mini. The goal modulation is 8-PSK (3 bits per symbol). QPSK (2 bits per symbol) comes first: the whole system is built and verified with QPSK, then 8-PSK is added on top of it. Both stay selectable afterwards.

"8QPSK" is not a standard constellation name. This spec reads it as 8-PSK, and keeps QPSK as a first-class mode because the project directory is named `qpsk-b210`. Requirement 1 records this reading so it can be corrected before design starts.

The B200-mini and B210 use the same B2xx UHD driver (`type=b200`), so one code path serves both. No device serials or product strings are hard-coded.

The DSP chain is kept independent of UHD so it can be tested offline. No USRP is attached to the development machine, so an offline loopback simulation is the main way to verify correctness before hardware is available. The main hardware test is a half-duplex echo: one unit (the initiator) sends a frame, the other (the responder) receives it and sends it back, and the initiator checks what returns. This exercises both directions of the link, and the round trip also measures the propagation delay. The first hardware test is a conducted, back-to-back link between two B200-mini units joined through fixed attenuators, used for bring-up. The real use is over the air between two separated units, where the signal arrives after an unknown propagation delay, with a path loss that varies and possibly a Doppler shift if either unit moves. Each unit is fed its own 10 MHz reference from a separate GPSDO-disciplined source, so the frequency offset between them should be far smaller than with free-running oscillators. The two references are not locked to each other, so a small residual frequency offset, a slowly drifting carrier phase, and a tiny sample-clock offset all remain. The receiver tracks these. It can also run on the internal oscillator when no reference is connected. Behaviour that needs hardware is called out explicitly.

## Requirements

### Requirement 1: Modulation scheme selection

**User Story:** As an RF developer, I want to choose between 8-PSK and QPSK, so that I can compare spectral efficiency against noise robustness on the same hardware.

#### Acceptance Criteria

1. THE System SHALL support 8-PSK and QPSK, selected by a command-line option.
2. WHEN no modulation is specified, THE System SHALL use QPSK during stage 1 and SHALL use 8-PSK once stage 2 verification passes (see criteria 7 to 10).
3. THE System SHALL use Gray-coded mappings, so that adjacent constellation points differ by exactly one bit.
4. THE System SHALL normalize every constellation to unit average symbol power.
5. THE System SHALL implement the mapper and demapper generically over the modulation order M, so that another M-PSK order can be added without changing the surrounding chain.
6. THE System SHALL document in the README that "8QPSK" was interpreted as 8-PSK.
7. THE System SHALL be built in two stages. Stage 1 delivers the complete system with QPSK only: DSP chain, loopback, echo test, radio layer, and documentation. Stage 2 adds 8-PSK.
8. THE stage 1 system SHALL pass all the loopback and echo requirements with QPSK, with 8-PSK disabled and rejected with a clear message, before any 8-PSK work starts.
9. THE stage 2 work SHALL add 8-PSK through the generic mapper and through the parts of the receiver that depend on the modulation order (carrier recovery and phase-ambiguity resolution), and SHALL NOT change the framing, pulse shaping, timing recovery, radio layer, or echo logic, except where verification shows a defect.
10. THE stage 2 system SHALL pass the same loopback and echo requirements with 8-PSK that stage 1 passed with QPSK, and SHALL still pass them with QPSK.
11. THE README SHALL recommend running the hardware tests in the same order: QPSK conducted echo, then QPSK over the air, then 8-PSK conducted echo, then 8-PSK over the air.

### Requirement 2: Transmit signal generation

**User Story:** As an RF developer, I want to generate a framed, pulse-shaped M-PSK signal, so that a receiver can synchronize to it and measure link quality.

#### Acceptance Criteria

1. THE Transmitter SHALL take its payload bits from either a seeded pseudorandom generator or an input file.
2. THE Transmitter SHALL prefix each frame with a known preamble sequence that a receiver can detect by correlation.
3. THE Transmitter SHALL include in each frame a header carrying a frame counter, a payload length, and a frame type (data or echo).
4. THE Transmitter SHALL shape symbols with a root-raised-cosine filter.
5. THE Transmitter SHALL accept configurable samples per symbol (default 4), roll-off (default 0.35), and filter span in symbols (default 11).
6. THE Transmitter SHALL generate the RRC taps from the closed-form expression, including the singular points at t = 0 and t = ±T/(4β).
7. WHERE a sample output file is specified, THE Transmitter SHALL write the generated baseband samples to that file instead of, or in addition to, streaming them to hardware.

### Requirement 3: Receive and demodulate

**User Story:** As an RF developer, I want the receiver to recover bits from impaired samples, so that I can measure link quality over the air.

#### Acceptance Criteria

1. THE Receiver SHALL remove DC offset from incoming samples.
2. THE Receiver SHALL apply a matched RRC filter whose parameters equal the transmitter's.
3. THE Receiver SHALL estimate and correct carrier frequency and phase offset for the selected M-PSK order.
4. THE Receiver SHALL recover symbol timing, including fractional offsets.
5. THE Receiver SHALL locate frame starts by correlating against the known preamble.
6. THE Receiver SHALL use the preamble to resolve the M-fold phase ambiguity left by carrier recovery.
7. THE Receiver SHALL Gray-demap recovered symbols to bits.
8. WHERE a sample input file is specified, THE Receiver SHALL read baseband samples from that file instead of from hardware.

### Requirement 4: Link-quality reporting

**User Story:** As an RF developer, I want per-frame statistics, so that I can see how the link behaves over time.

#### Acceptance Criteria

1. FOR each decoded frame, THE Receiver SHALL report the frame index, the estimated EVM or SNR, the bit error rate, and the residual frequency offset.
2. WHEN the expected payload is known, THE Receiver SHALL compute bit error rate against it.
3. WHEN a frame header cannot be decoded, THE Receiver SHALL report the frame as lost instead of reporting statistics for it.

### Requirement 5: USRP B200-mini radio interface

**User Story:** As an RF developer, I want to stream samples to and from the B200-mini, so that I can run the link over the air.

#### Acceptance Criteria

1. THE System SHALL configure the radio through `uhd::usrp::multi_usrp`, with configurable center frequency (default 2.45 GHz), sample rate, gain, bandwidth, and antenna.
2. THE System SHALL query the device for its valid gain range at runtime and SHALL NOT hard-code gain limits.
3. THE System SHALL choose a sample rate that divides the B200-mini master clock cleanly, and SHALL document the choice.
4. THE Transmitter SHALL use a `tx_streamer` with correct start-of-burst and end-of-burst metadata.
5. WHEN an underflow occurs, THE Transmitter SHALL report it to the user.
6. WHEN an overflow occurs, THE Receiver SHALL report it to the user.
7. WHEN the user presses Ctrl-C, THE System SHALL stop streaming and exit cleanly.
8. IF no USRP is found, THEN THE System SHALL print a clear, human-readable error and exit with a non-zero status, without crashing or printing an unhandled exception.
9. THE System SHALL accept a UHD device-address string (for example `serial=...`) for tx and rx modes, so that each mode can open a specific unit when two B200-mini units are connected to the same host.
10. IF the device-address string matches more than one USRP, or the matched unit is already in use, THEN THE System SHALL print a clear error naming the problem and SHALL NOT silently pick a unit.
11. THE System SHALL support running tx and rx as separate processes, on one host or on two hosts.
12. THE System SHALL accept a clock-source option with at least `internal` and `external` (10 MHz reference), defaulting to `internal`.
13. WHEN the external clock source is selected, THE System SHALL wait for the reference lock sensor to report locked, with a timeout, before streaming.
14. IF the reference does not lock within the timeout, THEN THE System SHALL print a clear error naming the clock source and exit with a non-zero status, instead of streaming on an unlocked reference.
15. THE System SHALL print the clock source and lock state at startup.

### Requirement 6: Offline loopback simulation

**User Story:** As an RF developer without hardware on hand, I want to run the whole chain in software, so that I can verify the DSP before using a radio.

#### Acceptance Criteria

1. THE System SHALL provide a loopback mode that runs transmitter, simulated channel, and receiver with no hardware.
2. THE simulated channel SHALL support configurable AWGN, carrier frequency offset, carrier phase offset, fractional timing offset, bulk propagation delay (whole and fractional samples, including a delay longer than one frame), and received amplitude scaling.
3. WHEN loopback runs with no impairments on a payload of at least 10,000 bits, THE System SHALL produce a bit error rate of exactly zero for QPSK in stage 1, and for both QPSK and 8-PSK in stage 2.
4. WHEN loopback runs with a carrier frequency offset of 1e-3 and then 1e-2 of the symbol rate, a random phase offset, and a fractional timing offset at high SNR, THE Receiver SHALL converge and keep the bit error rate low.
5. THE simulated channel SHALL also support a sample-clock offset in ppm and slow carrier phase drift, and WHEN loopback runs with a small carrier frequency offset (below 1e-4 of the symbol rate), a sample-clock offset of a few ppm, and slow phase drift at high SNR, THE Receiver SHALL hold lock for the whole capture and keep the bit error rate low.
6. WHEN loopback runs with a bulk propagation delay of zero, a fraction of a frame, and more than one frame, and with received amplitude scaled by at least 40 dB across the cases, THE Receiver SHALL find the frames in every case and produce a bit error rate of zero at high SNR.
7. WHEN the echo test runs in loopback, with two simulated nodes joined by a simulated channel in each direction, each direction having its own propagation delay, frequency offset, and amplitude scale, THE System SHALL complete the exchanges with zero bit errors at high SNR, and the estimated one-way delay SHALL match the simulated delay to within one sample period.
8. WHEN loopback runs an AWGN sweep, THE measured bit error rate SHALL decrease monotonically with SNR and SHALL be within an order of magnitude of the theoretical M-PSK bit error rate.

### Requirement 7: Back-to-back hardware test with two B200-mini units

**User Story:** As an RF developer, I want to connect two B200-mini units through attenuators and run the link between them, so that I can measure real hardware performance without radiating.

#### Acceptance Criteria

1. THE System SHALL support a conducted test in which one B200-mini transmits and a second B200-mini receives, joined by cable and fixed attenuators.
2. THE System SHALL support a test in which each unit uses its own 10 MHz external reference from a separate GPSDO-disciplined source.
3. THE Receiver SHALL acquire and track the residual carrier frequency offset between the two units, and SHALL NOT assume the offset is zero, because the two references are independent.
4. THE Receiver SHALL NOT assume the carrier phase is coherent between units, and SHALL track slow carrier phase drift for the whole capture.
5. THE Receiver SHALL track a small sample-clock offset between transmitter and receiver, so that symbol timing does not slip over a long capture.
6. WHERE either unit runs on its internal oscillator, THE Receiver SHALL acquire a frequency offset up to a documented limit, derived from the B200-mini oscillator accuracy specification at the configured center frequency.
7. THE Receiver SHALL report the measured residual frequency offset, so that the user can see the benefit of the external references.
8. THE Receiver SHALL start capturing without being time-aligned to the transmitter, and SHALL find frame boundaries from the preamble alone.
9. THE Transmitter SHALL default to a low TX gain, and SHALL require an explicit option to raise it.
10. WHEN the user requests a TX gain above a documented conservative threshold, THE Transmitter SHALL print a warning that the receiver input needs sufficient attenuation.
11. THE Receiver SHALL report the RX gain in use and a received signal level estimate, so that the user can check the attenuation without a spectrum analyzer.
12. IF the received signal appears clipped or saturated, THEN THE Receiver SHALL warn that the input level is too high.
13. THE README SHALL give a back-to-back test procedure that covers the cabling, the 10 MHz reference connections, the recommended total attenuation, a gain starting point, the order in which to start rx and tx, and how to read the results.
14. THE README SHALL state the maximum RX input power of the B200-mini, the maximum TX output power, and the required level and connector of the 10 MHz reference input, citing the Ettus datasheet, and SHALL tell the user to confirm these against the datasheet before connecting.
15. THE System SHALL report measured results (BER, EVM, frequency offset) at several attenuation or gain settings in a form the user can paste into a test log.

### Requirement 8: Half-duplex echo link test

**User Story:** As an RF developer, I want one unit to send a frame and the other to echo it back, so that I can test both directions of the link and measure the round-trip delay with only two radios.

#### Acceptance Criteria

1. THE System SHALL provide an echo test with two roles: an initiator, which sends test frames and measures the echoes, and a responder, which receives each frame and retransmits it.
2. THE echo test SHALL be half duplex: at any time a unit SHALL either transmit or receive, never both, and a unit SHALL NOT try to demodulate its own transmission.
3. THE initiator SHALL send a frame carrying a sequence number and a payload from the seeded pseudorandom generator, then switch to receive and wait for the echo.
4. THE responder SHALL decode each received frame and retransmit it as an echo frame regenerated from the decoded bits, so that the return leg starts from clean symbols and does not carry over noise from the forward leg.
5. THE responder SHALL start each echo at a fixed, configurable turnaround time after the receive timestamp of the frame start, using a timed transmit, and the default turnaround SHALL be documented.
6. IF the responder cannot meet the turnaround, because decoding finished too late or the radio reports a late command, THEN THE responder SHALL skip that echo, count it, and report it, instead of transmitting at the wrong time.
7. THE echo frame SHALL carry the responder's measurements of the forward leg: the sequence number, EVM or SNR, residual frequency offset, and bit error count against the expected payload.
8. THE initiator SHALL wait for each echo with a configurable timeout longer than the responder's turnaround plus the round-trip propagation delay at the maximum supported distance, and SHALL count a timeout as a lost echo.
9. THE initiator SHALL wait a configurable interval between exchanges, long enough for the previous echo to have ended.
10. FOR each exchange, THE initiator SHALL report the sequence number, the forward-leg statistics from the echo, the return-leg statistics measured locally, the end-to-end bit error rate of the echoed payload against the original, the round-trip time, and whether the echo was received.
11. THE initiator SHALL measure the round-trip time using its own device timestamps of its transmit start and the echo's arrival, SHALL subtract the responder's turnaround to estimate the one-way propagation delay and the matching distance, and SHALL document the accuracy limits of that estimate, including the sample period and any uncalibrated hardware delay.
12. WHEN a run ends, whether by exchange count or Ctrl-C, THE initiator SHALL print a summary of exchanges sent, echoes received, echoes lost, and the minimum, mean, and maximum round-trip time and bit error rate on each leg.
13. THE responder SHALL print a running count of frames received, echoes sent, echoes skipped, and forward-leg bit error rate.
14. THE echo test SHALL NOT need PPS or a shared timebase between the units. Each unit SHALL time its own transmissions from its own device clock.
15. THE echo test SHALL work with both units using a single antenna port for transmit and receive, so that one cable and attenuator carries both directions in the conducted test, and the antenna port SHALL stay configurable.
16. BOTH roles transmit, so the low default TX gain and the high-gain warning of Requirement 7 SHALL apply to both.
17. THE README SHALL give a procedure for the echo test in both the conducted and the over-the-air setups, covering which unit to start first, the roles, the turnaround, and how to read the summary.

### Requirement 9: Over-the-air operation with propagation delay

**User Story:** As an RF developer, I want the link to work between two separated units, so that I can use it in the field where the signal arrives late and at an unknown level.

#### Acceptance Criteria

1. THE Receiver SHALL NOT assume any fixed relationship between the transmit time and the arrival time, and SHALL find frames for any propagation delay within a documented maximum range.
2. THE documentation SHALL state the propagation delay as a function of distance (about 3.34 microseconds per kilometre) and the maximum distance the default frame structure supports.
3. THE Receiver SHALL cope with a received signal level that is unknown at start and varies during a capture, by normalizing amplitude before the synchronization loops or by using the radio's gain control, and SHALL document which one it uses.
4. WHERE either unit moves relative to the other, THE Receiver SHALL track the resulting Doppler shift as part of the residual frequency offset, within a documented limit.
5. WHEN the signal drops below the level needed for reliable frame detection, THE Receiver SHALL report loss of signal, and WHEN it returns, THE Receiver SHALL reacquire without a restart.
6. THE Receiver SHALL reject false frame detections on noise, and SHALL report a configurable detection threshold.
7. THE echo test of Requirement 8 SHALL account for the round-trip propagation delay in its timeouts and intervals, instead of assuming a zero-delay link.
8. THE README SHALL tell the user that over-the-air transmission is subject to the regulations of their location (permitted frequencies, power, and licensing), that the user is responsible for compliance, and that the default center frequency and gain are not a statement of what is allowed there.
9. THE README SHALL describe a staged procedure: offline loopback first, then the conducted back-to-back test, then over-the-air at low power and short range.

### Requirement 10: Project structure and build

**User Story:** As a developer, I want a clean, buildable project, so that I can extend it and reuse its DSP blocks.

#### Acceptance Criteria

1. THE System SHALL build with CMake 3.16 or newer and a C++17 compiler, with no warnings under `-Wall -Wextra`.
2. THE DSP blocks SHALL live in a library that does not depend on UHD.
3. THE radio layer SHALL depend on UHD and be separate from the DSP library.
4. THE build SHALL offer an option to exclude UHD, building only the DSP library and loopback mode.
5. WHEN UHD is enabled, THE build SHALL find the Homebrew UHD 4.x install and SHALL NOT link the older copies in `/usr/local/lib`.
6. THE project SHALL include unit tests for the DSP blocks, runnable through CTest.
7. THE public DSP APIs SHALL carry Doxygen-style comments, and the code SHALL explain the key DSP math (RRC closed form, loop-filter gain derivation, Gray coding) in comments.

### Requirement 11: Documentation

**User Story:** As a new user, I want clear documentation, so that I can build and run the project without reading the source.

#### Acceptance Criteria

1. THE README SHALL describe the project, the 8-PSK interpretation, and the B200-mini versus B210 relationship.
2. THE README SHALL list dependencies and macOS/Homebrew install steps, build instructions, and every command-line option with its default.
3. THE README SHALL give a worked example for each mode: tx, rx, loopback, and file I/O.
4. THE README SHALL include the loopback results measured during verification.
5. THE README SHALL include a short block-by-block description of the DSP chain.
6. THE README SHALL include troubleshooting notes covering underflows and overflows, B200-mini gain and sample-rate limits, USB 3 throughput, and FPGA image download with `uhd_images_downloader`.

### Out of Scope

- Running the hardware tests here, conducted or over the air. No USRP is attached to this machine, so hardware behaviour stays untested until the user runs it. The offline loopback covers the DSP only.
- Multipath equalization. Simulated multipath is not required, and a receiver equalizer is not part of this spec.
- Regulatory compliance of the user's transmissions. The README warns about it, but the user is responsible.
- Generating, distributing, or monitoring the 10 MHz references. The user supplies them, and the software only sees whether the radio locks to the reference, not whether the GPSDO itself has locked to GPS.
- PPS input and a shared timebase between the two units. A 10 MHz reference aligns frequency, not time. The echo test only uses each unit's own device clock for timed transmission.
- Full-duplex operation, and more than two units.
- GPSDO support beyond what `clock_source` exposes.
- Forward error correction, adaptive equalization, and multi-antenna operation.
- GNU Radio or other frameworks. This project uses UHD directly.
