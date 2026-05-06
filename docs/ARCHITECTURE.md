# System Architecture

## Overview

This system implements a Delay-Tolerant Networking (DTN) radio link using QPSK modulation over Ettus B200-series Software Defined Radios. It enables Bundle Protocol version 7 (BPv7) communication over RF using the Licklider Transmission Protocol (LTP) as the reliable transport layer.

Full-duplex operation is achieved using Frequency Division Duplex (FDD) with L-band for the forward link and S-band for the return link.

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
│   L-band TX / S-band RX (FDD)      │
└─────────────────────────────────────┘
```

## Full-Duplex FDD Operation

The system uses Frequency Division Duplex (FDD) to achieve simultaneous transmit and receive on each node. The B200-series SDRs have independent TX and RX synthesizers, enabling operation on different frequencies without switching.

**Frequency Plan:**

| Link | Node A (Ground) | Node B (Remote) |
|------|-----------------|-----------------|
| Forward (data) | TX: L-band (1280 MHz) | RX: L-band (1280 MHz) |
| Return (reports/data) | RX: S-band (2400 MHz) | TX: S-band (2400 MHz) |

```
         Node A                              Node B
    ┌─────────────┐                     ┌─────────────┐
    │  TX: 1280 MHz │ ═══ L-band ═══>   │  RX: 1280 MHz │
    │  (L-band)    │    Forward Link    │  (L-band)    │
    │             │                     │             │
    │  RX: 2400 MHz │ <═══ S-band ═══   │  TX: 2400 MHz │
    │  (S-band)    │    Return Link     │  (S-band)    │
    └─────────────┘                     └─────────────┘
```

This enables reliable (red) LTP operation where:
- Node A sends data segments on L-band
- Node B receives data, sends report segments back on S-band
- Node A receives reports on S-band, sends report-acks on L-band
- Both directions operate simultaneously with no scheduling required

**Configuration:**

```bash
# Node A (ground station): TX L-band, RX S-band
./build/src/qpsk_b200_app --mode txrx --serial <nodeA> \
    --tx-freq 1280e6 --rx-freq 2400e6 \
    --sample-rate 1e6 --tx-gain 10 --rx-gain 30 --fec-enabled 1

# Node B (remote/spacecraft): TX S-band, RX L-band
./build/src/qpsk_b200_app --mode txrx --serial <nodeB> \
    --tx-freq 2400e6 --rx-freq 1280e6 \
    --sample-rate 1e6 --tx-gain 10 --rx-gain 30 --fec-enabled 1
```

## Components

### qpsk_b200_app — QPSK Radio

The physical layer modem. Accepts raw byte payloads via TCP, modulates them to QPSK, and transmits over RF. On the receive side, demodulates incoming RF and delivers decoded payloads via TCP. TX and RX operate on independent frequencies for full-duplex FDD.

**TX Pipeline:**
```
TCP input (:5000) → Frame builder (preamble + header + CRC)
    → FEC encoder (rate 1/2 or 3/4 convolutional)
    → QPSK symbol mapper (Gray-coded)
    → RRC pulse shaper (α=0.35, 4 sps)
    → B200 TX streamer → RF antenna (L-band or S-band)
```

**RX Pipeline:**
```
RF antenna (S-band or L-band) → B200 RX streamer
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
| Forward link | L-band, 1280 MHz |
| Return link | S-band, 2400 MHz |
| Duplex method | FDD (simultaneous TX/RX) |
| Sample rate | 1 MSPS |
| Symbol rate | 250 ksps (4 sps) |
| Samples per symbol | 4 |
| Pulse shaping | Root Raised Cosine, α=0.35 |
| FEC | Rate 1/2 convolutional (default) |
| Frame preamble | 26-bit doubled Barker-13 |
| CRC | CRC-32 |
| Data rate (raw) | 500 kbps (QPSK, 2 bits/symbol) |
| Data rate (with FEC 1/2) | ~250 kbps effective |

**CLI Options:**
```
--mode tx|rx|txrx    Operating mode
--serial <serial>    Target specific B200 device
--center-freq <Hz>   Center frequency (both TX and RX, for simplex)
--tx-freq <Hz>       TX frequency (overrides center-freq for TX)
--rx-freq <Hz>       RX frequency (overrides center-freq for RX)
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
- Red data: reliable transfer with checkpoint/report handshake (requires FDD return link)
- Green data: unreliable fire-and-forget transfer (one-way, no return link needed)
- Segmentation: configurable max segment size (default 1400 bytes)
- Retransmission: configurable timeout and max retries
- Session management: up to 100 concurrent sessions

**Red LTP Handshake over FDD:**
```
Node A (engine 1)                    Node B (engine 2)
      │                                    │
      │── Data segment ──── L-band ──────>│
      │                                    │
      │<── Report segment ── S-band ──────│
      │                                    │
      │── Report-ack ─────── L-band ─────>│
      │                                    │
   Session                              Session
   complete                             complete
```

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

## Two-Node FDD Deployment

```
┌──────────── Node A (Ground Station) ─────────┐     ┌──────────── Node B (Remote) ──────────────┐
│                                                │     │                                           │
│  ION bpsource (ipn:1.1)                       │     │  ION bpsink (ipn:2.1)                     │
│       ↓ STCP                                   │     │       ↑ STCP                              │
│  ltp_cla_app (engine_id=1)                     │     │  ltp_cla_app (engine_id=2)                │
│    ingress:4556 / egress:4557                  │     │    ingress:4559 / egress:4557             │
│       ↓↑ TCP                                   │     │       ↓↑ TCP                             │
│  qpsk_b200_app --mode txrx                     │     │  qpsk_b200_app --mode txrx               │
│    --tx-freq 1280e6 --rx-freq 2400e6           │     │    --tx-freq 2400e6 --rx-freq 1280e6     │
│       ↓                    ↑                   │     │       ↓                    ↑              │
│  TX antenna          RX antenna                │     │  TX antenna          RX antenna           │
│  (L-band)            (S-band)                  │     │  (S-band)            (L-band)             │
│       │                    │                   │     │       │                    │              │
└───────┼────────────────────┼───────────────────┘     └───────┼────────────────────┼──────────────┘
        │                    │                                  │                    │
        │    1280 MHz        │         2400 MHz                 │                    │
        └────────────────────┼──────────────────────────────────┼────────────────────┘
                             │                                  │
                             └──────────────────────────────────┘
                                    RF propagation
```

## Single-Device Loopback (Test Mode)

For development and testing, a single B200mini with an SMA cable connecting TX/RX to RX2 provides a complete RF loopback at a single frequency:

```
┌─────────────────────────────────────────────────┐
│                                                  │
│  Test client (Python/netcat)                     │
│       ↓ TCP :4556              TCP :4557 ↑       │
│  ltp_cla_app (green mode)                        │
│       ↓ TCP :5000              TCP :5001 ↑       │
│  qpsk_b200_app --mode txrx                       │
│    --center-freq 915e6                           │
│       ↓ TX/RX port ──SMA cable──→ RX2 port ↑    │
│                    B200mini                       │
│                                                  │
└──────────────────────────────────────────────────┘
```

**Tested configuration:**
- TX gain: 10 dB, RX gain: 30 dB
- Center freq: 915 MHz (single frequency, simplex loopback)
- FEC rate 1/2 enabled
- Green (unreliable) LTP verified end-to-end

## Data Flow Example

Sending "Hello DTN World" from Node A to Node B with reliable (red) LTP:

1. **Application** → `bpsource ipn:2.1` creates a BPv7 bundle
2. **ION BP** → routes bundle to STCP outduct connected to LTP CLA ingress
3. **LTP CLA ingress** → receives bundle bytes via TCP with length-prefix framing
4. **LTP engine** → creates session, segments data, encodes LTP data segment with checkpoint
5. **CLA TX** → frames LTP segment, sends to QPSK radio TCP input
6. **QPSK TX** → builds frame (preamble + header + FEC payload + CRC), modulates, transmits on **L-band (1280 MHz)**
7. **RF propagation** → L-band signal travels to Node B
8. **QPSK RX (Node B)** → receives on L-band, matched filter, carrier/timing recovery, frame sync, FEC decode
9. **LTP engine (Node B)** → reassembles data, generates report segment
10. **QPSK TX (Node B)** → transmits report on **S-band (2400 MHz)**
11. **RF propagation** → S-band signal travels back to Node A
12. **QPSK RX (Node A)** → receives report on S-band, decodes
13. **LTP engine (Node A)** → processes report, sends report-ack on L-band, marks session complete
14. **LTP engine (Node B)** → receives report-ack, delivers data to egress
15. **LTP CLA egress (Node B)** → delivers reassembled bundle to ION via TCP
16. **ION BP (Node B)** → receives bundle, delivers to `bpsink`
17. **Application** → "Hello DTN World" received

## Build and Run

```bash
# Build
mkdir build && cd build
cmake .. -DQPSK_ENABLE_UHD=ON
cmake --build . -j$(nproc)

# Two-node FDD deployment
# Node A (ground):
./build/src/qpsk_b200_app --mode txrx --serial <nodeA> \
    --tx-freq 1280e6 --rx-freq 2400e6 \
    --sample-rate 1e6 --tx-gain 10 --rx-gain 30 --fec-enabled 1

./build/src/ltp_cla/ltp_cla_app \
    --ltp.local_engine_id=1 --ltp.remote_engine_id=2 \
    --cla.tx_port=5000 --cla.rx_port=5001 \
    --ingress.port=4556 --egress.port=4557

# Node B (remote):
./build/src/qpsk_b200_app --mode txrx --serial <nodeB> \
    --tx-freq 2400e6 --rx-freq 1280e6 \
    --sample-rate 1e6 --tx-gain 10 --rx-gain 30 --fec-enabled 1

./build/src/ltp_cla/ltp_cla_app \
    --ltp.local_engine_id=2 --ltp.remote_engine_id=1 \
    --cla.tx_port=5000 --cla.rx_port=5001 \
    --ingress.port=4559 --egress.port=4557

# Single-device loopback test (simplex, green LTP):
./build/src/qpsk_b200_app --mode txrx --serial 3218030 \
    --center-freq 915e6 --sample-rate 1e6 \
    --tx-gain 10 --rx-gain 30 --fec-enabled 1

./build/src/ltp_cla/ltp_cla_app \
    --ltp.local_engine_id=1 --ltp.remote_engine_id=2 \
    --cla.tx_port=5000 --cla.rx_port=5001 \
    --ingress.port=4556 --egress.port=4557 \
    --ingress.default_reliable=false
```

## Link Budget Considerations

| Parameter | L-band (1280 MHz) | S-band (2400 MHz) |
|-----------|-------------------|-------------------|
| Free-space path loss (1 km) | ~93 dB | ~99 dB |
| Free-space path loss (10 km) | ~113 dB | ~119 dB |
| B200 max TX power | ~10 dBm | ~10 dBm |
| B200 RX noise figure | ~5 dB | ~5 dB |
| Required Eb/N0 (QPSK, BER 10⁻⁵) | ~9.6 dB | ~9.6 dB |
| FEC coding gain (rate 1/2) | ~5 dB | ~5 dB |

For longer range, consider:
- External power amplifier on TX
- Low-noise amplifier (LNA) on RX
- Directional antennas (Yagi, patch, dish)
- Lower symbol rate for improved sensitivity

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
| Ettus B200mini | Primary radio | USB 3.0, 70 MHz–6 GHz, full-duplex |
| LibreSDR B210 | Secondary radio | Clone, may have FX3 USB issues |
| Raspberry Pi 4/5 | Remote node | ARM64, USB 3.0 for B200 |
| L-band antenna | Forward link TX/RX | 1280 MHz, SMA |
| S-band antenna | Return link TX/RX | 2400 MHz, SMA |
| SMA cable + attenuator | Loopback testing | Direct connection TX/RX → RX2 |
