# FastDevFS Mount Operations Guide

## Overview

This guide covers mounting and managing the FastDevFS filesystem using the daemon and CLI.

---

## Prerequisites

### System Requirements

1. **FUSE3 Library**: Required for filesystem mounting
   ```bash
   # Ubuntu/Debian
   sudo apt install fuse3 libfuse3-dev
   
   # Red Hat/CentOS
   sudo yum install fuse3 fuse3-dev
   
   # macOS
   brew install osxfuse
   ```

2. **User Permissions**: Add user to FUSE group (if required on your system)
   ```bash
   sudo usermod -a -G fuse $(whoami)
   # Then log out and back in
   ```

3. **Verify FUSE Installation**
   ```bash
   fusermount3 --version
   ```

---

## Building

### 1. Build from Source

```bash
cd /path/to/FastDevFs
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

### 2. Verify Binaries

```bash
ls -lh build/fastdevfs build/fastdevfs-cli
```

**Expected Output**:
```
-rwxr-xr-x ... fastdevfs (daemon)
-rwxr-xr-x ... fastdevfs-cli (CLI tool)
```

---

## Starting the Daemon

### Create Mountpoint

Before starting, ensure your mountpoint directory exists:

```bash
# Create a mountpoint
mkdir -p ~/fastdevfs_mnt

# Or in a system location (may require sudo)
sudo mkdir -p /mnt/fastdevfs
sudo chmod 755 /mnt/fastdevfs
```

### Mode 1: Background (Daemon Mode) — **Recommended for Production**

Start the filesystem in the background and leave it mounted:

```bash
cd /path/to/FastDevFs/build
./fastdevfs-cli start --daemon --mountpoint ~/fastdevfs_mnt
```

**Expected Output**:
```
[Config] Loaded config from ./config.ini
✔  Daemon started successfully in background
  Use 'fastdevfs-cli status' to check status
```

**Verify Mount**:
```bash
mount | grep fastdevfs
# Output: /path/to/fastdevfs on ~/fastdevfs_mnt type fuse.fastdevfs (rw,...)

ls -la ~/fastdevfs_mnt
# Should show directory listing
```

### Mode 2: Foreground — **For Development/Debugging**

Start the filesystem in foreground mode (blocks terminal, shows all logs):

```bash
cd /path/to/FastDevFs/build
./fastdevfs-cli start --mountpoint ~/fastdevfs_mnt
```

**Expected Output**:
```
[Config] Loaded config from ./config.ini
⚙  Mounting on: ~/fastdevfs_mnt
[Config] Loaded config from ./config.ini
[DedupIndex] Loaded existing dedup store
[Dedup] Policy: ALL (default)
[IPC] Server started
[Dedup] Worker started
FastDevFs mounted at ~/fastdevfs_mnt
```

**To Stop**: Press `Ctrl+C` in the terminal

---

## Mount Troubleshooting

### Error: "Mountpoint does not exist"

**Cause**: Directory not found

**Solution**:
```bash
mkdir -p ~/fastdevfs_mnt
```

---

### Error: "Mountpoint is not a directory"

**Cause**: Path points to a file instead of directory

**Solution**:
```bash
# Remove the file
rm /path/to/mountpoint

# Create directory instead
mkdir /path/to/mountpoint
```

---

### Error: "Permission denied"

**Cause**: Insufficient permissions on mountpoint

**Solution**:
```bash
# Ensure your user owns and can access the directory
chmod u+rwx ~/fastdevfs_mnt
chown $USER:$USER ~/fastdevfs_mnt

# Or use sudo if system-wide mountpoint
sudo chmod 755 /mnt/fastdevfs
sudo chown $USER:$USER /mnt/fastdevfs
```

---

### Error: "fuse_session_mount() failed"

**Cause**: Multiple reasons — FUSE not installed, already mounted, or insufficient permissions

**Solution**:
```bash
# 1. Verify FUSE is installed
fusermount3 --version

# 2. Check if already mounted
mount | grep fastdevfs

# 3. Verify mountpoint permissions
ls -ld ~/fastdevfs_mnt

# 4. Check if directory is empty
find ~/fastdevfs_mnt -type f

# 5. Try with verbose mode to see detailed errors
./fastdevfs-cli -v start --mountpoint ~/fastdevfs_mnt
```

---

### Error: "Already running"

**Cause**: Daemon already mounted on this path

**Solution**:
```bash
# Stop the running daemon
./fastdevfs-cli stop

# Or if using different mountpoint, specify it explicitly
./fastdevfs-cli start --daemon --mountpoint /tmp/other_mnt
```

---

## Checking Power Daemon Status

### Check Daemon Health

```bash
cd /path/to/FastDevFs/build
./fastdevfs-cli status
```

**Output (Running)**:
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

**Output (Not Running)**:
```
FastDevFs daemon is NOT running.
```

---

## Running Deduplication

### Trigger Dedup Pass

```bash
./fastdevfs-cli dedup run
```

**Output**:
```
Deduplication triggered: Pass completed. Linked 42 blocks, saved 18.5 MB
```

### View Dedup Stats

```bash
./fastdevfs-cli dedup stats
```

**Output**:
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

---

## Stopping the Daemon

### Graceful Shutdown

```bash
./fastdevfs-cli stop
```

**Expected Output**:
```
✔  FastDevFS daemon is shutting down
FastDevFs unmounting...
```

**Verify Unmounted**:
```bash
mount | grep fastdevfs
# Should return nothing
```

---

## Complete Workflow Example

### 1. Setup

```bash
cd ~/FastDevFs/build

# Create mountpoint
mkdir -p ~/my_filesystem

# Start daemon in background
./fastdevfs-cli start --daemon --mountpoint ~/my_filesystem
sleep 2

# Verify it's running
./fastdevfs-cli status
```

### 2. Use Filesystem

```bash
# Create files in mounted filesystem
echo "Hello, FastDevFS!" > ~/my_filesystem/test.txt
cp ~/Downloads/document.pdf ~/my_filesystem/

# List contents
ls -la ~/my_filesystem/

# Compute hash of file
./fastdevfs-cli hash ~/my_filesystem/test.txt

# Scan directory with dedup info
./fastdevfs-cli scan ~/my_filesystem -r
```

### 3. Run Deduplication

```bash
# Copy file multiple times to create duplicates
cp ~/my_filesystem/test.txt ~/my_filesystem/test_copy1.txt
cp ~/my_filesystem/test.txt ~/my_filesystem/test_copy2.txt
sleep 1

# Check dedup stats before running dedup
./fastdevfs-cli dedup stats

# Trigger dedup pass
./fastdevfs-cli dedup run

# Check stats after (should show saved space)
./fastdevfs-cli dedup stats
```

### 4. Shutdown

```bash
# Stop daemon gracefully
./fastdevfs-cli stop
sleep 1

# Verify unmounted
mount | grep fastdevfs
# Should return nothing (successful unmount)
```

---

## Configuration

### Using Custom Config File

Create `my_config.ini`:
```ini
mountpoint=/home/user/my_filesystem
dedup_enabled=1
hash_algorithm=SHA256
chunk_size=4096
```

Then use it:
```bash
./fastdevfs-cli --config my_config.ini start --daemon
```

### View Current Config

```bash
./fastdevfs-cli config get
```

### Update Config

```bash
./fastdevfs-cli config set mountpoint /tmp/new_mount
./fastdevfs-cli config set dedup_enabled 1
```

---

## Advanced Topics

### Mounting with FUSE Options

```bash
# Mount with specific FUSE options
./fastdevfs /mnt/fs -o allow_other,default_permissions

# Or via CLI with foreground mode
./fastdevfs-cli start -m /mnt/fs  # Then use Ctrl+C to stop
```

### Monitoring Mount Activity

```bash
# In one terminal, start daemon
./fastdevfs-cli start --mountpoint ~/my_fs

# In another terminal, monitor activity
watch -n 1 'ls -la ~/my_fs | head -20'

# Or check system logs
dmesg | tail -20  # Follow FUSE kernel messages
```

### Unmounting Without CLI

If daemon is stuck or CLI is unresponsive:

```bash
# Manual unmount (safe on empty mount)
fusermount3 -u ~/my_filesystem

# Force unmount (use with caution)
fusermount3 -uz ~/my_filesystem

# Verify unmounted
mount | grep fastdevfs
```

---

## Performance Tuning

### Disable Dedup (if not needed)

```bash
./fastdevfs-cli config set dedup_enabled 0
./fastdevfs-cli stop
./fastdevfs-cli start --daemon --mountpoint ~/my_fs
```

### Run Dedup Periodically

Create a cron job:
```bash
# Edit crontab
crontab -e

# Add line to run dedup every hour
0 * * * * /path/to/fastdevfs-cli dedup run >> /tmp/fastdevfs_dedup.log 2>&1
```

---

## Common Commands Quick Reference

| Task | Command |
|------|---------|
| Create mountpoint | `mkdir -p ~/fs` |
| Start in background | `./fastdevfs-cli start --daemon -m ~/fs` |
| Start in foreground | `./fastdevfs-cli start -m ~/fs` |
| Check status | `./fastdevfs-cli status` |
| Stop daemon | `./fastdevfs-cli stop` |
| List files | `ls -la ~/fs` |
| Hash file | `./fastdevfs-cli hash ~/fs/file.txt` |
| Run dedup | `./fastdevfs-cli dedup run` |
| Check dedup stats | `./fastdevfs-cli dedup stats` |
| Verbose mode | `./fastdevfs-cli -v start -m ~/fs` |
| Custom config | `./fastdevfs-cli --config cfg.ini start --daemon` |
| View config | `./fastdevfs-cli config get` |
| Update config | `./fastdevfs-cli config set key value` |

---

## Troubleshooting Checklist

When mount fails, check in this order:

- [ ] Mountpoint directory exists: `ls -d ~/fs`
- [ ] Mountpoint is a directory: `file ~/fs`
- [ ] Mountpoint is readable/writable: `[ -r ~/fs -a -w ~/fs ] && echo OK`
- [ ] FUSE is installed: `fusermount3 --version`
- [ ] Not already mounted: `mount | grep fastdevfs`
- [ ] Daemon binary exists: `ls -l ./fastdevfs`
- [ ] CLI binary exists: `ls -l ./fastdevfs-cli`
- [ ] Verbose mode for details: `./fastdevfs-cli -v start -m ~/fs`

---

## Additional Resources

- **CLI Guide**: See [CLI_GUIDE.md](CLI_GUIDE.md) for detailed command reference
- **CLI Tutorial**: See [CLI_TUTORIAL.md](CLI_TUTORIAL.md) for examples and test cases
- **Architecture**: See [CONCEPTS.md](CONCEPTS.md) for system design
- **Dedup Details**: See [ALGORITHMS.md](ALGORITHMS.md) for deduplication internals
- **Test Guide**: See [TESTS.md](TESTS.md) for running test suite

---

**Version**: 1.0  
**Last Updated**: 2026-04-08  
**Status**: In Development
