#!/bin/bash
# Start Node A (ground station)
# - QPSK modem: TX on L-band (1280 MHz), RX on S-band (2400 MHz)
# - ION-DTN with engine ID 1, LTP span to engine 2

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
QPSK_APP="$PROJECT_DIR/build/src/qpsk_b200_app"

# Frequencies (adjust as needed for your licensing/hardware)
TX_FREQ=${TX_FREQ:-1280e6}   # L-band TX
RX_FREQ=${RX_FREQ:-2400e6}   # S-band RX
TX_GAIN=${TX_GAIN:-20}       # Higher for over-the-air
RX_GAIN=${RX_GAIN:-40}

# Optional: specify device serial if multiple B200s present
DEVICE_SERIAL=${DEVICE_SERIAL:-}

cd "$SCRIPT_DIR"

echo "=== Starting Node A (Ground Station) ==="
echo ""
echo "QPSK Modem:"
echo "  TX: L-band ${TX_FREQ} Hz @ ${TX_GAIN} dB"
echo "  RX: S-band ${RX_FREQ} Hz @ ${RX_GAIN} dB"
echo ""

# Cleanup on exit
cleanup() {
    echo ""
    echo "Stopping Node A..."
    ionstop 2>/dev/null || killall -q ionadmin bpadmin bpclock ipnfw ltpclock ltpmeter 2>/dev/null || true
    kill $QPSK_PID 2>/dev/null || true
    wait 2>/dev/null
    echo "Node A stopped"
}
trap cleanup EXIT INT TERM

# --- Clean any previous ION state ---
killall -q ionadmin bpadmin bpclock ipnfw ltpclock ltpmeter udpclo udpcli bpsink 2>/dev/null || true
rm -f ion.log ion_nodes *.ionsdrlog 2>/dev/null || true
sleep 1

# --- Start QPSK modem ---
echo "Starting QPSK modem..."
SERIAL_ARG=""
if [ -n "$DEVICE_SERIAL" ]; then
    SERIAL_ARG="--serial $DEVICE_SERIAL"
fi

$QPSK_APP --mode txrx $SERIAL_ARG \
    --udp --udp-input-port 1113 --udp-output-port 1114 \
    --center-freq $TX_FREQ \
    --sample-rate 1e6 --tx-gain $TX_GAIN --rx-gain $RX_GAIN \
    --fec-enabled 1 \
    --acquisition-symbols 128 --ramp-symbols 8 \
    > /tmp/qpsk_nodeA.log 2>&1 &
QPSK_PID=$!

echo "QPSK modem PID: $QPSK_PID"
echo "Waiting for B200 initialization..."
sleep 15

if ! kill -0 $QPSK_PID 2>/dev/null; then
    echo "ERROR: QPSK modem failed to start"
    cat /tmp/qpsk_nodeA.log
    exit 1
fi
echo "QPSK modem ready"
echo ""

# --- Start ION-DTN ---
echo "Initializing ION-DTN (engine ID 1)..."
ionadmin nodeA.ionrc
sleep 1

echo "Configuring LTP..."
ltpadmin nodeA.ltprc
sleep 1

echo "Configuring BP..."
bpadmin nodeA.bprc
sleep 1

echo "Configuring IPN routing..."
ipnadmin nodeA.ipnrc 2>/dev/null || ionadmin nodeA.ipnrc 2>/dev/null || true
sleep 1

echo ""
echo "=== Node A running ==="
echo ""
echo "To send a bundle from this node:"
echo "  echo 'message' | bpsource ipn:2.1"
echo ""
echo "To receive bundles on this node:"
echo "  bpsink ipn:1.1"
echo ""
echo "Press Ctrl+C to stop."
echo ""

# Keep running
wait $QPSK_PID
