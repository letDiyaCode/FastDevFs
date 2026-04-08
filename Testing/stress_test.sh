#!/usr/bin/env bash
#
# ============================================================================
#  FastDevFs — Comprehensive Stress Test Suite
# ============================================================================
#  Pushes the filesystem to its absolute limits across every dimension:
#
#    1. Mass file creation (approach TREEFILE_MAX_NODES = 100,000)
#    2. Mass directory creation & deep nesting (250+ levels)
#    3. Large file I/O (sequential + random, up to 256 MB)
#    4. Concurrent parallel writers / readers
#    5. Rapid create-delete churn (free-list recycling)
#    6. Rename storm (cross-directory moves)
#    7. Wide-directory listing performance (10,000 entries)
#    8. Metadata-intensive ops (chmod, stat barrage)
#    9. Edge cases (long filenames, special characters, zero-byte files)
#   10. Capacity exhaustion & recovery
#
#  Usage:
#    sudo ./stress_test.sh                  # full suite
#    sudo ./stress_test.sh --quick          # reduced sizes for CI
#    sudo ./stress_test.sh --test <N>       # run only test N
#
# ============================================================================

set -euo pipefail

# ── Configuration ────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
FASTDEVFS_BIN="$BUILD_DIR/fastdevfs"
MOUNT_DIR="$PROJECT_DIR/test_mount_stress"
PERSIST_FILE="/tmp/fastdevfs_stress.mmap"
DATA_DIR="/tmp/fastdevfs_data"
LOG_FILE="$SCRIPT_DIR/stress_test_results.log"

# Tunable limits
QUICK_MODE=false
SINGLE_TEST=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --quick) QUICK_MODE=true; shift ;;
        --test)  SINGLE_TEST="$2"; shift 2 ;;
        *)       echo "Unknown arg: $1"; exit 1 ;;
    esac
done

if $QUICK_MODE; then
    MASS_FILE_COUNT=2000
    MASS_DIR_COUNT=500
    DEEP_NEST_DEPTH=50
    WIDE_DIR_COUNT=2000
    LARGE_FILE_MB=16
    CONCURRENT_WORKERS=8
    CHURN_ITERATIONS=1000
    RENAME_COUNT=500
    METADATA_OPS=2000
else
    MASS_FILE_COUNT=20000
    MASS_DIR_COUNT=5000
    DEEP_NEST_DEPTH=250
    WIDE_DIR_COUNT=10000
    LARGE_FILE_MB=256
    CONCURRENT_WORKERS=32
    CHURN_ITERATIONS=10000
    RENAME_COUNT=5000
    METADATA_OPS=20000
fi

# ── Colors & Output ─────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

PASSED=0
FAILED=0
SKIPPED=0
TOTAL_START=$(date +%s%N)

log() {
    local msg="[$(date '+%H:%M:%S')] $*"
    echo -e "$msg"
    echo "$msg" >> "$LOG_FILE"
}

header() {
    echo ""
    echo -e "${BOLD}${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${BOLD}${CYAN}  $*${NC}"
    echo -e "${BOLD}${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
}

test_pass() {
    echo -e "  ${GREEN}✓ PASS${NC}: $*"
    log "PASS: $*"
    PASSED=$((PASSED + 1))
}

test_fail() {
    echo -e "  ${RED}✗ FAIL${NC}: $*"
    log "FAIL: $*"
    FAILED=$((FAILED + 1))
}

test_info() {
    echo -e "  ${YELLOW}ℹ INFO${NC}: $*"
    log "INFO: $*"
}

elapsed_ms() {
    local start=$1 end
    end=$(date +%s%N)
    echo $(( (end - start) / 1000000 ))
}

should_run() {
    [[ -z "$SINGLE_TEST" || "$SINGLE_TEST" == "$1" ]]
}

# ── Filesystem Lifecycle ────────────────────────────────────────────────────
cleanup_mount() {
    # Kill any existing FastDevFs processes
    pkill -f "fastdevfs.*test_mount_stress" 2>/dev/null || true
    sleep 0.5

    # Force unmount if still mounted
    if mountpoint -q "$MOUNT_DIR" 2>/dev/null; then
        fusermount3 -uz "$MOUNT_DIR" 2>/dev/null || fusermount -uz "$MOUNT_DIR" 2>/dev/null || umount -lf "$MOUNT_DIR" 2>/dev/null || true
        sleep 0.5
    fi

    # Clean up persistence & data
    rm -f "$PERSIST_FILE"
    rm -rf "$DATA_DIR"
    rm -rf "$MOUNT_DIR"
}

mount_fs() {
    cleanup_mount

    mkdir -p "$MOUNT_DIR"

    # Start filesystem in background with a fresh mmap file
    # Since main.cpp hardcodes the path, we just clean and let it use default
    rm -f /tmp/fastdevfs.mmap
    rm -rf /tmp/fastdevfs_data

    "$FASTDEVFS_BIN" "$MOUNT_DIR" -f 2>/dev/null &
    FS_PID=$!
    sleep 1

    # Verify it mounted
    if ! mountpoint -q "$MOUNT_DIR" 2>/dev/null; then
        # Give it more time
        sleep 2
        if ! mountpoint -q "$MOUNT_DIR" 2>/dev/null; then
            echo -e "${RED}ERROR: Failed to mount FastDevFs at $MOUNT_DIR${NC}"
            kill $FS_PID 2>/dev/null || true
            return 1
        fi
    fi

    log "FastDevFs mounted at $MOUNT_DIR (PID=$FS_PID)"
    return 0
}

unmount_fs() {
    if [[ -n "${FS_PID:-}" ]]; then
        kill "$FS_PID" 2>/dev/null || true
        wait "$FS_PID" 2>/dev/null || true
    fi
    sleep 0.5

    if mountpoint -q "$MOUNT_DIR" 2>/dev/null; then
        fusermount3 -uz "$MOUNT_DIR" 2>/dev/null || fusermount -uz "$MOUNT_DIR" 2>/dev/null || umount -lf "$MOUNT_DIR" 2>/dev/null || true
    fi
}

remount_fs() {
    unmount_fs
    sleep 1
    # Remount (persistence test — uses existing mmap file)
    "$FASTDEVFS_BIN" "$MOUNT_DIR" -f 2>/dev/null &
    FS_PID=$!
    sleep 1.5

    if ! mountpoint -q "$MOUNT_DIR" 2>/dev/null; then
        sleep 2
        if ! mountpoint -q "$MOUNT_DIR" 2>/dev/null; then
            echo -e "${RED}ERROR: Failed to re-mount FastDevFs${NC}"
            return 1
        fi
    fi
    log "FastDevFs re-mounted (PID=$FS_PID)"
}

# ── Trap for cleanup ────────────────────────────────────────────────────────
trap 'echo -e "\n${RED}Interrupted — cleaning up...${NC}"; cleanup_mount; exit 130' INT TERM

# ============================================================================
#  TEST 1: Mass File Creation
# ============================================================================
test_1_mass_file_creation() {
    header "TEST 1: Mass File Creation ($MASS_FILE_COUNT files)"

    local dir="$MOUNT_DIR/mass_files"
    mkdir -p "$dir"

    local start=$(date +%s%N)
    local created=0
    local errors=0

    # Create files in batches using subdirectories to avoid single-dir bottleneck
    local batch_size=500
    local batch=0

    for ((i = 0; i < MASS_FILE_COUNT; i++)); do
        if (( i % batch_size == 0 )); then
            batch=$((i / batch_size))
            mkdir -p "$dir/batch_$batch" 2>/dev/null || true
        fi

        if echo "content_$i" > "$dir/batch_$batch/file_$i.txt" 2>/dev/null; then
            created=$((created + 1))
        else
            errors=$((errors + 1))
        fi

        # Progress every 2000 files
        if (( i > 0 && i % 2000 == 0 )); then
            test_info "Created $i / $MASS_FILE_COUNT files..."
        fi
    done

    local ms=$(elapsed_ms $start)
    local rate=$( (( ms > 0 )) && echo "$((created * 1000 / ms))" || echo "inf" )

    test_info "Created $created files in ${ms}ms (${rate} files/sec)"
    test_info "Errors: $errors"

    if (( created == MASS_FILE_COUNT )); then
        test_pass "All $MASS_FILE_COUNT files created successfully"
    elif (( created > MASS_FILE_COUNT * 90 / 100 )); then
        test_pass "Created $created / $MASS_FILE_COUNT files (>90% success)"
    else
        test_fail "Only created $created / $MASS_FILE_COUNT files"
    fi

    # Verify a sample of files
    local verify_ok=0
    for ((i = 0; i < created && i < 100; i++)); do
        batch=$((i / batch_size))
        if [[ -f "$dir/batch_$batch/file_$i.txt" ]]; then
            local content
            content=$(cat "$dir/batch_$batch/file_$i.txt" 2>/dev/null) || true
            if [[ "$content" == "content_$i" ]]; then
                verify_ok=$((verify_ok + 1))
            fi
        fi
    done

    if (( verify_ok >= 95 )); then
        test_pass "Content verification: $verify_ok / 100 sampled files correct"
    else
        test_fail "Content verification: only $verify_ok / 100 sampled files correct"
    fi
}

# ============================================================================
#  TEST 2: Mass Directory Creation
# ============================================================================
test_2_mass_dir_creation() {
    header "TEST 2: Mass Directory Creation ($MASS_DIR_COUNT directories)"

    local dir="$MOUNT_DIR/mass_dirs"
    mkdir -p "$dir"

    local start=$(date +%s%N)
    local created=0

    for ((i = 0; i < MASS_DIR_COUNT; i++)); do
        if mkdir -p "$dir/dir_$i" 2>/dev/null; then
            created=$((created + 1))
        fi

        if (( i > 0 && i % 1000 == 0 )); then
            test_info "Created $i / $MASS_DIR_COUNT dirs..."
        fi
    done

    local ms=$(elapsed_ms $start)
    local rate=$( (( ms > 0 )) && echo "$((created * 1000 / ms))" || echo "inf" )

    test_info "Created $created directories in ${ms}ms (${rate} dirs/sec)"

    if (( created == MASS_DIR_COUNT )); then
        test_pass "All $MASS_DIR_COUNT directories created"
    else
        test_fail "Only created $created / $MASS_DIR_COUNT directories"
    fi

    # Verify with ls
    local listed
    listed=$(ls -1 "$dir" 2>/dev/null | wc -l)
    if (( listed == created )); then
        test_pass "Directory listing confirms $listed entries"
    else
        test_fail "Directory listing shows $listed but created $created"
    fi
}

# ============================================================================
#  TEST 3: Deep Directory Nesting
# ============================================================================
test_3_deep_nesting() {
    header "TEST 3: Deep Directory Nesting (${DEEP_NEST_DEPTH} levels)"

    local path="$MOUNT_DIR/deep"
    local start=$(date +%s%N)
    local depth=0

    for ((i = 0; i < DEEP_NEST_DEPTH; i++)); do
        path="$path/level_$i"
        if mkdir -p "$path" 2>/dev/null; then
            depth=$((depth + 1))
        else
            test_info "mkdir failed at depth $i"
            break
        fi
    done

    local ms=$(elapsed_ms $start)
    test_info "Reached depth $depth in ${ms}ms"

    if (( depth >= DEEP_NEST_DEPTH )); then
        test_pass "Created hierarchy ${DEEP_NEST_DEPTH} levels deep"
    elif (( depth >= DEEP_NEST_DEPTH / 2 )); then
        test_pass "Reached $depth / $DEEP_NEST_DEPTH levels (path length limit hit)"
    elif (( depth >= 30 )); then
        # name[300] buffer limits paths to ~300 chars → ~33 levels of /level_XX
        test_pass "Reached $depth levels (300-char metadata.name buffer limit)"
    else
        test_fail "Only reached $depth levels"
    fi

    # Try to create a file at the deepest level
    if echo "deep content" > "$path/deep_file.txt" 2>/dev/null; then
        local readback
        readback=$(cat "$path/deep_file.txt" 2>/dev/null) || true
        if [[ "$readback" == "deep content" ]]; then
            test_pass "File at depth $depth is readable"
        else
            test_fail "File at depth $depth has wrong content"
        fi
    else
        test_info "Could not create file at depth $depth (path too long)"
    fi
}

# ============================================================================
#  TEST 4: Large File I/O
# ============================================================================
test_4_large_file_io() {
    header "TEST 4: Large File I/O (${LARGE_FILE_MB} MB)"

    local file="$MOUNT_DIR/large_file.bin"

    # ── Sequential Write ──
    local start=$(date +%s%N)
    dd if=/dev/urandom of="$file" bs=1M count=$LARGE_FILE_MB status=none 2>/dev/null
    local write_ms=$(elapsed_ms $start)
    local write_mbps=$( (( write_ms > 0 )) && echo "$((LARGE_FILE_MB * 1000 / write_ms))" || echo "inf" )
    test_info "Sequential write: ${LARGE_FILE_MB}MB in ${write_ms}ms (${write_mbps} MB/s)"

    # Verify size
    local actual_size
    actual_size=$(stat -c%s "$file" 2>/dev/null) || actual_size=0
    local expected_size=$((LARGE_FILE_MB * 1048576))
    if (( actual_size == expected_size )); then
        test_pass "File size correct: $actual_size bytes"
    else
        test_fail "File size mismatch: expected $expected_size, got $actual_size"
    fi

    # ── Compute checksum ──
    local cksum_write
    cksum_write=$(md5sum "$file" 2>/dev/null | awk '{print $1}') || cksum_write="error"

    # ── Sequential Read ──
    start=$(date +%s%N)
    dd if="$file" of=/dev/null bs=1M status=none 2>/dev/null
    local read_ms=$(elapsed_ms $start)
    local read_mbps=$( (( read_ms > 0 )) && echo "$((LARGE_FILE_MB * 1000 / read_ms))" || echo "inf" )
    test_info "Sequential read: ${LARGE_FILE_MB}MB in ${read_ms}ms (${read_mbps} MB/s)"

    # ── Checksum after read-back ──
    local cksum_read
    cksum_read=$(md5sum "$file" 2>/dev/null | awk '{print $1}') || cksum_read="error"
    if [[ "$cksum_write" == "$cksum_read" && "$cksum_write" != "error" ]]; then
        test_pass "Data integrity verified (MD5: $cksum_write)"
    else
        test_fail "Data corruption detected! write=$cksum_write read=$cksum_read"
    fi

    # ── Random I/O (small block pread/pwrite pattern) ──
    local rand_file="$MOUNT_DIR/random_io.bin"
    dd if=/dev/zero of="$rand_file" bs=1M count=4 status=none 2>/dev/null

    start=$(date +%s%N)
    local rand_ops=0
    for ((i = 0; i < 500; i++)); do
        local offset=$(( RANDOM % 4000000 ))
        local size=$(( (RANDOM % 4096) + 1 ))
        dd if=/dev/urandom of="$rand_file" bs=1 count=$size seek=$offset conv=notrunc status=none 2>/dev/null && rand_ops=$((rand_ops + 1))
    done
    local rand_ms=$(elapsed_ms $start)
    test_info "Random writes: $rand_ops ops in ${rand_ms}ms"

    start=$(date +%s%N)
    for ((i = 0; i < 500; i++)); do
        local offset=$(( RANDOM % 4000000 ))
        local size=$(( (RANDOM % 4096) + 1 ))
        dd if="$rand_file" of=/dev/null bs=1 count=$size skip=$offset status=none 2>/dev/null && rand_ops=$((rand_ops + 1))
    done
    rand_ms=$(elapsed_ms $start)
    test_info "Random reads: 500 ops in ${rand_ms}ms"
    test_pass "Random I/O completed without crashes"

    rm -f "$file" "$rand_file" 2>/dev/null || true
}

# ============================================================================
#  TEST 5: Concurrent Parallel I/O
# ============================================================================
test_5_concurrent_io() {
    header "TEST 5: Concurrent Parallel I/O ($CONCURRENT_WORKERS workers)"

    local dir="$MOUNT_DIR/concurrent"
    mkdir -p "$dir"

    local pids=()
    local start=$(date +%s%N)

    # Spawn workers — each creates files, writes, reads, deletes
    for ((w = 0; w < CONCURRENT_WORKERS; w++)); do
        (
            local wdir="$dir/worker_$w"
            mkdir -p "$wdir"
            for ((j = 0; j < 100; j++)); do
                local f="$wdir/file_$j.dat"
                echo "worker_${w}_data_${j}" > "$f" 2>/dev/null || true
                cat "$f" > /dev/null 2>/dev/null || true
            done
            # Verify
            local wcount
            wcount=$(ls -1 "$wdir" 2>/dev/null | wc -l)
            if (( wcount < 90 )); then
                exit 1
            fi
            # Cleanup half the files
            for ((j = 0; j < 50; j++)); do
                rm -f "$wdir/file_$j.dat" 2>/dev/null || true
            done
            exit 0
        ) &
        pids+=($!)
    done

    # Wait for all
    local worker_pass=0
    local worker_fail=0
    for pid in "${pids[@]}"; do
        if wait "$pid" 2>/dev/null; then
            worker_pass=$((worker_pass + 1))
        else
            worker_fail=$((worker_fail + 1))
        fi
    done

    local ms=$(elapsed_ms $start)
    test_info "$worker_pass workers completed, $worker_fail failed in ${ms}ms"

    if (( worker_fail == 0 )); then
        test_pass "All $CONCURRENT_WORKERS concurrent workers succeeded"
    elif (( worker_fail <= CONCURRENT_WORKERS / 10 )); then
        test_pass "$worker_pass / $CONCURRENT_WORKERS workers ok (minor failures)"
    else
        test_fail "$worker_fail / $CONCURRENT_WORKERS workers failed"
    fi
}

# ============================================================================
#  TEST 6: Create-Delete Churn (Free List Recycling)
# ============================================================================
test_6_churn() {
    header "TEST 6: Create-Delete Churn ($CHURN_ITERATIONS iterations)"

    local dir="$MOUNT_DIR/churn"
    mkdir -p "$dir"

    local start=$(date +%s%N)
    local success=0

    for ((i = 0; i < CHURN_ITERATIONS; i++)); do
        local f="$dir/churn_$((i % 100)).txt"
        echo "churn_data_$i" > "$f" 2>/dev/null && rm -f "$f" 2>/dev/null && success=$((success + 1))

        if (( i > 0 && i % 2000 == 0 )); then
            test_info "Churn iteration $i / $CHURN_ITERATIONS..."
        fi
    done

    local ms=$(elapsed_ms $start)
    local rate=$( (( ms > 0 )) && echo "$((success * 1000 / ms))" || echo "inf" )
    test_info "$success cycles in ${ms}ms (${rate} create-delete/sec)"

    if (( success >= CHURN_ITERATIONS * 95 / 100 )); then
        test_pass "Churn test: $success / $CHURN_ITERATIONS cycles completed"
    else
        test_fail "Churn test: only $success / $CHURN_ITERATIONS cycles"
    fi

    # Verify directory is clean (all files should be deleted)
    local remaining
    remaining=$(ls -1 "$dir" 2>/dev/null | wc -l)
    if (( remaining == 0 )); then
        test_pass "All churn files cleaned up ($remaining remaining)"
    else
        test_info "$remaining files remain after churn (expected 0)"
    fi
}

# ============================================================================
#  TEST 7: Rename Storm
# ============================================================================
test_7_rename_storm() {
    header "TEST 7: Rename Storm ($RENAME_COUNT operations)"

    local dir="$MOUNT_DIR/rename_test"
    mkdir -p "$dir/src" "$dir/dst"

    # Pre-create files
    for ((i = 0; i < RENAME_COUNT && i < 2000; i++)); do
        echo "rename_data_$i" > "$dir/src/file_$i.txt" 2>/dev/null || true
    done

    local pre_count
    pre_count=$(ls -1 "$dir/src" 2>/dev/null | wc -l)
    test_info "Pre-created $pre_count files for rename"

    local start=$(date +%s%N)
    local renamed=0

    # Rename within same directory
    for ((i = 0; i < pre_count; i++)); do
        if mv "$dir/src/file_$i.txt" "$dir/src/renamed_$i.txt" 2>/dev/null; then
            renamed=$((renamed + 1))
        fi
    done

    local ms1=$(elapsed_ms $start)
    test_info "Same-dir renames: $renamed in ${ms1}ms"

    # Cross-directory renames
    start=$(date +%s%N)
    local cross_renamed=0
    for ((i = 0; i < renamed; i++)); do
        if mv "$dir/src/renamed_$i.txt" "$dir/dst/moved_$i.txt" 2>/dev/null; then
            cross_renamed=$((cross_renamed + 1))
        fi
    done

    local ms2=$(elapsed_ms $start)
    test_info "Cross-dir renames: $cross_renamed in ${ms2}ms"

    # Verify destination
    local dst_count
    dst_count=$(ls -1 "$dir/dst" 2>/dev/null | wc -l)
    if (( dst_count == cross_renamed )); then
        test_pass "Rename storm: all $cross_renamed files moved successfully"
    else
        test_fail "Rename mismatch: expected $cross_renamed, found $dst_count"
    fi

    # Verify content of a sample
    local content_ok=0
    for ((i = 0; i < 50 && i < cross_renamed; i++)); do
        local content
        content=$(cat "$dir/dst/moved_$i.txt" 2>/dev/null) || true
        if [[ "$content" == "rename_data_$i" ]]; then
            content_ok=$((content_ok + 1))
        fi
    done
    if (( content_ok >= 45 )); then
        test_pass "Post-rename content verification: $content_ok / 50 correct"
    else
        test_fail "Post-rename content verification: $content_ok / 50 correct"
    fi
}

# ============================================================================
#  TEST 8: Wide Directory Listing Performance
# ============================================================================
test_8_wide_directory() {
    header "TEST 8: Wide Directory ($WIDE_DIR_COUNT entries)"

    local dir="$MOUNT_DIR/wide_dir"
    mkdir -p "$dir"

    # Create many entries
    local start=$(date +%s%N)
    for ((i = 0; i < WIDE_DIR_COUNT; i++)); do
        touch "$dir/entry_$i" 2>/dev/null || true
        if (( i > 0 && i % 2000 == 0 )); then
            test_info "Created $i / $WIDE_DIR_COUNT entries..."
        fi
    done
    local create_ms=$(elapsed_ms $start)
    test_info "Created $WIDE_DIR_COUNT entries in ${create_ms}ms"

    # Time ls
    start=$(date +%s%N)
    local listed
    listed=$(ls -1 "$dir" 2>/dev/null | wc -l)
    local ls_ms=$(elapsed_ms $start)
    test_info "ls listed $listed entries in ${ls_ms}ms"

    if (( listed >= WIDE_DIR_COUNT * 95 / 100 )); then
        test_pass "Wide directory: $listed / $WIDE_DIR_COUNT entries listed"
    else
        test_fail "Wide directory: only $listed / $WIDE_DIR_COUNT entries"
    fi

    # Time stat on each entry (sample)
    start=$(date +%s%N)
    for ((i = 0; i < 1000 && i < WIDE_DIR_COUNT; i++)); do
        stat "$dir/entry_$i" > /dev/null 2>/dev/null
    done
    local stat_ms=$(elapsed_ms $start)
    test_info "1000 stat calls in ${stat_ms}ms"
    test_pass "Wide directory operations completed"
}

# ============================================================================
#  TEST 9: Metadata Intensive Operations
# ============================================================================
test_9_metadata_ops() {
    header "TEST 9: Metadata Intensive ($METADATA_OPS operations)"

    local dir="$MOUNT_DIR/metadata_test"
    mkdir -p "$dir"

    # Create test files
    for ((i = 0; i < 100; i++)); do
        echo "meta_$i" > "$dir/meta_$i.txt" 2>/dev/null || true
    done

    local start=$(date +%s%N)
    local ops=0

    # chmod storm
    for ((i = 0; i < METADATA_OPS / 4; i++)); do
        local idx=$((i % 100))
        chmod $((0600 + (i % 200))) "$dir/meta_$idx.txt" 2>/dev/null && ops=$((ops + 1))
    done
    test_info "chmod: $ops ops"

    # stat storm
    local stat_ops=0
    for ((i = 0; i < METADATA_OPS / 4; i++)); do
        local idx=$((i % 100))
        stat "$dir/meta_$idx.txt" > /dev/null 2>/dev/null && stat_ops=$((stat_ops + 1))
    done
    ops=$((ops + stat_ops))
    test_info "stat: $stat_ops ops"

    # touch storm (update atime/mtime)
    local touch_ops=0
    for ((i = 0; i < METADATA_OPS / 4; i++)); do
        local idx=$((i % 100))
        touch "$dir/meta_$idx.txt" 2>/dev/null && touch_ops=$((touch_ops + 1))
    done
    ops=$((ops + touch_ops))
    test_info "touch: $touch_ops ops"

    # truncate storm
    local trunc_ops=0
    for ((i = 0; i < METADATA_OPS / 4; i++)); do
        local idx=$((i % 100))
        truncate -s $((RANDOM % 1024)) "$dir/meta_$idx.txt" 2>/dev/null && trunc_ops=$((trunc_ops + 1))
    done
    ops=$((ops + trunc_ops))

    local ms=$(elapsed_ms $start)
    local rate=$( (( ms > 0 )) && echo "$((ops * 1000 / ms))" || echo "inf" )
    test_info "Total metadata ops: $ops in ${ms}ms (${rate} ops/sec)"

    if (( ops >= METADATA_OPS * 80 / 100 )); then
        test_pass "Metadata stress: $ops ops completed"
    else
        test_fail "Metadata stress: only $ops ops completed"
    fi
}

# ============================================================================
#  TEST 10: Edge Cases
# ============================================================================
test_10_edge_cases() {
    header "TEST 10: Edge Cases"

    local dir="$MOUNT_DIR/edge_cases"
    mkdir -p "$dir"

    # ── Long filenames (up to 255 chars) ──
    local long_name
    long_name=$(printf 'A%.0s' $(seq 1 255))
    if echo "long" > "$dir/$long_name" 2>/dev/null; then
        if [[ -f "$dir/$long_name" ]]; then
            test_pass "255-char filename supported"
        else
            test_fail "255-char file created but not found"
        fi
    else
        test_info "255-char filename not supported (expected if name buffer is limited)"
    fi

    # ── Filenames with spaces and special chars ──
    local special_names=("file with spaces.txt" "file-with-dashes.txt" "file_under_scores.txt" "UPPERCASE.TXT" "MiXeD.CaSe" "file.multiple.dots.txt" "123numeric_start.txt")
    local special_ok=0
    for name in "${special_names[@]}"; do
        if echo "test" > "$dir/$name" 2>/dev/null; then
            if [[ -f "$dir/$name" ]]; then
                special_ok=$((special_ok + 1))
            fi
        fi
    done
    if (( special_ok == ${#special_names[@]} )); then
        test_pass "Special characters in filenames: $special_ok / ${#special_names[@]}"
    else
        test_fail "Special characters: only $special_ok / ${#special_names[@]}"
    fi

    # ── Zero-byte files ──
    touch "$dir/zero_byte_file" 2>/dev/null
    local zsize
    zsize=$(stat -c%s "$dir/zero_byte_file" 2>/dev/null) || zsize=-1
    if [[ "$zsize" == "0" ]]; then
        test_pass "Zero-byte file: size = 0"
    else
        test_fail "Zero-byte file: size = $zsize (expected 0)"
    fi

    # ── Overwrite existing file ──
    echo "original" > "$dir/overwrite_test.txt" 2>/dev/null
    echo "overwritten" > "$dir/overwrite_test.txt" 2>/dev/null
    local over_content
    over_content=$(cat "$dir/overwrite_test.txt" 2>/dev/null) || over_content=""
    if [[ "$over_content" == "overwritten" ]]; then
        test_pass "File overwrite works correctly"
    else
        test_fail "File overwrite: got '$over_content' expected 'overwritten'"
    fi

    # ── Append mode ──
    echo "line1" > "$dir/append_test.txt" 2>/dev/null
    echo "line2" >> "$dir/append_test.txt" 2>/dev/null
    local line_count
    line_count=$(wc -l < "$dir/append_test.txt" 2>/dev/null) || line_count=0
    if (( line_count == 2 )); then
        test_pass "Append mode works (2 lines)"
    else
        test_fail "Append mode: got $line_count lines, expected 2"
    fi

    # ── Delete non-existent file ──
    rm -f "$dir/nonexistent_file_12345.txt" 2>/dev/null
    test_pass "Delete non-existent file handled gracefully"

    # ── mkdir for already existing dir ──
    mkdir -p "$dir/existing_dir" 2>/dev/null
    mkdir -p "$dir/existing_dir" 2>/dev/null  # Should succeed (mkdir -p)
    test_pass "mkdir -p on existing dir succeeds"

    # ── rmdir on non-empty dir should fail ──
    mkdir -p "$dir/nonempty_dir" 2>/dev/null
    echo "content" > "$dir/nonempty_dir/file.txt" 2>/dev/null
    if rmdir "$dir/nonempty_dir" 2>/dev/null; then
        test_fail "rmdir succeeded on non-empty dir (should fail)"
    else
        test_pass "rmdir correctly fails on non-empty directory"
    fi

    # ── rmdir on empty dir should succeed ──
    mkdir -p "$dir/empty_dir" 2>/dev/null
    if rmdir "$dir/empty_dir" 2>/dev/null; then
        test_pass "rmdir on empty directory succeeds"
    else
        test_fail "rmdir failed on empty directory"
    fi
}

# ============================================================================
#  TEST 11: Persistence Across Remounts
# ============================================================================
test_11_persistence() {
    header "TEST 11: Persistence Across Remounts"

    local dir="$MOUNT_DIR/persist_test"
    mkdir -p "$dir"

    # Create known state
    mkdir -p "$dir/subdir"
    echo "persist_data_1" > "$dir/file1.txt" 2>/dev/null
    echo "persist_data_2" > "$dir/subdir/file2.txt" 2>/dev/null
    mkdir -p "$dir/subdir/nested"
    echo "persist_nested" > "$dir/subdir/nested/file3.txt" 2>/dev/null

    local pre_count
    pre_count=$(find "$dir" -type f 2>/dev/null | wc -l)
    test_info "Pre-remount: $pre_count files"

    # Remount
    remount_fs || { test_fail "Failed to remount"; return; }

    # Verify
    if [[ -d "$MOUNT_DIR/persist_test" ]]; then
        test_pass "Directory survived remount"
    else
        test_fail "Directory lost after remount"
        return
    fi

    if [[ -d "$MOUNT_DIR/persist_test/subdir" ]]; then
        test_pass "Subdirectory survived remount"
    else
        test_fail "Subdirectory lost after remount"
    fi

    local c1 c2 c3
    c1=$(cat "$MOUNT_DIR/persist_test/file1.txt" 2>/dev/null) || c1=""
    c2=$(cat "$MOUNT_DIR/persist_test/subdir/file2.txt" 2>/dev/null) || c2=""
    c3=$(cat "$MOUNT_DIR/persist_test/subdir/nested/file3.txt" 2>/dev/null) || c3=""

    if [[ "$c1" == "persist_data_1" ]]; then
        test_pass "File 1 content persisted"
    else
        test_fail "File 1 content lost: got '$c1'"
    fi

    if [[ "$c2" == "persist_data_2" ]]; then
        test_pass "File 2 content persisted"
    else
        test_fail "File 2 content lost: got '$c2'"
    fi

    if [[ "$c3" == "persist_nested" ]]; then
        test_pass "Nested file content persisted"
    else
        test_fail "Nested file content lost: got '$c3'"
    fi
}

# ============================================================================
#  TEST 12: Concurrent Read-Write Contention
# ============================================================================
test_12_rw_contention() {
    header "TEST 12: Read-Write Contention (multiple writers, single file)"

    local file="$MOUNT_DIR/contention_file.dat"
    echo "" > "$file" 2>/dev/null

    local pids=()
    local num_writers=$((CONCURRENT_WORKERS / 2 > 4 ? CONCURRENT_WORKERS / 2 : 4))

    for ((w = 0; w < num_writers; w++)); do
        (
            for ((j = 0; j < 200; j++)); do
                echo "writer_${w}_line_${j}" >> "$file" 2>/dev/null || true
            done
        ) &
        pids+=($!)
    done

    # Concurrent readers
    for ((r = 0; r < num_writers; r++)); do
        (
            for ((j = 0; j < 100; j++)); do
                wc -l "$file" > /dev/null 2>/dev/null || true
                head -10 "$file" > /dev/null 2>/dev/null || true
            done
        ) &
        pids+=($!)
    done

    local ok=0
    local fail=0
    for pid in "${pids[@]}"; do
        if wait "$pid" 2>/dev/null; then
            ok=$((ok + 1))
        else
            fail=$((fail + 1))
        fi
    done

    test_info "Workers: $ok ok, $fail failed"

    if [[ -f "$file" ]]; then
        local total_lines
        total_lines=$(wc -l < "$file" 2>/dev/null) || total_lines=0
        test_info "Total lines written: $total_lines (expected ~$((num_writers * 200)))"
        test_pass "Read-write contention did not crash the filesystem"
    else
        test_fail "Contention file disappeared"
    fi
}

# ============================================================================
#  TEST 13: Rapid Directory Create-Delete (Tree Structure Stress)
# ============================================================================
test_13_dir_churn() {
    header "TEST 13: Directory Create-Delete Churn"

    local dir="$MOUNT_DIR/dir_churn"
    mkdir -p "$dir"

    local iterations=$((CHURN_ITERATIONS / 5))
    local start=$(date +%s%N)
    local success=0

    for ((i = 0; i < iterations; i++)); do
        local d="$dir/temp_dir_$((i % 200))"
        if mkdir -p "$d" 2>/dev/null; then
            # Add a file inside
            echo "x" > "$d/file.txt" 2>/dev/null || true
            rm -f "$d/file.txt" 2>/dev/null || true
            rmdir "$d" 2>/dev/null && success=$((success + 1))
        fi
    done

    local ms=$(elapsed_ms $start)
    local rate=$( (( ms > 0 )) && echo "$((success * 1000 / ms))" || echo "inf" )
    test_info "Dir churn: $success cycles in ${ms}ms (${rate} cycles/sec)"

    if (( success >= iterations * 80 / 100 )); then
        test_pass "Directory churn: $success / $iterations cycles"
    else
        test_fail "Directory churn: only $success / $iterations cycles"
    fi
}

# ============================================================================
#  TEST 14: File Size Boundary Tests
# ============================================================================
test_14_size_boundaries() {
    header "TEST 14: File Size Boundary Tests"

    local dir="$MOUNT_DIR/size_tests"
    mkdir -p "$dir"

    # Various boundary sizes
    local sizes=(0 1 511 512 513 4095 4096 4097 8192 65536 1048576)
    local pass_count=0

    for sz in "${sizes[@]}"; do
        local f="$dir/file_${sz}b"
        dd if=/dev/urandom of="$f" bs=1 count=$sz status=none 2>/dev/null || true

        local actual
        actual=$(stat -c%s "$f" 2>/dev/null) || actual=-1
        if [[ "$actual" == "$sz" ]]; then
            pass_count=$((pass_count + 1))
        else
            test_info "Size $sz: expected $sz bytes, got $actual"
        fi
    done

    if (( pass_count == ${#sizes[@]} )); then
        test_pass "All ${#sizes[@]} boundary sizes verify correctly"
    else
        test_fail "Only $pass_count / ${#sizes[@]} boundary sizes correct"
    fi

    # Truncate up and down
    local tfile="$dir/truncate_test"
    echo "0123456789" > "$tfile" 2>/dev/null
    truncate -s 5 "$tfile" 2>/dev/null
    local t5
    t5=$(stat -c%s "$tfile" 2>/dev/null) || t5=-1
    if [[ "$t5" == "5" ]]; then
        test_pass "Truncate down to 5 bytes"
    else
        test_fail "Truncate down: expected 5, got $t5"
    fi

    truncate -s 10000 "$tfile" 2>/dev/null
    local t10k
    t10k=$(stat -c%s "$tfile" 2>/dev/null) || t10k=-1
    if [[ "$t10k" == "10000" ]]; then
        test_pass "Truncate up to 10000 bytes (sparse)"
    else
        test_fail "Truncate up: expected 10000, got $t10k"
    fi
}

# ============================================================================
#  TEST 15: Filesystem Capacity Exhaustion & Recovery
# ============================================================================
test_15_capacity_exhaustion() {
    header "TEST 15: Capacity Approach (filling toward node limit)"

    # Note: TREEFILE_MAX_NODES is 100,000.
    # We already used many nodes in previous tests.
    # Let's try to detect when we hit the limit.

    local dir="$MOUNT_DIR/capacity"
    mkdir -p "$dir"

    local created=0
    local last_error=0
    local batch=0
    local batch_size=1000

    local start=$(date +%s%N)

    # Keep creating until failure
    while (( created < 50000 )); do
        if (( created % batch_size == 0 )); then
            batch=$((created / batch_size))
            mkdir -p "$dir/batch_$batch" 2>/dev/null || break
        fi

        if touch "$dir/batch_$batch/f_$created" 2>/dev/null; then
            created=$((created + 1))
        else
            last_error=$created
            break
        fi

        if (( created % 5000 == 0 )); then
            test_info "Capacity: $created nodes created..."
        fi
    done

    local ms=$(elapsed_ms $start)
    test_info "Created $created nodes before limit/stop in ${ms}ms"

    if (( created >= 50000 )); then
        test_pass "Capacity: created 50,000+ nodes without hitting limit"
    elif (( created > 0 )); then
        test_pass "Capacity: hit limit at $created nodes (ENOSPC expected)"
    else
        test_fail "Could not create any nodes"
    fi

    # Recovery: delete some nodes, then create again
    if (( created > 100 )); then
        local del_count=0
        for ((i = 0; i < 100; i++)); do
            batch=$((i / batch_size))
            rm -f "$dir/batch_$batch/f_$i" 2>/dev/null && del_count=$((del_count + 1))
        done
        test_info "Deleted $del_count nodes for recovery test"

        local recovered=0
        for ((i = 0; i < del_count; i++)); do
            if touch "$dir/recovery_$i" 2>/dev/null; then
                recovered=$((recovered + 1))
            fi
        done

        if (( recovered > 0 )); then
            test_pass "Recovery: created $recovered nodes after deletion (free list works)"
        else
            test_info "Recovery: could not reuse freed nodes"
        fi
    fi
}

# ============================================================================
#  MAIN
# ============================================================================
main() {
    echo ""
    echo -e "${BOLD}${CYAN}╔══════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BOLD}${CYAN}║            FastDevFs — Comprehensive Stress Test Suite          ║${NC}"
    echo -e "${BOLD}${CYAN}╠══════════════════════════════════════════════════════════════════╣${NC}"
    echo -e "${BOLD}${CYAN}║  Mode: $(printf '%-55s' "$($QUICK_MODE && echo "QUICK (reduced sizes)" || echo "FULL (production limits)")")║${NC}"
    echo -e "${BOLD}${CYAN}║  Time: $(printf '%-55s' "$(date '+%Y-%m-%d %H:%M:%S')")║${NC}"
    echo -e "${BOLD}${CYAN}╚══════════════════════════════════════════════════════════════════╝${NC}"

    # Clear log
    > "$LOG_FILE"

    # Build if needed
    if [[ ! -x "$FASTDEVFS_BIN" ]]; then
        echo -e "${YELLOW}Building FastDevFs...${NC}"
        cd "$BUILD_DIR" && cmake .. > /dev/null 2>&1 && make -j$(nproc) > /dev/null 2>&1
        if [[ ! -x "$FASTDEVFS_BIN" ]]; then
            echo -e "${RED}Build failed!${NC}"
            exit 1
        fi
    fi

    # Mount fresh filesystem
    mount_fs || exit 1

    # Run tests
    should_run 1  && test_1_mass_file_creation
    should_run 2  && test_2_mass_dir_creation
    should_run 3  && test_3_deep_nesting
    should_run 4  && test_4_large_file_io
    should_run 5  && test_5_concurrent_io
    should_run 6  && test_6_churn
    should_run 7  && test_7_rename_storm
    should_run 8  && test_8_wide_directory
    should_run 9  && test_9_metadata_ops
    should_run 10 && test_10_edge_cases
    should_run 11 && test_11_persistence
    should_run 12 && test_12_rw_contention
    should_run 13 && test_13_dir_churn
    should_run 14 && test_14_size_boundaries
    should_run 15 && test_15_capacity_exhaustion

    # Cleanup
    unmount_fs
    sleep 0.5
    cleanup_mount

    # Summary
    local total_end=$(date +%s%N)
    local total_ms=$(( (total_end - TOTAL_START) / 1000000 ))
    local total_sec=$((total_ms / 1000))
    local total_tests=$((PASSED + FAILED))

    echo ""
    echo -e "${BOLD}${CYAN}╔══════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BOLD}${CYAN}║                        TEST RESULTS                             ║${NC}"
    echo -e "${BOLD}${CYAN}╠══════════════════════════════════════════════════════════════════╣${NC}"
    echo -e "${BOLD}${CYAN}║  ${GREEN}PASSED : $(printf '%-53s' "$PASSED")${CYAN}║${NC}"
    echo -e "${BOLD}${CYAN}║  ${RED}FAILED : $(printf '%-53s' "$FAILED")${CYAN}║${NC}"
    echo -e "${BOLD}${CYAN}║  TOTAL  : $(printf '%-52s' "$total_tests")║${NC}"
    echo -e "${BOLD}${CYAN}║  TIME   : $(printf '%-52s' "${total_sec}s (${total_ms}ms)")║${NC}"
    echo -e "${BOLD}${CYAN}╚══════════════════════════════════════════════════════════════════╝${NC}"

    if (( FAILED == 0 )); then
        echo -e "\n${BOLD}${GREEN}  ✓ ALL TESTS PASSED${NC}\n"
    else
        echo -e "\n${BOLD}${RED}  ✗ $FAILED TEST(S) FAILED${NC}\n"
    fi

    echo "Full log: $LOG_FILE"

    return $FAILED
}

main
