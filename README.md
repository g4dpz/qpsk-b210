# QPSK B200 Codec

A QPSK (Quadrature Phase Shift Keying) encoder/decoder for the Ettus Research USRP B200 software-defined radio. The system acts as a TCP-to-RF-to-TCP bridge: external applications inject byte streams via TCP, which are framed, optionally FEC-encoded, QPSK-modulated, pulse-shaped, and transmitted over the air through the B200. On the receive side, the B200 captures RF samples, demodulates them, recovers frames, and delivers decoded payloads to TCP clients.

## Architecture

```
TCP Client ──► TCP Input Server ──► Encoder ──► B200 TX ──► RF
                                                              │
TCP Clients ◄── TCP Output Server ◄── Decoder ◄── B200 RX ◄──┘
```

**TX pipeline:** payload bytes → frame builder (preamble + header + CRC-32) → FEC encoder (convolutional code, optional) → QPSK symbol mapper (Gray coded) → RRC pulse shaper → B200 TX streamer

**RX pipeline:** B200 RX streamer → RRC matched filter → Costas loop carrier sync → Mueller-Müller timing recovery → preamble correlator → QPSK demapper → FEC decoder (Viterbi, optional) → CRC verify → payload bytes

## Features

- **QPSK modulation** with Gray-coded constellation mapping
- **Root-raised-cosine pulse shaping** with configurable roll-off and samples per symbol
- **Forward error correction** using a rate-1/2 convolutional code (K=7, generators 171/133 octal) with Viterbi decoding. Rate 3/4 available via puncturing.
- **Frame structure** with 26-bit Barker preamble, 35-bit header (payload length, FEC flags, sequence number), CRC-32 error detection
- **Carrier synchronization** via second-order Costas loop
- **Symbol timing recovery** via Mueller-Müller timing error detector
- **TCP data interface** — single client feeds the transmitter, multiple clients can tap the receiver output
- **Runtime configuration** via JSON files or command-line arguments
- **B200 hardware support** via Ettus UHD (optional — builds and tests without hardware)

## Building

Requires CMake 3.14+ and a C++17 compiler. Dependencies (nlohmann/json, spdlog, Google Test, RapidCheck) are fetched automatically via CMake FetchContent. UHD is optional.

```bash
# Without B200 hardware (software-only, tests pass without UHD)
cmake -B build -DQPSK_ENABLE_UHD=OFF
cmake --build build

# With B200 hardware
cmake -B build -DQPSK_ENABLE_UHD=ON
cmake --build build
```

## Running Tests

```bash
ctest --test-dir build --output-on-failure -j1
```

331 tests covering unit, property-based (RapidCheck), and integration tests. Runs in ~1.3 seconds.

## Usage

```bash
# Run with default settings (2.4 GHz, 1 MSPS, FEC enabled)
./build/src/qpsk_b200_app

# Run with custom settings
./build/src/qpsk_b200_app --center-freq 915e6 --sample-rate 2e6 --tx-gain 20

# Load settings from JSON
./build/src/qpsk_b200_app --config my_config.json

# See all options
./build/src/qpsk_b200_app --help
```

The application starts TCP servers on `127.0.0.1:5000` (TX input) and `127.0.0.1:5001` (RX output) by default. Connect with any TCP client to send or receive data:

```bash
# Send data to the transmitter
echo "Hello, QPSK!" | nc 127.0.0.1 5000

# Receive decoded data
nc 127.0.0.1 5001
```

## Default Configuration

| Parameter | Default | Range |
|-----------|---------|-------|
| Center frequency | 2.4 GHz | 70 MHz – 6 GHz |
| Sample rate | 1 MSPS | up to 56 MSPS |
| TX gain | 40 dB | 0 – 89.75 dB |
| RX gain | 40 dB | 0 – 76 dB |
| Samples per symbol | 4 | ≥ 2 |
| RRC roll-off | 0.35 | 0 – 1.0 |
| FEC | enabled, rate 1/2 | 1/2 or 3/4 |
| TCP input | 127.0.0.1:5000 | — |
| TCP output | 127.0.0.1:5001 | — |

## Project Structure

```
include/qpsk_b200/       Public headers
  types.h                Config, Frame, diagnostics structs
  symbol_mapper.h        Gray-coded QPSK mapping
  pulse_shaper.h         RRC filter
  fec.h                  FEC encoder (convolutional) and decoder (Viterbi)
  frame.h                Frame builder and parser
  frame_sync.h           Preamble correlator
  carrier_sync.h         Costas loop
  timing_recovery.h      Mueller-Müller TED
  b200_interface.h       UHD hardware wrapper
  tcp_input_server.h     Single-client TCP input
  tcp_output_server.h    Multi-client TCP output
  encoder.h              TX pipeline orchestration
  decoder.h              RX pipeline orchestration
  spsc_queue.h           Lock-free inter-thread queue

src/                     Implementation files + main.cpp
tests/unit/              Unit tests (Google Test)
tests/property/          Property-based tests (RapidCheck)
tests/integration/       Integration tests
.kiro/specs/             Spec documents (requirements, design, tasks)
```

## Frame Wire Format

```
[Preamble 26b][Header 35b][Payload][CRC-32 32b]

Header:
  Frame Length       16 bits   (payload byte count, pre-FEC)
  Padding Bits        4 bits
  FEC Enabled         1 bit
  FEC Code Rate       2 bits   (00 = 1/2, 01 = 3/4)
  Sequence Number    12 bits
```

## Future Work

A follow-on spec exists for layering DTN Bundle Protocol v7 (RFC 9171) over LTP (RFC 5326) on top of this codec. See `.kiro/specs/bp-ltp-dtn/` for the requirements and design documents.
