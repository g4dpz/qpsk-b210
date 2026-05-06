# Two-Node FDD BPv7/LTP/QPSK Test

End-to-end test of BPv7 bundles over LTP over QPSK between two Raspberry Pi nodes using Frequency Division Duplex (FDD).

## Architecture

```
┌────────── Node A (Ground) ─────────┐     ┌────────── Node B (Remote) ─────────┐
│  bpsource ipn:1.1 → ipn:2.1        │     │  bpsink ipn:2.1                    │
│         ↓                           │     │         ↑                          │
│  ION-DTN (BPv7 + LTP)              │     │  ION-DTN (BPv7 + LTP)             │
│    engine 1 ↔ engine 2             │     │    engine 2 ↔ engine 1             │
│         ↓↑ UDP :1113/:1114          │     │         ↓↑ UDP :1113/:1114         │
│  qpsk_b200_app --udp                │     │  qpsk_b200_app --udp               │
│    TX: L-band 1280 MHz              │     │    TX: S-band 2400 MHz             │
│    RX: S-band 2400 MHz              │     │    RX: L-band 1280 MHz             │
│         ↓↑ RF                       │     │         ↓↑ RF                      │
│    B200mini + antennas              │     │    B200mini + antennas             │
└─────────────────────────────────────┘     └────────────────────────────────────┘
                │                                            │
                │  L-band 1280 MHz forward ───────────────→ │
                │ ←─────────────── S-band 2400 MHz return   │
```

## Prerequisites

On each Raspberry Pi:
- Pi 4 or 5 with USB 3.0
- Ubuntu/Debian aarch64
- UHD 4.6+ installed with udev rules for B200
- ION-DTN 4.x built and installed (binaries in `/usr/local/bin` or similar)
- This project built at `~/dev/qpsk-b210/build/src/qpsk_b200_app`
- B200mini connected via USB 3.0
- L-band antenna (1280 MHz) on TX/RX port
- S-band antenna (2400 MHz) on RX2 port

Network:
- Either direct ethernet between the Pis, or both on the same LAN
- No IP networking needed between the nodes for BPv7 traffic — the link is RF

## Configuration Files

### Node A (Ground — forward link TX on L-band)

- `nodeA.ionrc` — ION initialization (engine ID 1)
- `nodeA.ltprc` — LTP span to node 2 via UDP to local modem
- `nodeA.bprc` — BP with LTP induct/outduct
- `nodeA.ipnrc` — routing: ipn:2.* → LTP engine 2
- `start_nodeA.sh` — launches modem + ION

### Node B (Remote — forward link RX on L-band)

- `nodeB.ionrc` — ION initialization (engine ID 2)
- `nodeB.ltprc` — LTP span to node 1 via UDP to local modem
- `nodeB.bprc` — BP with LTP induct/outduct
- `nodeB.ipnrc` — routing: ipn:1.* → LTP engine 1
- `start_nodeB.sh` — launches modem + ION

## Running the Test

### On Node A (ground):

```bash
cd ~/dev/qpsk-b210/ion_test/two_node
./start_nodeA.sh
```

This starts:
1. QPSK modem: TX on 1280 MHz, RX on 2400 MHz
2. ION-DTN with engine ID 1
3. LTP span pointing at the local modem via UDP

### On Node B (remote):

```bash
cd ~/dev/qpsk-b210/ion_test/two_node
./start_nodeB.sh
```

This starts:
1. QPSK modem: TX on 2400 MHz, RX on 1280 MHz
2. ION-DTN with engine ID 2
3. LTP span pointing at the local modem via UDP

### Test bundle delivery

**On Node B**, start the receiver:
```bash
bpsink ipn:2.1
```

**On Node A**, send a bundle:
```bash
echo "Hello from ground station" | bpsource ipn:2.1
```

The bundle flows:
- Node A ION BP → LTP engine → UDP → QPSK modem → L-band RF
- → Node B QPSK modem → UDP → LTP engine → BP → bpsink

Return path (LTP reports, report-acks):
- Node B ION LTP → UDP → modem → S-band RF
- → Node A modem → UDP → ION LTP → session complete

## Stopping

On each node:
```bash
ionstop
# Ctrl+C the qpsk_b200_app, or:
killall qpsk_b200_app
```

## Troubleshooting

**No RF signal on RX:**
- Check antenna connections (L-band on TX/RX, S-band on RX2)
- Verify TX gain (try 20 dB for over-the-air)
- Check `uhd_find_devices` detects the B200

**LTP session timing out:**
- Increase retransmission timeout in `.ltprc`
- Check both directions are decoding: look for "RX: decoded" in modem logs

**Modem CPU too high:**
- Decrease sample rate to 500 kSPS
- Ensure Pi is running at full clock
