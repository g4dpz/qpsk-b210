# System Architecture

## Overview

This system implements a Delay-Tolerant Networking (DTN) radio link using QPSK modulation over Ettus B200-series Software Defined Radios. It enables Bundle Protocol version 7 (BPv7) communication over RF using the Licklider Transmission Protocol (LTP) as the reliable transport layer.

## Protocol Stack

```
┌─────────────────────────────────────┐
│         Application Layer           │
│   (bpsource / bpsink / bpcp)        │
├─────────────────────────────────────┤
│     Bundle Protocol v7 (BPv7)       │
│          ION-DTN daemon             │
├─────────────────────────────────────┤
│   Convergence Layer (STCP/TCP)      │
│   4-byte length-prefix framing      │
├─────────────────────────────────────┤
│  Licklider Transmission Protocol    │
│         ltp_cla_app                 │
├─────────────────────────────────────┤
│      QPSK Physical Layer            │
│        qpsk_b200_app                │
├─────────────────────────────────────┤
│     Ettus B200 / B210 SDR           │
│        915 MHz RF link              │
└─────────────────────────────────────┘
```

## Components

### qpsk_b200_app — QPSK Radio

The physical layer modem. Accepts raw byte payloads via TCP, modulates them to QPSK, and transmits over RF. On the receive side, demodulates incoming RF and delivers decoded payloads via TCP.

**TX Pipeline:**
```
TCP input (:5000) → Frame builder (preamble + header + CRC)
    → FEC encoder (rate 1/2 or 3/4 convolutional)
    → QPSK symbol mapper (Gray-coded)
    → RRC pulse shaper (α=0.35, 4 sps)
    → B200 TX streamer → RF antenna
```

**RX Pipeline:**
```
RF antenna → B200 RX streamer
    → RRC matched filter
    → Carrier synchronization (Costas loop)
    → Timing recovery (Gardner TED)
    → Frame synchronization (Barker preamble correlation)
    → QPSK demapper
    → FEC decoder
    → CRC verification
    → TCP output (:5001)
```

**Key Parameters:**
| Parameter | Value |
|-----------|-------|
| Modulation | QPSK (Gray-coded) |
| Center frequency | 915 MHz (configurable) |
| Sample rate | 1 MSPS |
| Samples per symbol | 4 |
| Pulse shaping | Root Raised Cosine, α=0.35 |
| FEC | Rate 1/2 convolutional (default) |
| Frame preamble | 26-bit doubled Barker-13 |
| CRC | CRC-32 |

**CLI Options:**
```
--mode tx|rx|txrx    Operating mode
--serial <serial>    Target specific B200 device
--center-freq <Hz>   Center frequency
--sample-rate <Hz>   Sample rate
--tx-gain <dB>       Transmit gain (0-89 dB)
--rx-gain <dB>       Receive gain (0-76 dB)
--fec-enabled 0|1    Enable/disable FEC
--fec-code-rate      "1/2" or "3/4"
--tcp-input-port     TCP port for TX data input
--tcp-output-port    TCP port for RX data output
```

### ltp_cla_app — LTP Convergence Layer Adapter

Implements the Licklider Transmission Protocol (RFC 5326) over the QPSK radio link. Provides reliable (red) and unreliable (green) data transfer with segmentation, retransmission, and session management.

**Architecture:**
```
TCP Ingress (:4556)          TCP Egress (:4557)
       ↓                            ↑
   LTP Engine                   LTP Engine
  (segmentation,              (reassembly,
   checkpoints,                report generation,
   retransmission)             data delivery)
       ↓                            ↑
   CLA Framing                  CLA Framing
  (4-byte length prefix)       (4-byte length prefix)
       ↓                            ↑
  TCP to qpsk_b200_app        TCP from qpsk_b200_app
      (:5000)                      (:5001)
```

**LTP Features:**
- Red data: reliable transfer with checkpoint/report handshake
- Green data: unreliable fire-and-forget transfer
- Segmentation: configurable max segment size (default 1400 bytes)
- Retransmission: configurable timeout and max retries
- Session management: up to 100 concurrent sessions

**Configuration:**
```
--ltp.local_engine_id=N       Local LTP engine identifier
--ltp.remote_engine_id=N      Remote LTP engine identifier
--ltp.max_segment_size=N      Max bytes per LTP segment
--ltp.retransmission_timeout_ms=N  Retransmission timeout
--ingress.port=N              TCP port for incoming data
--ingress.default_reliable=true|false  Red or green data
--egress.port=N               TCP port for delivered data
--cla.tx_port=N               QPSK radio TX input port
--cla.rx_port=N               QPSK radio RX output port
```

### ION-DTN — Bundle Protocol Agent

JPL's Interplanetary Overlay Network implementation provides the BPv7 bundle agent. It handles bundle creation, routing, custody transfer, and delivery.

**Integration:** ION connects to the LTP CLA via STCP (Simple TCP) convergence layer, which uses the same 4-byte big-endian length-prefix framing as the LTP CLA ingress/egress ports.

## Two-Node Deployment

```
┌─────────── Node A (Sender) ──────────┐     ┌─────────── Node B (Receiver) ─────────┐
│                                       │     │                                        │
│  ION bpsource (ipn:1.1)              │     │  ION bpsink (ipn:2.1)                  │
│       ↓ STCP                          │     │       ↑ STCP                           │
│  ltp_cla_app                          │     │  ltp_cla_app                           │
│    engine_id=1                        │     │    engine_id=2                          │
│    ingress:4556                       │     │    egress:4557                          │
│       ↓ TCP                           │     │       ↑ TCP                            │
│  qpsk_b200_app --mode tx              │     │  qpsk_b200_app --mode rx               │
│    --serial <device_A>                │     │    --serial <device_B>                  │
│       ↓                               │     │       ↑                                │
│  B200 TX antenna ═══ 915 MHz RF ═══════════> B200 RX antenna                         │
│                                       │     │                                        │
└───────────────────────────────────────┘     └────────────────────────────────────────┘
```

## Single-Device Loopback (Test Mode)

For development and testing, a single B200mini with an SMA cable connecting TX/RX to RX2 provides a complete RF loopback:

```
┌─────────────────────────────────────────────────┐
│                                                  │
│  Test client (Python/netcat)                     │
│       ↓ TCP :4556              TCP :4557 ↑       │
│  ltp_cla_app (green mode)                        │
│       ↓ TCP :5000              TCP :5001 ↑       │
│  qpsk_b200_app --mode txrx --serial 3218030     │
│       ↓ TX/RX port ──SMA cable──→ RX2 port ↑    │
│                    B200mini                       │
│                                                  │
└──────────────────────────────────────────────────┘
```

**Tested configuration:**
- TX gain: 10 dB, RX gain: 30 dB
- FEC rate 1/2 enabled
- Green (unreliable) LTP for single-instance loopback
- Red (reliable) LTP requires two separate CLA instances (two-node deployment)

## Data Flow Example

Sending "Hello DTN World" from Node A to Node B:

1. **Application** → `bpsource ipn:2.1` creates a BPv7 bundle
2. **ION BP** → routes bundle to STCP outduct connected to LTP CLA ingress
3. **LTP CLA ingress** → receives bundle bytes via TCP with length-prefix framing
4. **LTP engine** → creates session, segments data, encodes LTP segment with headers
5. **CLA TX** → frames LTP segment with length prefix, sends to QPSK radio TCP input
6. **QPSK TX** → builds frame (preamble + header + FEC-coded payload + CRC), QPSK modulates, pulse shapes, transmits at 915 MHz
7. **RF propagation** → signal travels through cable or over the air
8. **QPSK RX** → matched filter, carrier/timing recovery, frame sync, FEC decode, CRC check
9. **CLA RX** → extracts framed LTP segment from QPSK radio TCP output
10. **LTP engine** → reassembles data, sends report (red) or delivers immediately (green)
11. **LTP CLA egress** → delivers reassembled data to connected TCP client
12. **ION BP** → receives bundle via STCP induct, delivers to `bpsink`
13. **Application** → "Hello DTN World" received

## Build and Run

```bash
# Build
mkdir build && cd build
cmake .. -DQPSK_ENABLE_UHD=ON
cmake --build . -j$(nproc)

# Run (single-device loopback test)
./build/src/qpsk_b200_app --mode txrx --serial 3218030 \
    --center-freq 915e6 --sample-rate 1e6 \
    --tx-gain 10 --rx-gain 30 --fec-enabled 1

./build/src/ltp_cla/ltp_cla_app \
    --ltp.local_engine_id=1 --ltp.remote_engine_id=2 \
    --cla.tx_port=5000 --cla.rx_port=5001 \
    --ingress.port=4556 --egress.port=4557 \
    --ingress.default_reliable=false

# Send test data
python3 -c "
import socket, struct
msg = b'Hello DTN World!'
s = socket.socket()
s.connect(('127.0.0.1', 4556))
s.send(struct.pack('>I', len(msg)) + msg)
s.close()
"
```

## Dependencies

| Component | Version | Purpose |
|-----------|---------|---------|
| UHD | 4.6+ | Ettus USRP hardware driver |
| spdlog | 1.14 | Logging |
| nlohmann/json | 3.11 | Configuration serialization |
| GoogleTest | 1.16 | Unit and integration testing |
| RapidCheck | latest | Property-based testing |
| ION-DTN | 4.x | BPv7 bundle agent |

## Hardware

| Device | Role | Notes |
|--------|------|-------|
| Ettus B200mini | Primary radio | USB 3.0 required, single TX+RX |
| LibreSDR B210 | Secondary radio | Clone, may have FX3 USB issues |
| Raspberry Pi 4/5 | Remote node | ARM64, USB 3.0 for B200 |
| SMA cable + attenuator | Loopback testing | Direct connection TX/RX → RX2 |
| 915 MHz antennas | Over-the-air | For two-node deployment |
