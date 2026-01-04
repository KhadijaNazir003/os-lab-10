# Lab 9: Cache Server with Eviction Policy Benchmarking

## Complete Implementation with 4 Eviction Policies

This is an enhanced implementation of Lab 9 that includes:

### ✅ All Core Requirements
- 100 MB cache divided into 40 KB pages (scalable to 2 GB)
- Contiguous page allocation
- TCP server with epoll() I/O multiplexing  
- Text protocol (Method:Key:Value)
- CRUD operations (ADD, UPDATE, GET, DELETE)

### ✅ Enhanced Features
- **Multi-threading**: 1 listener thread + 4 worker threads
- **4 Eviction Policies**: LRU, FIFO, SIEVE, CLOCK
- **Real-time Statistics**: Hit ratio, evictions, throughput
- **Benchmark System**: Automated testing with trace replay
- **Wikipedia Trace Support**: Works with real CDN access patterns

## Quick Start

### 1. Build
```bash
cp Makefile_enhanced Makefile
make clean
make enhanced
```

### 2. Generate Trace (Synthetic)
```bash
python3 generate_trace.py 50000 5000 cache_trace.txt
```

This creates 50K requests with realistic Zipf distribution (power-law popularity).

### 3. Run Benchmark (All Policies)
```bash
chmod +x run_benchmark.sh
./run_benchmark.sh
```

This will:
- Test LRU, FIFO, SIEVE, and CLOCK policies
- Run each on a different port (8080-8083)
- Generate comparison report
- Save results to `benchmark_results.txt`

## Manual Testing

### Test Single Policy

**Terminal 1** - Start Server:
```bash
./cache_server_enhanced lru 8080
```

**Terminal 2** - Run Benchmark:
```bash
python3 benchmark_client.py cache_trace.txt localhost 8080 25000
```

Press Ctrl+C in Terminal 1 to see statistics.

### Test All Policies

```bash
# LRU
./cache_server_enhanced lru 8080

# FIFO
./cache_server_enhanced fifo 8081

# SIEVE (state-of-the-art, 2024)
./cache_server_enhanced sieve 8082

# CLOCK (second chance)
./cache_server_enhanced clock 8083
```

## Using Real Wikipedia Traces

### On a Machine with Internet:
```bash
chmod +x download_wikipedia_trace.sh
./download_wikipedia_trace.sh
```

This downloads `cache-t-00` from Wikipedia's CDN analytics.

### Transfer to Server:
```bash
scp cache-t-00 user@server:/path/to/cache_server/
```

### Run Benchmark:
```bash
python3 benchmark_client.py cache-t-00 localhost 8080 100000
```

**Note**: Wikipedia traces are large (millions of requests). Use max_requests parameter to limit runtime.

## Understanding Results

### Sample Output
```
================================================================
BENCHMARK RESULTS
================================================================
Total Time:       25.45 seconds
Total Requests:   25,000
  - GETs:         25,000
  - ADDs:         6,234
Request Rate:     982 req/s

Cache Performance:
  Hits:           18,766
  Misses:         6,234
  HIT RATIO:      75.0640%
================================================================
```

### Server Statistics (on Ctrl+C)
```
============================================================
Cache Statistics (LRU Policy)
============================================================
Total Requests:       25000
Cache Hits:           18766 (75.06%)
Cache Misses:         6234
Evictions:            1234
Adds:                 6234
Updates:              0
Deletes:              0
============================================================
HIT RATIO:            75.0640%
============================================================
```

## Expected Performance

With synthetic Zipf trace (α=1.2, 50K requests, 5K unique keys):

| Policy | Expected Hit Ratio | Best For |
|--------|-------------------|----------|
| **SIEVE** | 70-78% | General purpose, modern |
| **LRU** | 68-76% | Strong temporal locality |
| **CLOCK** | 67-75% | Low overhead, balanced |
| **FIFO** | 60-70% | Simple, predictable |

**Real Wikipedia traces** may show different patterns based on:
- Cache size vs working set
- Temporal locality in access pattern
- Popularity distribution

## File Structure

### Core Implementation
- `cache_server_enhanced.h` - Server with all 4 policies
- `cache_server_enhanced.cpp` - Implementation (~1000 lines)
- `main_enhanced.cpp` - Server entry point

### Benchmarking Tools  
- `benchmark_client.py` - Trace replay client
- `generate_trace.py` - Synthetic trace generator
- `run_benchmark.sh` - Automated multi-policy testing

### Documentation
- `BENCHMARK_GUIDE.md` - Comprehensive guide
- `README_BENCHMARK.md` - This file
- `download_wikipedia_trace.sh` - Wikipedia trace helper

### Configuration
- `Makefile_enhanced` - Build system

## Architecture

```
┌──────────────────────────────────────┐
│     Main Thread (Listener)           │
│  - Epoll event loop                  │
│  - Accept new connections            │
│  - Distribute to workers             │
└──────────┬───────────────────────────┘
           │
           ▼
┌──────────────────────────────────────┐
│   Worker Thread Pool (4 threads)     │
│  - Parse commands                    │
│  - Execute cache operations          │
│  - Apply eviction policy             │
│  - Return responses                  │
└──────────┬───────────────────────────┘
           │
           ▼
┌──────────────────────────────────────┐
│      Shared Cache (Thread-Safe)      │
│  - 2,560 pages × 40 KB               │
│  - Mutex-protected                   │
│  - Real-time statistics              │
└──────────────────────────────────────┘
```

## Eviction Policy Details

### LRU (Least Recently Used)
- **Data Structure**: Doubly-linked list + hash map
- **Time Complexity**: O(1) access, O(k) eviction
- **Space Overhead**: O(n) for list
- **Best For**: Strong temporal locality
- **Implementation**: Move-to-front on access

### FIFO (First In First Out)
- **Data Structure**: Queue with insertion order
- **Time Complexity**: O(1) access, O(k log k) eviction
- **Space Overhead**: O(1)
- **Best For**: Predictable, simple workloads
- **Implementation**: Track insertion order, evict oldest

### SIEVE (State-of-the-Art)
- **Data Structure**: Circular list with visited bits
- **Time Complexity**: O(1) amortized
- **Space Overhead**: O(n) for list + bits
- **Best For**: General purpose, modern workloads
- **Implementation**: Hand pointer, clear visited bits
- **Paper**: NSDI 2024

### CLOCK (Second Chance)
- **Data Structure**: Circular buffer with reference bits
- **Time Complexity**: O(1) amortized  
- **Space Overhead**: O(n) for circular list
- **Best For**: Balanced performance/overhead
- **Implementation**: Clock hand, reference bits

## Advanced Configuration

### Adjust Cache Size

Edit `cache_server_enhanced.h`:
```cpp
constexpr size_t CACHE_SIZE = 200ULL * 1024 * 1024; // 200 MB
```

### Adjust Worker Threads

Edit `cache_server_enhanced.h`:
```cpp
constexpr int NUM_WORKER_THREADS = 8; // 8 threads
```

### Generate Different Workloads

Edit `generate_trace.py`:
```python
zipf_param = 1.5  # More concentrated (hotter)
zipf_param = 0.8  # More uniform (colder)
```

## Makefile Targets

```bash
make enhanced        # Build enhanced server
make clean          # Clean build artifacts
make trace          # Generate synthetic trace
make run-lru        # Run with LRU policy
make run-fifo       # Run with FIFO policy
make run-sieve      # Run with SIEVE policy
make run-clock      # Run with CLOCK policy
make benchmark      # Full benchmark suite
make test           # Quick test with LRU
make help           # Show all targets
```

## Troubleshooting

### Build Issues
```bash
# Ensure C++17 support
g++ --version  # Need 7.0+

# Clean rebuild
make clean && make enhanced
```

### Server Won't Start
```bash
# Check port availability
lsof -i :8080

# Try different port
./cache_server_enhanced lru 9090
```

### Connection Issues
```bash
# Verify server running
ps aux | grep cache_server

# Check logs
cat server_lru.log
```

### Low Hit Ratio
- Cache may be too small for working set
- Try larger cache size
- Check trace characteristics
- Verify policy is appropriate for workload

## Performance Tips

1. **Match cache size to working set**
   - Monitor evictions
   - Increase if evictions are high

2. **Choose policy based on workload**
   - SIEVE: Best general purpose
   - LRU: Strong recency patterns
   - FIFO: Simple, sequential access
   - CLOCK: Low overhead

3. **Adjust worker threads**
   - More threads for high concurrency
   - Fewer threads for single-client

4. **Profile with real traces**
   - Synthetic traces are approximations
   - Real workloads show true performance

## Benchmark Algorithm

```
For each request in trace file:
    1. Send GET(key) to server
    2. If response is MISS:
        Send ADD(key, dummy_value)
    3. Track hits and misses
    4. Continue until end of trace
    
Final: Calculate hit_ratio = hits / total_gets
```

This simulates a cache-aside pattern common in web applications.

## Citation

SIEVE algorithm from:
> Yazhuo Zhang, Juncheng Yang, Yao Yue, Ymir Vigfusson, K. V. Rashmi
> "SIEVE is Simpler than LRU: an Efficient Turn-Key Eviction Algorithm for Web Caches"
> USENIX NSDI 2024

## Original Lab 9 Files

The original single-threaded, LRU-only implementation is also included:
- `cache_server.h`
- `cache_server.cpp`
- `main.cpp`

To build original:
```bash
make basic
./cache_server 8080
```

## Summary

This enhanced implementation provides:
- ✅ 4 eviction policies (LRU, FIFO, SIEVE, CLOCK)
- ✅ Multi-threaded architecture (1+4 threads)
- ✅ Thread-safe cache operations
- ✅ Real-time performance metrics
- ✅ Automated benchmarking system
- ✅ Wikipedia trace support
- ✅ Synthetic trace generation
- ✅ Comprehensive documentation

Perfect for comparing cache eviction algorithms under realistic workloads!

## Next Steps

1. ✅ Build: `make enhanced`
2. ✅ Generate trace: `python3 generate_trace.py`
3. ✅ Run benchmark: `./run_benchmark.sh`
4. ✅ Analyze results: `cat benchmark_results.txt`
5. ✅ Download Wikipedia trace (optional)
6. ✅ Compare policies with real data

For detailed instructions, see `BENCHMARK_GUIDE.md`.
