#!/bin/bash

# Wikipedia Cache Trace Download Helper
# Run this on a machine with internet access, then transfer to your server

echo "=========================================="
echo "Wikipedia Cache Trace Download Helper"
echo "=========================================="

TRACE_URL="https://analytics.wikimedia.org/published/datasets/caching/2019/text/cache-t-00.gz"
TRACE_FILE="cache-t-00.gz"
TRACE_EXTRACTED="cache-t-00"

echo ""
echo "This script will:"
echo "  1. Download Wikipedia cache trace"
echo "  2. Decompress it"
echo "  3. Show sample data"
echo "  4. Provide transfer instructions"
echo ""

# Download
echo "[1/4] Downloading trace from Wikipedia..."
echo "URL: $TRACE_URL"

if command -v wget &> /dev/null; then
    wget -c "$TRACE_URL" -O "$TRACE_FILE"
elif command -v curl &> /dev/null; then
    curl -L "$TRACE_URL" -o "$TRACE_FILE"
else
    echo "ERROR: Neither wget nor curl found"
    echo "Please install wget or curl and try again"
    exit 1
fi

if [ ! -f "$TRACE_FILE" ]; then
    echo "ERROR: Download failed"
    exit 1
fi

echo "✓ Downloaded: $TRACE_FILE ($(du -h $TRACE_FILE | cut -f1))"

# Decompress
echo ""
echo "[2/4] Decompressing..."
gunzip -k "$TRACE_FILE" 2>/dev/null || gunzip "$TRACE_FILE"

if [ ! -f "$TRACE_EXTRACTED" ]; then
    # Try without -k flag
    gunzip "$TRACE_FILE"
fi

if [ ! -f "$TRACE_EXTRACTED" ]; then
    echo "ERROR: Decompression failed"
    exit 1
fi

echo "✓ Extracted: $TRACE_EXTRACTED ($(du -h $TRACE_EXTRACTED | cut -f1))"

# Show sample
echo ""
echo "[3/4] Sample data (first 10 lines):"
echo "----------------------------------------"
head -10 "$TRACE_EXTRACTED"
echo "----------------------------------------"

# Stats
echo ""
echo "[4/4] Trace statistics:"
TOTAL_LINES=$(wc -l < "$TRACE_EXTRACTED")
UNIQUE_KEYS=$(sort -u "$TRACE_EXTRACTED" | wc -l)

echo "  Total requests: $(printf "%'d" $TOTAL_LINES)"
echo "  Unique keys:    $(printf "%'d" $UNIQUE_KEYS)"

# Transfer instructions
echo ""
echo "=========================================="
echo "Next Steps:"
echo "=========================================="
echo ""
echo "Transfer the trace file to your server:"
echo ""
echo "  scp $TRACE_EXTRACTED user@your-server:/path/to/cache_server/"
echo ""
echo "Or if you prefer a subset (for faster testing):"
echo ""
echo "  # Create a 100K request sample"
echo "  head -100000 $TRACE_EXTRACTED > cache_trace_sample.txt"
echo "  scp cache_trace_sample.txt user@your-server:/path/to/cache_server/"
echo ""
echo "Then on your server, run:"
echo ""
echo "  python3 benchmark_client.py $TRACE_EXTRACTED localhost 8080"
echo ""
echo "=========================================="
echo "Download complete!"
echo "=========================================="
