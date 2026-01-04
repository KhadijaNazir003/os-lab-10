# Cache Server Benchmark Guide

## Overview

This benchmark system tests **4 different cache eviction policies** with multi-threaded server architecture:

1. **LRU** (Least Recently Used) - Classic eviction algorithm
2. **FIFO** (First In First Out) - Simple queue-based eviction
3. **SIEVE** - State-of-the-art eviction algorithm (2024)
4. **CLOCK** - Second-chance algorithm with reference bits

## Architecture

### Server Architecture
```
┌─────────────────────────────────────────┐
│         Listener Thread                 │
│    (Epoll, Accept Connections)          │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│      4 Worker Threads (Thread Pool)     │
│  - Process GET/ADD/UPDATE/DELETE        │
│  - Execute eviction policies            │
│  - Thread-safe cache operations         │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│      Shared Cache (100 MB)              │
│  - 2,560 pages × 40 KB                  │
│  - Mutex-protected operations           │
│  - Real-time statistics tracking        │
└─────────────────────────────────────────┘
```

### Benchmark Algorithm
```
For each request in trace:
    1. GET(key)
    2. If MISS:
        ADD(key, dummy_value)
    3. Track hits/misses
    4. Continue until trace ends
```

## Quick Start

### Option 1: Using Synthetic Trace (Recommended for Testing)

```bash
# Build everything
cp Makefile_enhanced Makefile
make clean
make enhanced

# Generate realistic synthetic trace
python3 generate_trace.py 100000 10000 cache_trace.txt

# Run comprehensive benchmark (all 4 policies)
chmod +x run_benchmark.sh
./run_benchmark.sh
```

**This will:**
- Build the enhanced server
- Generate 100K requests with Zipf distribution (realistic)
- Test LRU, FIFO, SIEVE, and CLOCK policies
- Compare hit ratios
- Save results to `benchmark_results.txt`

### Option 2: Using Real Wikipedia Traces

Wikipedia provides real cache access traces from their CDN infrastructure.

#### Download Wikipedia Trace

From your local machine (with internet access):

```bash
# Download from Wikipedia analytics
wget https://analytics.wikimedia.org/published/datasets/caching/2019/text/cache-t-00.gz

# Decompress
gunzip cache-t-00.gz

# Upload to your server
scp cache-t-00 user@server:/path/to/cache_server/
```

**Alternative sources** (if Wikimedia is blocked):
- Use the synthetic trace generator (already included)
- Download from: https://github.com/1a1a11a/libCacheSim (has sample traces)
- Use your own application access logs

#### Trace Format

The trace should be a text file with one key per line:
```
key_00000001
key_00000002
key_00000001
key_00000003
...
```

## Manual Benchmark (Step-by-Step)

### Step 1: Build
```bash
cp Makefile_enhanced Makefile
make clean
make enhanced
```

### Step 2: Generate or Prepare Trace
```bash
# Option A: Generate synthetic trace
python3 generate_trace.py 100000 10000 cache_trace.txt

# Option B: Use your Wikipedia trace
# (already downloaded as cache-t-00)
```

### Step 3: Run Server with Specific Policy

**Terminal 1 - Start LRU Server:**
```bash
./cache_server_enhanced lru 8080
```

**Terminal 2 - Run Benchmark:**
```bash
python3 benchmark_client.py cache_trace.txt localhost 8080 50000
```

Press Ctrl+C in Terminal 1 to stop and see statistics.

### Step 4: Repeat for Other Policies

```bash
# FIFO
./cache_server_enhanced fifo 8081
python3 benchmark_client.py cache_trace.txt localhost 8081 50000

# SIEVE  
./cache_server_enhanced sieve 8082
python3 benchmark_client.py cache_trace.txt localhost 8082 50000

# CLOCK
./cache_server_enhanced clock 8083
python3 benchmark_client.py cache_trace.txt localhost 8083 50000
```

## Understanding the Results

### Sample Output

```
================================================================
BENCHMARK RESULTS
================================================================
Total Time:       45.23 seconds
Total Requests:   50,000
  - GETs:         50,000
  - ADDs:         12,345
Request Rate:     1105 req/s

Cache Performance:
  Hits:           37,655
  Misses:         12,345
  HIT RATIO:      75.3100%
================================================================
```

### Key Metrics

1. **Hit Ratio** - Most important metric
   - Higher = Better caching performance
   - Shows how well the policy predicts future accesses
   - Typical range: 30-80% depending on workload

2. **Request Rate** - Throughput
   - Requests processed per second
   - Affected by network, thread contention, eviction overhead

3. **Evictions** - How many items were kicked out
   - High evictions = cache too small or poor policy
   - Check server logs for this metric

### Expected Results (Synthetic Trace with Zipf Distribution)

Based on typical Zipf workloads (α=1.2):

| Policy | Expected Hit Ratio | Notes |
|--------|-------------------|-------|
| **SIEVE** | 65-75% | Best for most workloads |
| **LRU** | 63-73% | Good general purpose |
| **CLOCK** | 62-72% | Similar to LRU, lower overhead |
| **FIFO** | 55-65% | Simpler but less effective |

**Note:** Real Wikipedia traces may show different patterns!

## Advanced Usage

### Test with Different Cache Sizes

Edit `cache_server_enhanced.h`:
```cpp
constexpr size_t CACHE_SIZE = 200ULL * 1024 * 1024; // 200 MB
```

Then rebuild:
```bash
make clean && make enhanced
```

### Generate Different Workloads

```bash
# Heavy-tailed (more hot items)
python3 generate_trace.py 100000 10000 trace_hot.txt

# Edit generate_trace.py and change:
# zipf_param = 1.5  # More concentrated

# Uniform (all items equally likely)
# zipf_param = 0.0  # Uniform distribution
```

### Benchmark with Custom Traces

```bash
# Your custom trace format:
# one_key_per_line.txt

python3 benchmark_client.py one_key_per_line.txt localhost 8080
```

## Automated Benchmark Script

The `run_benchmark.sh` script automates everything:

```bash
./run_benchmark.sh
```

**What it does:**
1. Builds enhanced server
2. Generates trace (if not exists)
3. Runs all 4 policies on different ports
4. Collects statistics
5. Generates comparison report

**Output files:**
- `benchmark_results.txt` - Summary of all runs
- `benchmark_lru.log` - Detailed LRU results
- `benchmark_fifo.log` - Detailed FIFO results
- `benchmark_sieve.log` - Detailed SIEVE results
- `benchmark_clock.log` - Detailed CLOCK results
- `server_*.log` - Server logs with eviction stats

## Server Statistics

When you stop a server (Ctrl+C), it prints detailed statistics:

```
============================================================
Cache Statistics (SIEVE Policy)
============================================================
Total Requests:       50000
Cache Hits:           37655 (75.31%)
Cache Misses:         12345
Evictions:            2134
Adds:                 12345
Updates:              0
Deletes:              0
============================================================
HIT RATIO:            75.3100%
============================================================
```

## Comparing Policies

### Create Comparison Table

```bash
# After running benchmarks
echo "Policy,Hit Ratio,Evictions"
grep "HIT RATIO:" benchmark_*.log | awk -F: '{print $1,$3}' | sed 's/benchmark_//;s/.log//'
```

### Visualize Results (if you have gnuplot)

```bash
# Extract hit ratios
echo "Policy HitRatio" > results.dat
grep "HIT RATIO:" benchmark_lru.log | awk '{print "LRU", $3}' | sed 's/%//' >> results.dat
grep "HIT RATIO:" benchmark_fifo.log | awk '{print "FIFO", $3}' | sed 's/%//' >> results.dat
grep "HIT RATIO:" benchmark_sieve.log | awk '{print "SIEVE", $3}' | sed 's/%//' >> results.dat
grep "HIT RATIO:" benchmark_clock.log | awk '{print "CLOCK", $3}' | sed 's/%//' >> results.dat

# Plot
gnuplot -e "set term png; set output 'comparison.png'; set title 'Cache Hit Ratio Comparison'; set ylabel 'Hit Ratio (%)'; set style data histogram; plot 'results.dat' using 2:xtic(1) title 'Hit Ratio'"
```

## Troubleshooting

### Server won't start
```bash
# Check if port is in use
lsof -i :8080

# Try different port
./cache_server_enhanced lru 9090
```

### Benchmark client can't connect
```bash
# Verify server is running
ps aux | grep cache_server

# Check server logs
tail -f server_lru.log
```

### Low hit ratio
- Cache may be too small for trace
- Trace may have poor temporal locality
- Try increasing cache size in code

### Build errors
```bash
# Ensure C++17 compiler
g++ --version  # Need 7.0+

# Clean rebuild
make clean
make enhanced
```

## Performance Tips

1. **Use larger cache** for better hit ratios
2. **Adjust worker threads** (NUM_WORKER_THREADS in .h file)
3. **Profile with different traces** to find best policy
4. **Monitor system resources** (CPU, memory)

## Real-World Wikipedia Trace Notes

Wikipedia traces contain:
- **Millions of requests** (very large files)
- **Real user access patterns** from Wikipedia CDN
- **Temporal locality** (recent articles accessed more)
- **Popularity skew** (few articles very popular)

**Recommended settings for Wikipedia traces:**
- Cache: 200-500 MB (edit CACHE_SIZE)
- Max requests: 500K-1M for reasonable runtime
- Expect: 40-60% hit ratio depending on cache size

## Citation

SIEVE algorithm from:
> "SIEVE is Simpler than LRU: an Efficient Turn-Key Eviction Algorithm for Web Caches"
> NSDI 2024

## Files Overview

- `cache_server_enhanced.h` - Server header with all policies
- `cache_server_enhanced.cpp` - Implementation (~1000 lines)
- `main_enhanced.cpp` - Server entry point
- `benchmark_client.py` - Benchmark client
- `generate_trace.py` - Synthetic trace generator
- `run_benchmark.sh` - Automated benchmark script
- `Makefile_enhanced` - Build configuration

## Summary

This benchmark system provides:
- ✅ 4 eviction policies (LRU, FIFO, SIEVE, CLOCK)
- ✅ Multi-threaded server (1 listener + 4 workers)
- ✅ Real-time statistics
- ✅ Automated benchmarking
- ✅ Wikipedia trace support
- ✅ Synthetic trace generation
- ✅ Thread-safe cache operations
- ✅ Detailed performance metrics

Perfect for comparing cache eviction algorithms under realistic workloads!
