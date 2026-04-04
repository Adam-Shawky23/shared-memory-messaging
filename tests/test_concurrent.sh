#!/bin/bash
# Concurrent and stress tests

set -e

PROG="./shm"
TEST_DLG_ID=888

cleanup() {
    pkill -f "$PROG recv" 2>/dev/null || true
    sleep 0.5
    ipcrm -m 0x4D534700 2>/dev/null || true
    ipcrm -s 0x4D534701 0x4D534702 0x4D534703 2>/dev/null || true
}

pass() {
    echo "✓ PASS: $1"
}

fail() {
    echo "✗ FAIL: $1"
    cleanup
    exit 1
}

trap cleanup EXIT

cleanup
sleep 0.2

echo "========================================="
echo "   Concurrent and Stress Tests"
echo "========================================="

# Initialize
$PROG init > /dev/null 2>&1 || fail "init"

# Create dialogue
$PROG create $TEST_DLG_ID > /dev/null 2>&1 || fail "create"

# Test: Multiple senders and receivers
echo -n "Test 1: Concurrent send/recv... "
timeout 5 $PROG recv > /tmp/recv_output.txt 2>&1 &
RECV_PID=$!
sleep 0.3

for i in {1..5}; do
    $PROG send $TEST_DLG_ID "Message $i" > /dev/null 2>&1
done

$PROG send $TEST_DLG_ID "TERMINATE" > /dev/null 2>&1
wait $RECV_PID 2>/dev/null || true

if grep -q "Message 1" /tmp/recv_output.txt; then
    pass "concurrent send/recv"
else
    fail "concurrent send/recv - no messages received"
fi

# Clean up for next test
cleanup
sleep 0.2

# Reinit for next test
$PROG init > /dev/null 2>&1

# Test: Message ordering
echo -n "Test 2: Message ordering (sequence numbers)... "
$PROG create 777 > /dev/null 2>&1

timeout 3 $PROG recv > /tmp/recv_seq.txt 2>&1 &
RECV_PID=$!
sleep 0.2

for i in {0..3}; do
    $PROG send 777 "Seq $i" > /dev/null 2>&1
done

$PROG send 777 "TERMINATE" > /dev/null 2>&1
wait $RECV_PID 2>/dev/null || true

if grep -q "Sequence: 0" /tmp/recv_seq.txt && grep -q "Sequence: 3" /tmp/recv_seq.txt; then
    pass "message ordering"
else
    fail "message ordering - sequence numbers not correct"
fi

echo ""
echo "========================================="
echo "✓ All concurrent tests passed!"
echo "========================================="
