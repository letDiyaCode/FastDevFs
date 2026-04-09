# FastDevFS CLI Tool — Complete Tutorial and Testing Guide

## Overview

The **FastDevFS CLI tool** (`fastdevfs-cli`) is a production-quality management interface for controlling the FastDevFS daemon. It provides commands for:

- **Daemon Management**: Start/stop/status operations
- **Directory Scanning**: Analyze file contents and metadata
- **Deduplication**: Trigger dedup passes and view statistics
- **File Operations**: Compute hashes, scan directories
- **Configuration**: Manage persistent settings

The CLI uses **CLI11** for robust argument parsing and supports both daemon and offline operations.

## Building the CLI Tool

### Prerequisites

Ensure you have already built the main project:

```bash
cd FastDevFs
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

### Verify CLI Tool Binary

```bash
ls -la build/fastdevfs-cli
# Output: -rwxr-xr-x ... fastdevfs-cli
```

If not present, the build may have failed. Check:
```bash
cd build && cmake .. && make 2>&1 | grep -i error
```

---

## Global Options

All commands support these global flags:

```bash
fastdevfs-cli [GLOBAL_OPTIONS] <command> [COMMAND_OPTIONS]
```

| Option | Description |
|--------|-------------|
| `-v, --verbose` | Enable verbose logging (shows details like IPC communication, hash computation) |
| `--config <path>` | Use alternate config file (default: `./config.ini`) |
| `-h, --help` | Display help for command or subcommand |

### Examples of Global Options

```bash
# Verbose mode
fastdevfs-cli -v status

# Use custom config
fastdevfs-cli --config /etc/fastdevfs.conf status

# Get help
fastdevfs-cli --help
fastdevfs-cli start --help
```

---

## Command Reference

### 1. START — Launch the Daemon

**Purpose**: Start the FastDevFS filesystem daemon

**Syntax**:
```bash
fastdevfs-cli start [OPTIONS]
```

**Options**:

| Option | Description |
|--------|-------------|
| `-m, --mountpoint <PATH>` | Mount point directory (must exist) |
| `-d, --daemon` | Run in background (daemon mode); without it runs in foreground |

**Mode Comparison**:

| Mode | Behavior | Use Case |
|------|----------|----------|
| **Foreground** | Blocks terminal; logs shown; Ctrl+C unmounts | Development/debugging |
| **Daemon** | Runs in background; returns immediately | Production/persistent mount |

#### Example 1: Foreground Mode (Development)

```bash
# Create mount point
mkdir -p ~/fastdevfs_mnt

# Start in foreground
fastdevfs-cli start --mountpoint ~/fastdevfs_mnt
```

**Expected Output**:
```
Starting FastDevFs on /home/user/fastdevfs_mnt...
FastDevFs mounted at /home/user/fastdevfs_mnt
[FUSE kernel module output...]
```

**To stop**: Press `Ctrl+C` in the terminal

#### Example 2: Daemon Mode (Production)

```bash
# Start in background
fastdevfs-cli start --daemon --mountpoint ~/fastdevfs_mnt
```

**Expected Output**:
```
Starting FastDevFs on /home/user/fastdevfs_mnt...
Daemon started in background.
```

**Filesystem remains mounted** even after terminal closes.

#### Example 3: Using Config File

Create `config.ini`:
```ini
mountpoint=/home/user/fastdevfs_mnt
dedup_enabled=1
```

Then simply:
```bash
fastdevfs-cli --config config.ini start
```

#### Test Case: START_TC1

**Test**: Start daemon and verify mountpoint

```bash
#!/bin/bash
set -e

# Setup
TEST_MNT="/tmp/fastdevfs_test_mnt_$$"
mkdir -p "$TEST_MNT"

# Test foreground mode (timeout after 3 seconds)
timeout 3 fastdevfs-cli start --mountpoint "$TEST_MNT" 2>&1 &
sleep 1

# Verify mount exists
if mount | grep -q "$TEST_MNT"; then
    echo "✓ PASS: Mountpoint recognized"
else
    echo "✗ FAIL: Mountpoint not mounted"
    exit 1
fi

# Cleanup
fusermount -u "$TEST_MNT" 2>/dev/null || true
rm -rf "$TEST_MNT"
```

---

### 2. STOP — Shut Down the Daemon

**Purpose**: Gracefully stop the running FastDevFS daemon

**Syntax**:
```bash
fastdevfs-cli stop
```

**Behavior**:
1. Sends IPC command to daemon (preferred)
2. Falls back to SIGTERM if IPC fails
3. Cleans up PID file
4. Unmounts filesystem

#### Example 1: Simple Stop

```bash
fastdevfs-cli stop
```

**Expected Output**:
```
FastDevFs daemon is shutting down.
```

#### Example 2: Stop with Verification

```bash
# Stop
fastdevfs-cli stop

# Verify it stopped
sleep 1
fastdevfs-cli status
```

**Expected Output after stop**:
```
FastDevFs daemon is NOT running.
```

#### Test Case: STOP_TC1

**Test**: Start, then stop, verify unmounted

```bash
#!/bin/bash
set -e

TEST_MNT="/tmp/fastdevfs_test_stop_$$"
mkdir -p "$TEST_MNT"

# Start daemon
fastdevfs-cli start --daemon --mountpoint "$TEST_MNT"
sleep 2

# Verify running
if ! fastdevfs-cli status 2>&1 | grep -q "RUNNING"; then
    echo "✗ FAIL: Daemon not running after start"
    exit 1
fi

# Stop
fastdevfs-cli stop
sleep 1

# Verify stopped
if fastdevfs-cli status 2>&1 | grep -q "NOT running"; then
    echo "✓ PASS: Daemon stopped successfully"
else
    echo "✗ FAIL: Daemon still running"
    exit 1
fi

rm -rf "$TEST_MNT"
```

---

### 3. STATUS — Check Daemon State

**Purpose**: Display daemon status and live statistics

**Syntax**:
```bash
fastdevfs-cli status
```

**Output Fields** (when daemon is running):

| Field | Description |
|-------|-------------|
| `mountpoint` | Where filesystem is mounted |
| `pid` | Daemon process ID |
| `uptime` | How long daemon has been running |
| `files_managed` | Number of files in tree |
| `total_size` | Total data stored |
| `dedup_saved` | Space saved by deduplication |

#### Example 1: Daemon Running

```bash
fastdevfs-cli status
```

**Output**:
```
FastDevFs daemon is RUNNING.
─────────────────────────────
  mountpoint:        /home/user/fastdevfs_mnt
  pid:               12345
  uptime:            00:05:23
  files_managed:     156
  total_size:        2.3 GB
  dedup_saved:       450 MB
```

#### Example 2: Daemon Not Running

```bash
fastdevfs-cli status
```

**Output**:
```
FastDevFs daemon is NOT running.
(stale PID file found: 12345)
```

#### Example 3: With Verbose Mode

```bash
fastdevfs-cli -v status
```

**Additional Output**:
```
[verbose] Querying daemon status via IPC
[verbose] IPC socket: /tmp/fastdevfs.sock
```

#### Test Case: STATUS_TC1

**Test**: Verify status reporting

```bash
#!/bin/bash
set -e

TEST_MNT="/tmp/fastdevfs_test_status_$$"
mkdir -p "$TEST_MNT"

# Start daemon
fastdevfs-cli start --daemon --mountpoint "$TEST_MNT"
sleep 2

# Check status
STATUS_OUTPUT=$(fastdevfs-cli status)

# Verify key fields exist
for field in "RUNNING" "mountpoint" "pid" "uptime"; do
    if echo "$STATUS_OUTPUT" | grep -q "$field"; then
        echo "✓ Field '$field' present"
    else
        echo "✗ FAIL: Missing field '$field'"
        fastdevfs-cli stop
        rm -rf "$TEST_MNT"
        exit 1
    fi
done

echo "✓ PASS: Status reporting correct"

fastdevfs-cli stop
rm -rf "$TEST_MNT"
```

---

### 4. SCAN — Analyze Directory Contents

**Purpose**: Recursively scan a directory, list files with sizes and hashes

**Syntax**:
```bash
fastdevfs-cli scan <PATH> [OPTIONS]
```

**Options**:

| Option | Description |
|--------|-------------|
| `<PATH>` | Directory to scan (required) |
| `-r, --recursive` | Scan subdirectories recursively |

**Output Format** (normal):
```
Scanning: /path/to/dir
────────────────────────────────
  /path/to/file1.txt (4096 bytes)
  /path/to/file2.bin (102400 bytes)
────────────────────────────────
Files: 2  Directories: 1  Total size: 106496 bytes
```

**Output Format** (verbose with `-v`):
```
  /path/to/file1.txt (4096 bytes) SHA256: a1b2c3d4e5f6...
  /path/to/file2.bin (102400 bytes) SHA256: 9a8b7c6d5e4f...
```

#### Example 1: Simple Scan

```bash
fastdevfs-cli scan ~/fastdevfs_mnt
```

#### Example 2: Recursive Scan with Verbose

```bash
fastdevfs-cli -v scan ~/fastdevfs_mnt -r
```

**Output**:
```
[verbose] Computing SHA-256 hash for: /home/user/fastdevfs_mnt/file1.txt
[verbose] Computing SHA-256 hash for: /home/user/fastdevfs_mnt/subdir/file2.txt
Scanning: /home/user/fastdevfs_mnt (recursive)
────────────────────────────────────────────────────
  /home/user/fastdevfs_mnt/file1.txt (1024 bytes) SHA256: 3f3a...
  /home/user/fastdevfs_mnt/subdir/file2.txt (2048 bytes) SHA256: 9e4b...
────────────────────────────────────────────────────
Files: 2  Directories: 1  Total size: 3072 bytes
```

#### Example 3: Non-existent Path

```bash
fastdevfs-cli scan /nonexistent/path
```

**Output**:
```
Error: path '/nonexistent/path' does not exist.
```

#### Test Case: SCAN_TC1

**Test**: Scan mounted filesystem with known files

```bash
#!/bin/bash
set -e

TEST_MNT="/tmp/fastdevfs_test_scan_$$"
mkdir -p "$TEST_MNT"

fastdevfs-cli start --daemon --mountpoint "$TEST_MNT"
sleep 2

# Create test files
echo "test content 1" > "$TEST_MNT/file1.txt"
echo "test content 2" > "$TEST_MNT/file2.txt"
mkdir -p "$TEST_MNT/subdir"
echo "nested file" > "$TEST_MNT/subdir/file3.txt"
sleep 1

# Scan non-recursive
SCAN=$(fastdevfs-cli scan "$TEST_MNT")
if echo "$SCAN" | grep -q "Files: 2"; then
    echo "✓ PASS: Non-recursive scan correct"
else
    echo "✗ FAIL: Non-recursive scan"
    fastdevfs-cli stop
    rm -rf "$TEST_MNT"
    exit 1
fi

# Scan recursive
SCAN_R=$(fastdevfs-cli scan "$TEST_MNT" -r)
if echo "$SCAN_R" | grep -q "Files: 3"; then
    echo "✓ PASS: Recursive scan correct"
else
    echo "✗ FAIL: Recursive scan"
    fastdevfs-cli stop
    rm -rf "$TEST_MNT"
    exit 1
fi

fastdevfs-cli stop
rm -rf "$TEST_MNT"
```

---

### 5. HASH — Compute File Hash

**Purpose**: Compute SHA-256 hash of a file

**Syntax**:
```bash
fastdevfs-cli hash <FILE>
```

**Options**:

| Option | Description |
|--------|-------------|
| `<FILE>` | File to hash (required, must exist and be regular file) |

**Output Format**:
```
<64-character-hex-hash>  <filepath>
```

#### Example 1: Hash a Regular File

```bash
echo "Hello, FastDevFS!" > ~/test.txt
fastdevfs-cli hash ~/test.txt
```

**Output**:
```
7f83b1657ff1fc53b92dc18148a1d65dfc2d4b1fa3d677284addd200126d9069  /home/user/test.txt
```

#### Example 2: Hash Multiple Files in Loop

```bash
for file in ~/fastdevfs_mnt/*; do
    fastdevfs-cli hash "$file"
done
```

#### Example 3: Error — File Not Found

```bash
fastdevfs-cli hash /nonexistent/file.txt
```

**Output**:
```
Error: file '/nonexistent/file.txt' does not exist.
```

#### Example 4: Error — Not a Regular File

```bash
fastdevfs-cli hash ~/fastdevfs_mnt   # Directory, not file
```

**Output**:
```
Error: '/home/user/fastdevfs_mnt' is not a regular file.
```

#### Test Case: HASH_TC1

**Test**: Verify hash consistency

```bash
#!/bin/bash
set -e

# Create a test file
TEST_FILE="/tmp/fastdevfs_test_$$"
echo "consistent content for testing" > "$TEST_FILE"

# Hash it twice
HASH1=$(fastdevfs-cli hash "$TEST_FILE" | awk '{print $1}')
HASH2=$(fastdevfs-cli hash "$TEST_FILE" | awk '{print $1}')

if [ "$HASH1" = "$HASH2" ]; then
    echo "✓ PASS: Hash is consistent"
else
    echo "✗ FAIL: Hash mismatch ($HASH1 vs $HASH2)"
    exit 1
fi

rm "$TEST_FILE"
```

#### Test Case: HASH_TC2

**Test**: Verify different content = different hash

```bash
#!/bin/bash
set -e

# Create two files with different content
FILE1="/tmp/fastdevfs_test_f1_$$"
FILE2="/tmp/fastdevfs_test_f2_$$"

echo "content A" > "$FILE1"
echo "content B" > "$FILE2"

HASH1=$(fastdevfs-cli hash "$FILE1" | awk '{print $1}')
HASH2=$(fastdevfs-cli hash "$FILE2" | awk '{print $1}')

if [ "$HASH1" != "$HASH2" ]; then
    echo "✓ PASS: Different content produces different hash"
else
    echo "✗ FAIL: Same hash for different content"
    exit 1
fi

rm "$FILE1" "$FILE2"
```

---

### 6. DEDUP — Deduplication Operations

**Purpose**: Trigger and manage data deduplication

#### 6.1 DEDUP RUN — Trigger Deduplication Pass

**Syntax**:
```bash
fastdevfs-cli dedup run
```

**Behavior**:
- Sends IPC command to daemon
- Triggers deduplication pass
- Finds and links duplicate data blocks
- Reports results

#### Example 1: Simple Dedup Run

```bash
fastdevfs-cli dedup run
```

**Output**:
```
Deduplication triggered: Pass completed. Linked 42 blocks, saved 18.5 MB
```

#### Example 2: Daemon Not Running

```bash
fastdevfs-cli dedup run
```

**Output**:
```
Error: daemon is not running.
Deduplication requires the daemon to be active.
Start it with: fastdevfs-cli start
```

#### Example 3: Multiple Dedup Runs

```bash
# With verbose logging
fastdevfs-cli -v dedup run
fastdevfs-cli -v dedup run
```

**First Run Output**:
```
[verbose] Sending dedup_run via IPC
Deduplication triggered: Pass 1 completed. Linked 50 blocks, saved 25.0 MB
```

**Second Run Output** (fewer duplicates):
```
[verbose] Sending dedup_run via IPC
Deduplication triggered: Pass 2 completed. Linked 5 blocks, saved 2.5 MB
```

#### Test Case: DEDUP_RUN_TC1

**Test**: Trigger dedup and verify results

```bash
#!/bin/bash
set -e

TEST_MNT="/tmp/fastdevfs_test_dedup_$$"
mkdir -p "$TEST_MNT"

fastdevfs-cli start --daemon --mountpoint "$TEST_MNT"
sleep 2

# Create duplicate content
echo "duplicate data for dedup test" > "$TEST_MNT/file1.txt"
cp "$TEST_MNT/file1.txt" "$TEST_MNT/file2.txt"
cp "$TEST_MNT/file1.txt" "$TEST_MNT/file3.txt"
sleep 1

# Run dedup
DEDUP_OUTPUT=$(fastdevfs-cli dedup run)

if echo "$DEDUP_OUTPUT" | grep -q "Linked"; then
    echo "✓ PASS: Dedup executed and found duplicates"
else
    echo "✗ FAIL: Dedup did not find duplicates"
    fastdevfs-cli stop
    rm -rf "$TEST_MNT"
    exit 1
fi

fastdevfs-cli stop
rm -rf "$TEST_MNT"
```

---

#### 6.2 DEDUP STATS — Display Deduplication Statistics

**Syntax**:
```bash
fastdevfs-cli dedup stats
```

**Output Fields** (when daemon running):

| Field | Description |
|-------|-------------|
| `total_blocks` | Total data blocks in dedup index |
| `unique_blocks` | Unique (deduplicated) blocks |
| `duplicate_blocks` | Duplicate blocks linked |
| `space_saved` | Total storage saved by dedup |
| `dedup_ratio` | Space saved as percentage |
| `policy` | Current dedup policy (user/library/all) |

**Output Format**:
```
Deduplication Statistics
───────────────────────
  total blocks:       1500
  unique blocks:      1200
  duplicate blocks:   300
  space saved:        125.4 MB
  dedup ratio:        8.4%
  policy:             ALL
```

#### Example 1: Show Live Stats (Daemon Running)

```bash
fastdevfs-cli dedup stats
```

#### Example 2: Offline Stats (Daemon Not Running)

```bash
fastdevfs-cli dedup stats
```

**Output**:
```
Deduplication Statistics (offline)
──────────────────────────────────
  Dedup enabled:    yes
  Hash algorithm:   SHA256
  Chunk size:       4096
  (start daemon for live statistics)
```

#### Test Case: DEDUP_STATS_TC1

**Test**: Verify stats reporting

```bash
#!/bin/bash
set -e

TEST_MNT="/tmp/fastdevfs_test_stats_$$"
mkdir -p "$TEST_MNT"

fastdevfs-cli start --daemon --mountpoint "$TEST_MNT"
sleep 2

STATS=$(fastdevfs-cli dedup stats)

# Check key fields
for field in "total_blocks" "unique_blocks" "duplicate_blocks"; do
    if echo "$STATS" | grep -q "$field"; then
        echo "✓ Field '$field' present"
    else
        echo "✗ FAIL: Missing field '$field'"
        fastdevfs-cli stop
        rm -rf "$TEST_MNT"
        exit 1
    fi
done

echo "✓ PASS: Statistics reporting correct"

fastdevfs-cli stop
rm -rf "$TEST_MNT"
```

---

### 7. CONFIG — Configuration Management

**Purpose**: View and modify persistent configuration

#### 7.1 CONFIG GET — Retrieve Configuration

**Syntax**:
```bash
fastdevfs-cli config get [KEY]
```

**Behavior**:
- If `<KEY>` provided: Show that specific value (prefers daemon's live value)
- If `<KEY>` omitted: Show all configuration

#### Example 1: Get All Config

```bash
fastdevfs-cli config get
```

**Output**:
```
FastDevFs Configuration (/home/user/config.ini)
──────────────────────────────────────────────
  mountpoint:       /home/user/fastdevfs_mnt
  dedup_enabled:    1
  hash_algorithm:   SHA256
  chunk_size:       4096
  pid_path:         /tmp/fastdevfs.pid
  socket_path:      /tmp/fastdevfs.sock
  persist_path:     /tmp/fastdevfs.mmap
  dedup_path:       /tmp/fastdevfs_dedup.mmap
```

#### Example 2: Get Specific Key

```bash
fastdevfs-cli config get mountpoint
```

**Output**:
```
mountpoint = /home/user/fastdevfs_mnt
```

#### Example 3: Get Live Value from Running Daemon

```bash
fastdevfs-cli config get dedup_enabled
```

**Output** (if daemon running):
```
dedup_enabled = 1 (live)
```

#### Example 4: Unknown Key

```bash
fastdevfs-cli config get unknown_key
```

**Output**:
```
Error: unknown config key 'unknown_key'.
Valid keys:
  mountpoint
  dedup_enabled
  hash_algorithm
  ...
```

#### Test Case: CONFIG_GET_TC1

**Test**: Verify config retrieval

```bash
#!/bin/bash
set -e

# Ensure fresh config
CONFIG_FILE="/tmp/fastdevfs_test_cfg_$$"
cat > "$CONFIG_FILE" << EOF
mountpoint=/tmp/test_mnt
dedup_enabled=1
hash_algorithm=SHA256
EOF

# Get all config
ALL=$(fastdevfs-cli --config "$CONFIG_FILE" config get)

if echo "$ALL" | grep -q "mountpoint"; then
    echo "✓ PASS: Config retrieval works"
else
    echo "✗ FAIL: Config retrieval failed"
    rm "$CONFIG_FILE"
    exit 1
fi

rm "$CONFIG_FILE"
```

---

#### 7.2 CONFIG SET — Update Configuration

**Syntax**:
```bash
fastdevfs-cli config set <KEY> <VALUE>
```

**Behavior**:
1. Updates config file on disk
2. If daemon running: Propagates change via IPC (live update)
3. If daemon not running: Change takes effect on next start

#### Example 1: Set Mountpoint

```bash
fastdevfs-cli config set mountpoint /mnt/fastdevfs
```

**Output**:
```
mountpoint = /mnt/fastdevfs
```

#### Example 2: Enable Deduplication

```bash
fastdevfs-cli config set dedup_enabled 1
```

**Output**:
```
dedup_enabled = 1
```

#### Example 3: Set Config and Apply to Running Daemon

```bash
fastdevfs-cli config set hash_algorithm SHA256
```

**Output** (if daemon running):
```
hash_algorithm = SHA256
(applied to running daemon)
```

**Output** (if daemon not running):
```
hash_algorithm = SHA256
```

#### Example 4: Invalid Key

```bash
fastdevfs-cli config set invalid_key value
```

**Output**:
```
Error: unknown config key 'invalid_key'.
Valid keys:
  mountpoint
  dedup_enabled
  hash_algorithm
  ...
```

#### Test Case: CONFIG_SET_TC1

**Test**: Set and verify config change

```bash
#!/bin/bash
set -e

CONFIG_FILE="/tmp/fastdevfs_test_set_$$"
cat > "$CONFIG_FILE" << EOF
mountpoint=/tmp/old_mnt
dedup_enabled=0
EOF

# Set new value
fastdevfs-cli --config "$CONFIG_FILE" config set mountpoint /tmp/new_mnt

# Verify it was set
VALUE=$(fastdevfs-cli --config "$CONFIG_FILE" config get mountpoint | awk '{print $3}')

if [ "$VALUE" = "/tmp/new_mnt" ]; then
    echo "✓ PASS: Config set and verified"
else
    echo "✗ FAIL: Config set failed (got $VALUE)"
    rm "$CONFIG_FILE"
    exit 1
fi

rm "$CONFIG_FILE"
```

---

## Complete Testing Workflow

### Scenario 1: Fresh Setup and Basic Operations

```bash
#!/bin/bash
set -e

echo "=== Scenario 1: Fresh Setup ==="

MNT="/tmp/fastdevfs_scenario1_$$"
CONFIG="/tmp/fastdevfs_cfg_$$"

# Step 1: Create config
mkdir -p "$MNT"
cat > "$CONFIG" << EOF
mountpoint=$MNT
dedup_enabled=1
hash_algorithm=SHA256
EOF

echo "✓ Step 1: Config created"

# Step 2: Start daemon
fastdevfs-cli --config "$CONFIG" start --daemon
sleep 2
echo "✓ Step 2: Daemon started"

# Step 3: Check status
fastdevfs-cli --config "$CONFIG" status
echo "✓ Step 3: Status checked"

# Step 4: Create test files
echo "file 1 content" > "$MNT/file1.txt"
echo "file 2 content" > "$MNT/file2.txt"
mkdir -p "$MNT/subdir"
echo "nested file" > "$MNT/subdir/file3.txt"
sleep 1
echo "✓ Step 4: Test files created"

# Step 5: Scan directory
fastdevfs-cli --config "$CONFIG" scan "$MNT" -r
echo "✓ Step 5: Directory scanned"

# Step 6: Hash a file
fastdevfs-cli --config "$CONFIG" hash "$MNT/file1.txt"
echo "✓ Step 6: File hash computed"

# Step 7: Dedup stats
fastdevfs-cli --config "$CONFIG" dedup stats
echo "✓ Step 7: Dedup stats displayed"

# Step 8: Stop daemon
fastdevfs-cli --config "$CONFIG" stop
sleep 1
echo "✓ Step 8: Daemon stopped"

# Cleanup
rm -rf "$MNT" "$CONFIG"
echo "✓ All steps completed successfully!"
```

### Scenario 2: Deduplication Testing

```bash
#!/bin/bash
set -e

echo "=== Scenario 2: Deduplication Testing ==="

MNT="/tmp/fastdevfs_scenario2_$$"
mkdir -p "$MNT"

# Start daemon
fastdevfs-cli start --daemon --mountpoint "$MNT"
sleep 2

# Create files with duplicate content
CONTENT="This is duplicate content for dedup testing"
echo "$CONTENT" > "$MNT/file1.txt"
echo "$CONTENT" > "$MNT/file2.txt"
echo "$CONTENT" > "$MNT/file3.txt"
sleep 1

echo "✓ Created 3 files with identical content"

# Initial stats
echo "--- Before dedup ---"
fastdevfs-cli dedup stats

# Run dedup
echo "--- Running dedup ---"
fastdevfs-cli dedup run

# Stats after dedup
echo "--- After dedup ---"
fastdevfs-cli dedup stats

# Cleanup
fastdevfs-cli stop
rm -rf "$MNT"
echo "✓ Dedup testing complete!"
```

### Scenario 3: Configuration Management

```bash
#!/bin/bash
set -e

echo "=== Scenario 3: Configuration Management ==="

CONFIG="/tmp/fastdevfs_cfg_scenario3_$$"

# Create initial config
cat > "$CONFIG" << EOF
mountpoint=/tmp/mnt1
dedup_enabled=0
hash_algorithm=SHA256
EOF

echo "✓ Step 1: Initial config created"

# View all config
echo "--- Current Config ---"
fastdevfs-cli --config "$CONFIG" config get

# Update individual values
fastdevfs-cli --config "$CONFIG" config set dedup_enabled 1
echo "✓ Step 2: Dedup enabled"

fastdevfs-cli --config "$CONFIG" config set mountpoint /tmp/mnt2
echo "✓ Step 3: Mountpoint changed"

# View updated config
echo "--- Updated Config ---"
fastdevfs-cli --config "$CONFIG" config get

# Cleanup
rm "$CONFIG"
echo "✓ Configuration management complete!"
```

---

## Troubleshooting

### Issue: "Error: no mountpoint specified"

**Cause**: Mountpoint not provided and not in config

**Solution**:
```bash
# Provide mountpoint explicitly
fastdevfs-cli start --mountpoint /tmp/fastdevfs_mnt

# Or set in config file
fastdevfs-cli config set mountpoint /tmp/fastdevfs_mnt
```

---

### Issue: "Error: FastDevFs daemon is already running"

**Cause**: Daemon already started on the same mountpoint

**Solution**:
```bash
# View current status
fastdevfs-cli status

# Stop existing daemon
fastdevfs-cli stop

# Then start new instance
fastdevfs-cli start --daemon --mountpoint /tmp/fastdevfs_mnt
```

---

### Issue: "Error: daemon is not running" (for dedup commands)

**Cause**: Dedup requires running daemon

**Solution**:
```bash
# Start daemon first
fastdevfs-cli start --daemon --mountpoint /tmp/fastdevfs_mnt
sleep 2

# Then run dedup
fastdevfs-cli dedup run
```

---

### Issue: IPC Communication Errors

**Cause**: Daemon and CLI unable to communicate; stale socket

**Solution**:
```bash
# Remove stale socket
rm /tmp/fastdevfs.sock 2>/dev/null || true

# Restart daemon
fastdevfs-cli stop 2>/dev/null || true
fastdevfs-cli start --daemon --mountpoint /tmp/fastdevfs_mnt
```

---

### Issue: Permission Denied When Starting

**Cause**: mounting FUSE requires certain permissions

**Solution**:
```bash
# Use sudo (if FUSE setup requires it)
sudo fastdevfs-cli start --daemon --mountpoint /tmp/fastdevfs_mnt

# Or adjust FUSE permissions in system config
```

---

## Performance Tips

1. **Use Daemon Mode**: Don't use foreground mode in production
   ```bash
   fastdevfs-cli start --daemon --mountpoint /mnt/fastdevfs
   ```

2. **Run Dedup Regularly**: Schedule periodic deduplication
   ```bash
   # Via cron: 0 2 * * * /usr/local/bin/fastdevfs-cli dedup run
   ```

3. **Monitor Stats**: Track dedup effectiveness
   ```bash
   fastdevfs-cli dedup stats
   ```

4. **Verbose Mode for Debugging**: Use `-v` flag to understand what's happening
   ```bash
   fastdevfs-cli -v dedup run
   ```

---

## Quick Reference

| Task | Command |
|------|---------|
| Show help | `fastdevfs-cli --help` |
| Start (daemon) | `fastdevfs-cli start --daemon -m /mnt` |
| Start (foreground) | `fastdevfs-cli start -m /mnt` |
| Stop daemon | `fastdevfs-cli stop` |
| Check status | `fastdevfs-cli status` |
| List files | `fastdevfs-cli scan /mnt -r` |
| Hash file | `fastdevfs-cli hash /mnt/file.txt` |
| Run dedup | `fastdevfs-cli dedup run` |
| Show dedup stats | `fastdevfs-cli dedup stats` |
| View config | `fastdevfs-cli config get` |
| Set config | `fastdevfs-cli config set key value` |
| Use Alt config | `fastdevfs-cli --config /etc/fastdevfs.conf status` |
| Verbose mode | `fastdevfs-cli -v start --daemon -m /mnt` |

---

## Additional Resources

- See [CLI_GUIDE.md](CLI_GUIDE.md) for high-level CLI overview
- See [ALGORITHMS.md](ALGORITHMS.md) for deduplication details
- See [CONCEPTS.md](CONCEPTS.md) for system architecture
- Command help: `fastdevfs-cli <command> --help`

---

**Version**: 1.0  
**Last Updated**: 2026-04-07  
**Status**: In Development
