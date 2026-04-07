#!/bin/bash
set -e

MNT="/tmp/mnt"
DATA="/tmp/fastdevfs_data"
LOG="/tmp/lib_test.log"

echo "=== FastDevFs Library Folders Dedup Test ==="

# 1. Clean and Start FS
kill -9 $(pgrep fastdevfs) 2>/dev/null || true
sleep 1
fusermount -u "$MNT" 2>/dev/null || true
rm -rf "$DATA" /tmp/fastdevfs.mmap /tmp/fastdevfs_dedup.mmap /tmp/fastdevfs_dedup.sock /tmp/fastdevfs_lib_config.txt
mkdir -p "$MNT" "$DATA"

echo "Starting FS..."
/home/mohit/Desktop/FastDevFs/build/fastdevfs -f "$MNT" > "$LOG" 2>&1 &
sleep 2

# 2. Create canonical library folder
echo "Creating library instance A..."
mkdir -p "$MNT/projA/node_modules/lodash"
echo "function lodash(){}" > "$MNT/projA/node_modules/lodash/index.js"
echo "var version = 1;" > "$MNT/projA/node_modules/lodash/package.json"

# FastDevFs folder settlement is 3 seconds. Wait 4s so the registry gets updated.
echo "Waiting for settlement of library A (4s)..."
sleep 4

# 3. Create duplicate library folder
echo "Creating library instance B..."
mkdir -p "$MNT/projB/node_modules/lodash"
echo "function lodash(){}" > "$MNT/projB/node_modules/lodash/index.js"
echo "var version = 1;" > "$MNT/projB/node_modules/lodash/package.json"

# Wait 4s for settlement
echo "Waiting for settlement of library B (4s)..."
sleep 4

# 4. Verify hard links
echo "Checking disk usage and hard links..."
echo "Node_modules files in projA:"
ls -i "$MNT/projA/node_modules/lodash"

echo "Node_modules files in projB:"
ls -i "$MNT/projB/node_modules/lodash"

echo "Data storage links:"
ls -lia "$DATA"

echo "Tail of log output:"
tail -n 15 "$LOG"

echo "FS process running:"
pgrep fastdevfs

# 5. Cleanup
fusermount -u "$MNT" 2>/dev/null || true
echo "=== Test Complete ==="
