#!/bin/bash
# ION-DTN loopback test over QPSK (UDP mode)
#
# Architecture:
#   ION-DTN (BPv7 + LTP) → udpclo → UDP :1113
#       → qpsk_b200_app (modem, UDP mode)
#       → QPSK RF (915 MHz, B200mini SMA loopback)
#       → qpsk_b200_app → UDP :1114
#       → udpcli → ION-DTN (LTP reassembly → BP delivery)
#
# Prerequisites:
#   - B200mini connected with SMA cable between TX/RX and RX2
#   - ION-DTN binaries available at ../ION-DTN/bin

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

ION_BIN="$(realpath "$PROJECT_DIR/../ION-DTN/bin")"
ION_LIB="$(realpath "$PROJECT_DIR/../ION-DTN/lib")"
QPSK_APP="$PROJECT_DIR/build/src/qpsk_b200_app"

export LD_LIBRARY_PATH="$ION_LIB:$LD_LIBRARY_PATH"
export PATH="$ION_BIN:$PATH"
export ION_NODE_LIST_DIR="$SCRIPT_DIR"

cd "$SCRIPT_DIR"

echo "=== BPv7 over LTP over QPSK — UDP Loopback Test ==="
echo ""
echo "Modem: qpsk_b200_app in UDP mode"
echo "Protocol: ION-DTN handles BPv7 + LTP"
echo "Link: B200mini SMA loopback at 915 MHz"
echo ""

# Cleanup function
cleanup() {
    echo ""
    echo "Cleaning up..."
    kill $QPSK_PID 2>/dev/null || true
    ionstop 2>/dev/null || killall -q ionadmin bpadmin bpclock ipnfw ltpclock ltpmeter udpclo udpcli 2>/dev/null
    rm -f ion.log ion_nodes *.ionsdrlog 2>/dev/null
    wait 2>/dev/null
}
trap cleanup EXIT

# Clean any previous ION state
killall -q ionadmin bpadmin bpclock ipnfw ltpclock ltpmeter udpclo udpcli bpsink 2>/dev/null || true
rm -f ion.log ion_nodes *.ionsdrlog 2>/dev/null
sleep 1

# --- Step 1: Start QPSK modem in UDP mode ---
echo "--- Step 1: Starting QPSK modem (UDP mode) ---"
$QPSK_APP --mode txrx --serial 3218030 \
    --udp --udp-input-port 1113 --udp-output-port 1114 \
    --center-freq 915e6 --sample-rate 1e6 \
    --tx-gain 10 --rx-gain 30 --fec-enabled 1 \
    --acquisition-symbols 128 --ramp-symbols 8 \
    > /tmp/qpsk_udp_log.txt 2>&1 &
QPSK_PID=$!
echo "QPSK modem started (PID $QPSK_PID)"
echo "Waiting for B200 initialization..."
sleep 15

# Verify modem is running
if ! kill -0 $QPSK_PID 2>/dev/null; then
    echo "ERROR: QPSK modem failed to start"
    cat /tmp/qpsk_udp_log.txt
    exit 1
fi
echo "QPSK modem ready"
echo ""

# --- Step 2: Initialize ION-DTN ---
echo "--- Step 2: Initializing ION-DTN ---"

# Create ION config for single-node loopback with LTP over UDP
cat > host1.ionrc << 'EOF'
1 1 ''
s
EOF

cat > host1.ltprc << 'EOF'
1 32
a span 1 32 32 1400 1400 1 'udplso 127.0.0.1:1113'
s 'udplsi 127.0.0.1:1114'
EOF

cat > host1.bprc << 'EOF'
1
a scheme ipn 'ipnfw' 'ipnadminep'
a endpoint ipn:1.0 x
a endpoint ipn:1.1 x
a endpoint ipn:1.2 x
a protocol ltp 1400 100
a induct ltp 1 ltpcli
a outduct ltp 1 ltpclo
s
EOF

cat > host1.ipnrc << 'EOF'
a plan 1 ltp/1
EOF

echo "Running ionadmin..."
ionadmin host1.ionrc
sleep 1

echo "Running ltpadmin..."
ltpadmin host1.ltprc
sleep 1

echo "Running bpadmin..."
bpadmin host1.bprc
sleep 1

echo "Running ipnadmin..."
ionadmin host1.ipnrc 2>/dev/null || true
sleep 1

echo "ION-DTN initialized"
echo ""

# --- Step 3: Send and receive a bundle ---
echo "--- Step 3: Sending test bundle ---"
echo ""

# Start bpsink on endpoint ipn:1.2
bpsink ipn:1.2 > /tmp/bpsink_output.txt 2>&1 &
SINK_PID=$!
sleep 2

# Send a bundle from ipn:1.1 to ipn:1.2 (loopback via QPSK)
echo "Hello from ION-DTN over QPSK at 915 MHz!" | bpsource ipn:1.2
echo "Bundle sent: ipn:1.1 → ipn:1.2"
echo "Path: ION LTP → UDP → QPSK modem → RF loopback → QPSK modem → UDP → ION LTP"
echo ""
echo "Waiting for delivery (LTP handshake over RF)..."
sleep 20

# --- Step 4: Check results ---
echo ""
echo "--- Results ---"
echo ""

# Check bpsink output
if [ -s /tmp/bpsink_output.txt ]; then
    echo "SUCCESS! bpsink received:"
    cat /tmp/bpsink_output.txt
else
    echo "bpsink: no output (bundle may not have been delivered)"
fi

echo ""

# Check QPSK modem activity
echo "QPSK modem stats:"
TX_COUNT=$(grep -c "TX:" /tmp/qpsk_udp_log.txt 2>/dev/null || echo "0")
RX_COUNT=$(grep -c "RX: decoded" /tmp/qpsk_udp_log.txt 2>/dev/null || echo "0")
echo "  TX bursts: $TX_COUNT"
echo "  RX decoded: $RX_COUNT"

echo ""
echo "=== Test complete ==="

kill $SINK_PID 2>/dev/null || true
