#!/bin/bash
# ============================================================
# FastDevFs Library Dedup Stress Test Suite
# ============================================================

set -e

MNT="/tmp/mnt"
DATA="/tmp/fastdevfs_data"
LOG="/tmp/lib_stress.log"
PASS=0
FAIL=0
TOTAL=0

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

run_test() {
    TOTAL=$((TOTAL + 1))
    echo -e "${YELLOW}━━━ TEST $TOTAL: $1 ━━━${NC}"
}

pass() { PASS=$((PASS + 1)); echo -e "${GREEN}  ✓ PASS: $1${NC}"; }
fail() { FAIL=$((FAIL + 1)); echo -e "${RED}  ✗ FAIL: $1${NC}"; }
check() { if eval "$1"; then pass "$2"; else fail "$2"; fi; }

get_data_files_with_content() {
    local content="$1"
    for f in "$DATA"/*; do
        [ -f "$f" ] || continue
        local c
        c=$(cat "$f" 2>/dev/null) || continue
        if [ "$c" = "$content" ]; then
            echo "$f"
        fi
    done
}

echo "=== FastDevFs Library Folders Dedup Stress Test ==="

# 1. Clean and Start FS
kill -9 $(pgrep fastdevfs) 2>/dev/null || true
sleep 1
fusermount -u "$MNT" 2>/dev/null || true
rm -rf "$DATA" /tmp/fastdevfs.mmap /tmp/fastdevfs_dedup.mmap /tmp/fastdevfs_dedup.sock /tmp/fastdevfs_lib_config.txt
mkdir -p "$MNT" "$DATA"

echo "Starting FS..."
/home/mohit/Desktop/FastDevFs/build/fastdevfs -f "$MNT" > "$LOG" 2>&1 &
sleep 2

# ============================================================
# TEST 1: Rapid creation of multiple identical libraries
# ============================================================
run_test "Rapid creation of 10 identical libraries"

for i in $(seq 1 10); do
    mkdir -p "$MNT/proj$i/node_modules/lodash"
    echo "function lodash(){ return 'v1'; }" > "$MNT/proj$i/node_modules/lodash/index.js"
    echo "{\"name\": \"lodash\", \"version\": \"1.0.0\"}" > "$MNT/proj$i/node_modules/lodash/package.json"
done

echo "Waiting for settlement (6s)..."
sleep 6

ALL_OK=true
for i in $(seq 1 10); do
    if [ ! -f "$MNT/proj$i/node_modules/lodash/index.js" ]; then
        ALL_OK=false
    fi
done
check '$ALL_OK' "All 10 lodash libraries are accessible"

TARGET_DATA=($(get_data_files_with_content "function lodash(){ return 'v1'; }"))
if [ "${#TARGET_DATA[@]}" -ge 1 ]; then
    NLINK=$(stat -c '%h' "${TARGET_DATA[0]}")
    check '[ "$NLINK" -ge 9 ]' "Files are deduplicated (nlink=$NLINK >= 9)"
else
    fail "Could not find content in data directory"
fi

# ============================================================
# TEST 2: Deeply nested libraries
# ============================================================
run_test "Deeply nested library structures"

mkdir -p "$MNT/nested/node_modules/pkg/node_modules/subpkg/node_modules/deep"
echo "deep content" > "$MNT/nested/node_modules/pkg/node_modules/subpkg/node_modules/deep/test.txt"

mkdir -p "$MNT/nested2/node_modules/pkg/node_modules/subpkg/node_modules/deep"
echo "deep content" > "$MNT/nested2/node_modules/pkg/node_modules/subpkg/node_modules/deep/test.txt"

sleep 6

check '[ "$(cat "$MNT/nested/node_modules/pkg/node_modules/subpkg/node_modules/deep/test.txt")" = "deep content" ]' "Nested file 1 readable"
check '[ "$(cat "$MNT/nested2/node_modules/pkg/node_modules/subpkg/node_modules/deep/test.txt")" = "deep content" ]' "Nested file 2 readable"

# ============================================================
# TEST 3: High file volume in one library
# ============================================================
run_test "High file volume library"

mkdir -p "$MNT/volumeA/node_modules/huge"
mkdir -p "$MNT/volumeB/node_modules/huge"

for i in $(seq 1 50); do
    echo "huge file content $i" > "$MNT/volumeA/node_modules/huge/file$i.txt"
    echo "huge file content $i" > "$MNT/volumeB/node_modules/huge/file$i.txt"
done

sleep 6

LINES_A=$(ls -1 "$MNT/volumeA/node_modules/huge" | wc -l)
LINES_B=$(ls -1 "$MNT/volumeB/node_modules/huge" | wc -l)

check '[ "$LINES_A" -eq 50 ]' "volumeA has all 50 files"
check '[ "$LINES_B" -eq 50 ]' "volumeB has all 50 files"

# ============================================================
# TEST 4: CoW on a library file
# ============================================================
run_test "CoW on deduplicated library file"

echo "MODIFIED" > "$MNT/proj1/node_modules/lodash/index.js"

sleep 4

check '[ "$(cat "$MNT/proj1/node_modules/lodash/index.js")" = "MODIFIED" ]' "Modified file has new content"
check '[ "$(cat "$MNT/proj2/node_modules/lodash/index.js")" = "function lodash(){ return '\''v1'\''; }" ]' "Other canonical users remain unchanged"

# ============================================================
# TEST 5: Concurrent reads/writes during deduplication
# ============================================================
run_test "Concurrent reads/writes during settlement"

mkdir -p "$MNT/raceA/node_modules/race"
mkdir -p "$MNT/raceB/node_modules/race"

echo "race content" > "$MNT/raceA/node_modules/race/file.txt"
echo "race content" > "$MNT/raceB/node_modules/race/file.txt"

# loop reads while settling
for i in $(seq 1 50); do
    cat "$MNT/raceA/node_modules/race/file.txt" > /dev/null
    cat "$MNT/raceB/node_modules/race/file.txt" > /dev/null
    sleep 0.1
done

check '[ "$(cat "$MNT/raceA/node_modules/race/file.txt")" = "race content" ]' "Race A is consistent"
check '[ "$(cat "$MNT/raceB/node_modules/race/file.txt")" = "race content" ]' "Race B is consistent"

# ============================================================
# Summary
# ============================================================
echo ""
echo -e "${YELLOW}════════════════════════════════════════${NC}"
echo -e "${YELLOW}  LIBRARY DEDUP STRESS TEST RESULTS${NC}"
echo -e "${YELLOW}════════════════════════════════════════${NC}"
echo -e "  Tests run:    $TOTAL"
echo -e "  ${GREEN}Passed:       $PASS${NC}"
if [ $FAIL -gt 0 ]; then
    echo -e "  ${RED}Failed:       $FAIL${NC}"
else
    echo -e "  Failed:       0"
fi
echo -e "${YELLOW}════════════════════════════════════════${NC}"

# Cleanup
kill -9 $(pgrep fastdevfs) 2>/dev/null || true
sleep 1
fusermount -u "$MNT" 2>/dev/null || true

exit $FAIL
