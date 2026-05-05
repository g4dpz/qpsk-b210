#!/bin/bash
# BPv7 over LTP over QPSK end-to-end test (single ION node)
#
# Architecture:
#   ION Node 1 (bpsource ipn:1.1 → ipn:2.1)
#       → STCP outduct → ltp_cla_app ingress:4556
#       → LTP engine → CLA TX → qpsk_b200_app TCP:5000
#       → QPSK RF (915 MHz, B200mini SMA loopback)
#       → qpsk_b200_app TCP:5001 → CLA RX → LTP engine
#       → ltp_cla_app egress:4557 → netcat listener (verifies delivery)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

ION_BIN=$(realpath ../ION-DTN/bin)
ION_LIB=$(realpath ../ION-DTN/lib)
QPSK_APP=$(realpath ./build/src/qpsk_b200_app)
LTP_CLA_APP=$(realpath ./build/src/ltp_cla/ltp_cla_app)

export LD_LIBRARY_PATH="$ION_LIB:$LD_LIBRARY_PATH"
export PATH="$ION_BIN:$PATH"

echo "=== BPv7 over LTP over QPSK Test ==="
echo ""

# Step 1: Ensure QPSK radio is running
echo "--- Step 1: QPSK B200 radio ---"
if ss -tlnp | grep -q ":5000 "; then
    echo "QPSK radio already running on port 5000"
else
    echo "Start the QPSK radio first:"
    echo "  $QPSK_APP --mode txrx --serial 3218030 \\"
    echo "    --tcp-input-port 5000 --tcp-output-port 5001 \\"
    echo "    --center-freq 915e6 --sample-rate 1e6 \\"
    echo "    --tx-gain 10 --rx-gain 30 --fec-enabled 1"
    exit 1
fi

# Step 2: Start LTP CLA
echo "--- Step 2: LTP CLA ---"
$LTP_CLA_APP \
    --ltp.local_engine_id=1 \
    --ltp.remote_engine_id=2 \
    --cla.tx_port=5000 \
    --cla.rx_port=5001 \
    --ingress.port=4556 \
    --egress.port=4557 &
LTP_PID=$!
sleep 2
echo "LTP CLA started (PID $LTP_PID)"

# Step 3: Start receiver on egress port
echo "--- Step 3: Receiver on egress port 4557 ---"
nc 127.0.0.1 4557 > /tmp/received_bundle.bin &
NC_PID=$!
sleep 1
echo "Receiver listening (PID $NC_PID)"

# Step 4: Send a test bundle (raw bytes with 4-byte length prefix)
echo "--- Step 4: Sending test data ---"
python3 -c "
import socket, struct
msg = b'Hello DTN World over QPSK!'
# Frame with 4-byte big-endian length prefix (matching ltp_cla ingress format)
framed = struct.pack('>I', len(msg)) + msg
s = socket.socket()
s.connect(('127.0.0.1', 4556))
s.send(framed)
s.close()
print(f'Sent {len(msg)} bytes to LTP CLA ingress')
"

echo "Waiting for delivery through QPSK link..."
sleep 15

# Check result
kill $NC_PID 2>/dev/null || true
kill $LTP_PID 2>/dev/null || true

echo ""
echo "--- Result ---"
if [ -s /tmp/received_bundle.bin ]; then
    echo "SUCCESS! Received data:"
    xxd /tmp/received_bundle.bin | head -5
    echo ""
    # Try to extract the message (skip 4-byte length prefix framing from egress)
    python3 -c "
import struct
with open('/tmp/received_bundle.bin', 'rb') as f:
    data = f.read()
if len(data) >= 4:
    length = struct.unpack('>I', data[:4])[0]
    payload = data[4:4+length]
    print(f'Decoded payload ({len(payload)} bytes): {payload.decode(\"utf-8\", errors=\"replace\")}')
else:
    print(f'Raw data ({len(data)} bytes): {data}')
"
else
    echo "FAILED: No data received on egress port"
fi

echo ""
echo "=== Test complete ==="
