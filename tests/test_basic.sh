#!/bin/bash
# Basic functionality tests for shared memory message system
# Tests: init, create, join, send, recv

set -e  # Exit on first error

PROG="./shm"
TEST_DLG_ID=999

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

cleanup() {
    echo -n "Cleaning up IPC resources... "
    ipcrm -m 0x4D534700 2>/dev/null || true
    ipcrm -s 0x4D534701 0x4D534702 0x4D534703 2>/dev/null || true
    echo "done"
}

pass() {
    echo -e "${GREEN}✓ PASS${NC}: $1"
}

fail() {
    echo -e "${RED}✗ FAIL${NC}: $1"
    cleanup
    exit 1
}

# Clean before starting
cleanup

echo "========================================="
echo "   Basic Functionality Tests"
echo "========================================="

# Test 1: Initialization
echo -n "Test 1: Initialize shared memory... "
if $PROG init > /dev/null 2>&1; then
    pass "init"
else
    fail "init failed"
fi

# Test 2: Create dialogue
echo -n "Test 2: Create dialogue... "
if $PROG create $TEST_DLG_ID > /dev/null 2>&1; then
    pass "create dialogue $TEST_DLG_ID"
else
    fail "create dialogue"
fi

# Test 3: Join existing dialogue
echo -n "Test 3: Join dialogue... "
if $PROG join $TEST_DLG_ID > /dev/null 2>&1; then
    pass "join dialogue $TEST_DLG_ID"
else
    fail "join dialogue"
fi

# Test 4: Send message
echo -n "Test 4: Send message... "
if $PROG send $TEST_DLG_ID "Hello, World!" > /dev/null 2>&1; then
    pass "send message"
else
    fail "send message"
fi

# Test 5: List dialogues
echo -n "Test 5: List dialogues... "
if $PROG list > /dev/null 2>&1; then
    pass "list command"
else
    fail "list command"
fi

# Test 6: System status
echo -n "Test 6: Check system status... "
if $PROG status > /dev/null 2>&1; then
    pass "status command"
else
    fail "status command"
fi

# Test 7: Send TERMINATE and verify cleanup
echo -n "Test 7: Send TERMINATE message... "
if $PROG send $TEST_DLG_ID "TERMINATE" > /dev/null 2>&1; then
    pass "TERMINATE message"
else
    fail "TERMINATE message"
fi

echo ""
echo "========================================="
echo -e "${GREEN}All tests passed!${NC}"
echo "========================================="

cleanup
