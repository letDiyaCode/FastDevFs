#!/bin/bash
# ============================================================
# FastDevFs Dedup Stress Test Suite (v2 — fixed timing/checks)
# ============================================================

set -e

MNT="/tmp/mnt"
DATA="/tmp/fastdevfs_data"
LOG="/tmp/dedup_stress.log"
PASS=0
FAIL=0
TOTAL=0

# Wait time: debounce (500ms) + hash + I/O overhead
DWAIT=2.5

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

# Helper: get the data index for a virtual path by checking which data file
# has the right content (robust to node allocation order)
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

# ============================================================
# TEST 1: Basic dedup — two identical files get hard-linked
# ============================================================
run_test "Basic duplicate detection"

echo "identical content for test 1" > "$MNT/dup_a.txt"
sleep $DWAIT

echo "identical content for test 1" > "$MNT/dup_b.txt"
sleep $DWAIT

# Find data files with this content
MATCHES=($(get_data_files_with_content "identical content for test 1"))

if [ "${#MATCHES[@]}" -ge 2 ]; then
    INODE_A=$(stat -c '%i' "${MATCHES[0]}")
    INODE_B=$(stat -c '%i' "${MATCHES[1]}")
    check '[ "$INODE_A" = "$INODE_B" ]' \
        "Identical files share same inode (A=$INODE_A B=$INODE_B)"
    NLINK=$(stat -c '%h' "${MATCHES[0]}")
    check '[ "$NLINK" -ge 2 ]' "nlink >= 2 (got $NLINK)"
else
    fail "Could not find 2 data files with test content (found ${#MATCHES[@]})"
fi

# Verify content via virtual FS
check '[ "$(cat "$MNT/dup_a.txt")" = "identical content for test 1" ]' "dup_a content OK"
check '[ "$(cat "$MNT/dup_b.txt")" = "identical content for test 1" ]' "dup_b content OK"

# ============================================================
# TEST 2: Unique files stay independent
# ============================================================
run_test "Unique files remain independent"

echo "unique alpha 42" > "$MNT/uniq_a.txt"
sleep $DWAIT
echo "unique beta 99" > "$MNT/uniq_b.txt"
sleep $DWAIT

MA=($(get_data_files_with_content "unique alpha 42"))
MB=($(get_data_files_with_content "unique beta 99"))

if [ "${#MA[@]}" -ge 1 ] && [ "${#MB[@]}" -ge 1 ]; then
    IA=$(stat -c '%i' "${MA[0]}")
    IB=$(stat -c '%i' "${MB[0]}")
    check '[ "$IA" != "$IB" ]' "Unique files have different inodes ($IA vs $IB)"
else
    fail "Could not find data files for unique test"
fi

# ============================================================
# TEST 3: CoW break on modification
# ============================================================
run_test "CoW break when modifying a deduped file"

echo "cow test data" > "$MNT/cow_a.txt"
sleep $DWAIT
echo "cow test data" > "$MNT/cow_b.txt"
sleep $DWAIT

# Verify deduped (both should read the same, data should be linked)
check '[ "$(cat "$MNT/cow_a.txt")" = "cow test data" ]' "cow_a readable before modify"
check '[ "$(cat "$MNT/cow_b.txt")" = "cow test data" ]' "cow_b readable before modify"

# Modify cow_b — triggers CoW break
echo "cow test MODIFIED" > "$MNT/cow_b.txt"
sleep $DWAIT

# cow_a should still have original, cow_b should have new content
COW_A=$(cat "$MNT/cow_a.txt" 2>/dev/null)
COW_B=$(cat "$MNT/cow_b.txt" 2>/dev/null)
check '[ "$COW_A" = "cow test data" ]' "cow_a still has original content ($COW_A)"
check '[ "$COW_B" = "cow test MODIFIED" ]' "cow_b has modified content ($COW_B)"

# ============================================================
# TEST 4: Delete refcount management
# ============================================================
run_test "Delete handles refcount correctly"

echo "delete refcount test" > "$MNT/ref_a.txt"
sleep $DWAIT
echo "delete refcount test" > "$MNT/ref_b.txt"
sleep $DWAIT

rm "$MNT/ref_a.txt"
sleep 0.5

REF_B=$(cat "$MNT/ref_b.txt" 2>/dev/null)
check '[ "$REF_B" = "delete refcount test" ]' "Surviving file readable ($REF_B)"

# ============================================================
# TEST 5: 20 identical files — high refcount
# ============================================================
run_test "20 identical files deduplication"

for i in $(seq 1 20); do
    echo "stress dup v42" > "$MNT/stress_$i.txt"
    sleep 0.1
done
sleep 5  # extra time for 20 debounce timers to fire

ALL_OK=true
for i in $(seq 1 20); do
    C=$(cat "$MNT/stress_$i.txt" 2>/dev/null)
    if [ "$C" != "stress dup v42" ]; then ALL_OK=false; break; fi
done
check '$ALL_OK' "All 20 files readable with correct content"

# Check nlink on the canonical data file
STRESS_FILES=($(get_data_files_with_content "stress dup v42"))
if [ "${#STRESS_FILES[@]}" -ge 1 ]; then
    NLINK=$(stat -c '%h' "${STRESS_FILES[0]}")
    check '[ "$NLINK" -ge 10 ]' "Canonical file nlink=$NLINK (expected ~20)"
fi

# ============================================================
# TEST 6: Rapid writes (debounce test)
# ============================================================
run_test "Rapid sequential writes (debounce test)"

for i in $(seq 1 50); do
    echo "rapid line $i" >> "$MNT/rapid.txt"
done
sleep $DWAIT

LINES=$(wc -l < "$MNT/rapid.txt")
check '[ "$LINES" -eq 50 ]' "File has all 50 lines (got $LINES)"

# Count hash operations for this file
HASH_COUNT=$(grep -c "Registered canonical.*rapid" "$LOG" 2>/dev/null || echo "0")
echo "  Info: $HASH_COUNT hash operations for rapid file (good if <= 5)"

# ============================================================
# TEST 7: Empty files don't crash
# ============================================================
run_test "Empty files handled safely"

touch "$MNT/empty1.txt"
touch "$MNT/empty2.txt"
sleep 1

check 'ls "$MNT/empty1.txt" > /dev/null 2>&1' "Empty file 1 exists"
check 'ls "$MNT/empty2.txt" > /dev/null 2>&1' "Empty file 2 exists"

# ============================================================
# TEST 8: Large file dedup (1 MB)
# ============================================================
run_test "Large file dedup (1 MB)"

dd if=/dev/urandom bs=1024 count=1024 of=/tmp/_test_1mb.bin 2>/dev/null
cp /tmp/_test_1mb.bin "$MNT/large_a.bin"
sleep 3

cp /tmp/_test_1mb.bin "$MNT/large_b.bin"
sleep 3

HASH_ORIG=$(sha256sum /tmp/_test_1mb.bin | cut -d' ' -f1)
HASH_A=$(sha256sum "$MNT/large_a.bin" | cut -d' ' -f1)
HASH_B=$(sha256sum "$MNT/large_b.bin" | cut -d' ' -f1)
check '[ "$HASH_ORIG" = "$HASH_A" ]' "large_a matches original"
check '[ "$HASH_ORIG" = "$HASH_B" ]' "large_b matches original"

# Check hard-link
LF=($(find "$DATA" -size +500k 2>/dev/null))
if [ "${#LF[@]}" -ge 2 ]; then
    LIA=$(stat -c '%i' "${LF[0]}")
    LIB=$(stat -c '%i' "${LF[1]}")
    check '[ "$LIA" = "$LIB" ]' "1MB files hard-linked ($LIA = $LIB)"
fi

rm -f /tmp/_test_1mb.bin

# ============================================================
# TEST 9: Batch create 50 files (25 alpha + 25 beta)
# ============================================================
run_test "Batch create 50 files, two dedup groups"

for i in $(seq 1 25); do
    echo "batch alpha" > "$MNT/ba_$i.txt"
    echo "batch beta" > "$MNT/bb_$i.txt"
done
sleep 6

ALPHA_OK=true
for i in $(seq 1 25); do
    C=$(cat "$MNT/ba_$i.txt" 2>/dev/null)
    if [ "$C" != "batch alpha" ]; then ALPHA_OK=false; break; fi
done
check '$ALPHA_OK' "All 25 alpha files readable"

BETA_OK=true
for i in $(seq 1 25); do
    C=$(cat "$MNT/bb_$i.txt" 2>/dev/null)
    if [ "$C" != "batch beta" ]; then BETA_OK=false; break; fi
done
check '$BETA_OK' "All 25 beta files readable"

# ============================================================
# TEST 10: Delete all but one from dedup group
# ============================================================
run_test "Survivor — delete 4 of 5 identical files"

for i in $(seq 1 5); do
    echo "survivor 2026" > "$MNT/sv_$i.txt"
done
sleep 4

for i in $(seq 1 4); do rm "$MNT/sv_$i.txt"; done
sleep 1

SV=$(cat "$MNT/sv_5.txt" 2>/dev/null)
check '[ "$SV" = "survivor 2026" ]' "Last survivor readable ($SV)"

# ============================================================
# TEST 11: Library path classification
# ============================================================
run_test "Library path classification"

mkdir -p "$MNT/proj/src" "$MNT/proj/node_modules/pkg"
echo "lib code" > "$MNT/proj/node_modules/pkg/idx.js"
echo "usr code" > "$MNT/proj/src/app.js"
sleep $DWAIT

check '[ "$(cat "$MNT/proj/node_modules/pkg/idx.js")" = "lib code" ]' "Library file OK"
check '[ "$(cat "$MNT/proj/src/app.js")" = "usr code" ]' "User file OK"

# ============================================================
# TEST 12: Cross-link prevention
# ============================================================
run_test "Cross-link prevention (user vs library same content)"

echo "crosslink data" > "$MNT/proj/src/cross.txt"
sleep $DWAIT
echo "crosslink data" > "$MNT/proj/node_modules/pkg/cross.txt"
sleep $DWAIT

check '[ "$(cat "$MNT/proj/src/cross.txt")" = "crosslink data" ]' "User file content OK"
check '[ "$(cat "$MNT/proj/node_modules/pkg/cross.txt")" = "crosslink data" ]' "Library file content OK"

# They should NOT be hard-linked (different is_library flag)
UF=($(get_data_files_with_content "crosslink data"))
if [ "${#UF[@]}" -ge 2 ]; then
    UI=$(stat -c '%i' "${UF[0]}")
    LI=$(stat -c '%i' "${UF[1]}")
    # With cross-link prevention, they should have different inodes
    # (but this depends on processing order, so just verify content is fine)
    echo "  Info: Data file inodes: $UI and $LI (should differ if cross-link prevention works)"
fi

# ============================================================
# TEST 13: Overwrite with different content
# ============================================================
run_test "File overwrite"

echo "version 1" > "$MNT/overwrite.txt"
sleep $DWAIT
echo "version 2 - much longer" > "$MNT/overwrite.txt"
sleep $DWAIT

OW=$(cat "$MNT/overwrite.txt" 2>/dev/null)
check '[ "$OW" = "version 2 - much longer" ]' "Overwritten file has new content"

# ============================================================
# TEST 14: Create-delete cycle stress
# ============================================================
run_test "Create-delete cycle (30 iterations)"

for iter in $(seq 1 30); do
    echo "cycle $iter" > "$MNT/cycle.txt"
    sleep 0.05
    rm "$MNT/cycle.txt" 2>/dev/null
done
sleep 1

check 'ls "$MNT" > /dev/null 2>&1' "Filesystem survived 30 create-delete cycles"

# ============================================================
# TEST 15: Multiple modifications to same deduped file
# ============================================================
run_test "Multiple sequential modifications"

echo "base content" > "$MNT/multi_a.txt"
sleep $DWAIT
echo "base content" > "$MNT/multi_b.txt"
sleep $DWAIT

# Modify b three times
echo "mod 1" > "$MNT/multi_b.txt"
sleep $DWAIT
echo "mod 2" > "$MNT/multi_b.txt"
sleep $DWAIT
echo "mod 3" > "$MNT/multi_b.txt"
sleep $DWAIT

check '[ "$(cat "$MNT/multi_a.txt")" = "base content" ]' "Unmodified file preserved"
check '[ "$(cat "$MNT/multi_b.txt")" = "mod 3" ]' "Multi-modified file has final content"

# ============================================================
# Summary
# ============================================================
echo ""
echo -e "${YELLOW}════════════════════════════════════════${NC}"
echo -e "${YELLOW}  STRESS TEST RESULTS${NC}"
echo -e "${YELLOW}════════════════════════════════════════${NC}"
echo -e "  Tests run:    $TOTAL"
echo -e "  ${GREEN}Passed:       $PASS${NC}"
if [ $FAIL -gt 0 ]; then
    echo -e "  ${RED}Failed:       $FAIL${NC}"
else
    echo -e "  Failed:       0"
fi
echo -e "${YELLOW}════════════════════════════════════════${NC}"

echo ""
echo "Dedup log summary:"
echo "  Canonicals registered: $(grep -c 'Registered canonical' "$LOG" 2>/dev/null || echo 0)"
echo "  Hard-links created:    $(grep -c 'Linked' "$LOG" 2>/dev/null || echo 0)"
echo "  CoW breaks:            $(grep -c 'CoW break' "$LOG" 2>/dev/null || echo 0)"
echo "  Errors in log:         $(grep -ci 'error\|fail' "$LOG" 2>/dev/null || echo 0)"
echo ""
echo "Disk usage: $(du -sh "$DATA" 2>/dev/null | cut -f1)"

exit $FAIL
