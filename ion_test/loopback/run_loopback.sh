#!/bin/bash
# ION-DTN loopback test over QPSK
#
# Prerequisites: qpsk_b200_app and ltp_cla_app must be running
# (ports 5000, 5001, 4556, 4557)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ION_BIN="$(realpath "$SCRIPT_DIR/../../ION-DTN/bin")"
ION_LIB="$(realpath "$SCRIPT_DIR/../../ION-DTN/lib")"

export LD_LIBRARY_PATH="$ION_LIB:$LD_LIBRARY_PATH"
export PATH="$ION_BIN:$PATH"
export ION_NODE_LIST_DIR="$SCRIPT_DIR"

cd "$SCRIPT_DIR"

echo "=== ION-DTN Loopback over QPSK ==="
echo ""

# Check prerequisites
if ! ss -tlnp | grep -q ":5000 "; then
    echo "ERROR: QPSK radio not running on port 5000"
    exit 1
fi
if ! ss -tlnp | grep -q ":4556 "; then
    echo "ERROR: LTP CLA not running on port 4556"
    exit 1
fi
echo "Prerequisites OK (QPSK radio + LTP CLA running)"
echo ""

# Clean any previous ION state
echo "Cleaning previous ION state..."
killall -q lt-ionadmin lt-bpadmin ionadmin bpadmin bpclock ipnfw stcpcli stcpclo 2>/dev/null
rm -f ion.log ion_nodes *.ionsdrlog 2>/dev/null
sleep 1

# Initialize ION
echo "Initializing ION node 1..."
ionadmin host1.ionrc
sleep 1

echo "Configuring BP..."
bpadmin host1.bprc
sleep 1

echo "Configuring IPN routing..."
ionadmin host1.ipnrc 2>/dev/null || true
sleep 1

echo ""
echo "ION initialized. Sending test bundle..."
echo ""

# Start bpsink to receive bundles on endpoint ipn:1.2
bpsink ipn:1.2 > /tmp/bpsink_output.txt 2>&1 &
SINK_PID=$!
sleep 2

# Send a bundle from ipn:1.1 to ipn:1.2
echo "Hello from ION over QPSK" | bpsource ipn:1.2
echo "Bundle sent from ipn:1.1 to ipn:1.2"
echo "Waiting for delivery..."

# Wait for the bundle to traverse: ION → STCP → LTP CLA → QPSK → LTP CLA → STCP → ION
sleep 15

# Check results
echo ""
echo "--- bpsink output ---"
cat /tmp/bpsink_output.txt
echo ""

kill $SINK_PID 2>/dev/null

# Also check if data made it through the QPSK link (LTP CLA egress)
echo "--- QPSK link status ---"
grep -c "RX: decoded" /tmp/qpsk_log.txt 2>/dev/null && echo "frames decoded by QPSK RX"
grep -c "Delivering" /tmp/ltp_cla_log.txt 2>/dev/null && echo "blocks delivered by LTP CLA"

echo ""
echo "=== Test complete ==="

# Cleanup ION
ionstop 2>/dev/null || killall -q ionadmin bpadmin bpclock ipnfw stcpcli stcpclo
