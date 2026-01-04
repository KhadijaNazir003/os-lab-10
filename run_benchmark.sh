#!/bin/bash

# Comprehensive Cache Server Benchmark Script
# Tests LRU, FIFO, SIEVE, and CLOCK eviction policies

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

TRACE_FILE="cache_trace.txt"
NUM_REQUESTS=100000
NUM_UNIQUE_KEYS=10000
MAX_BENCH_REQUESTS=50000

echo -e "${BLUE}================================================================${NC}"
echo -e "${BLUE}    CACHE SERVER BENCHMARK - EVICTION POLICY COMPARISON${NC}"
echo -e "${BLUE}================================================================${NC}"

# Step 1: Build
echo -e "\n${YELLOW}[1/5] Building enhanced cache server...${NC}"
make clean > /dev/null 2>&1 || true
make enhanced

if [ ! -f "./cache_server_enhanced" ]; then
    echo -e "${RED}ERROR: Build failed${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Build successful${NC}"

# Step 2: Generate trace if needed
if [ ! -f "$TRACE_FILE" ]; then
    echo -e "\n${YELLOW}[2/5] Generating synthetic trace file...${NC}"
    python3 generate_trace.py $NUM_REQUESTS $NUM_UNIQUE_KEYS $TRACE_FILE
    echo -e "${GREEN}✓ Trace generated: $TRACE_FILE${NC}"
else
    echo -e "\n${YELLOW}[2/5] Using existing trace file: $TRACE_FILE${NC}"
    wc -l $TRACE_FILE
fi

# Step 3: Make benchmark client executable
echo -e "\n${YELLOW}[3/5] Preparing benchmark client...${NC}"
chmod +x benchmark_client.py
echo -e "${GREEN}✓ Benchmark client ready${NC}"

# Results file
RESULTS_FILE="benchmark_results.txt"
echo "Cache Server Benchmark Results" > $RESULTS_FILE
echo "===============================" >> $RESULTS_FILE
echo "Date: $(date)" >> $RESULTS_FILE
echo "Trace: $TRACE_FILE" >> $RESULTS_FILE
echo "" >> $RESULTS_FILE

# Policies to test
POLICIES=("lru" "fifo" "sieve" "clock")
BASE_PORT=8080

echo -e "\n${YELLOW}[4/5] Running benchmarks for all policies...${NC}"

for i in "${!POLICIES[@]}"; do
    POLICY="${POLICIES[$i]}"
    PORT=$((BASE_PORT + i))
    
    echo -e "\n${BLUE}================================================================${NC}"
    echo -e "${BLUE}Testing Policy: ${POLICY^^} on port $PORT${NC}"
    echo -e "${BLUE}================================================================${NC}"
    
    # Start server
    echo -e "${YELLOW}Starting ${POLICY^^} server...${NC}"
    ./cache_server_enhanced $POLICY $PORT > server_${POLICY}.log 2>&1 &
    SERVER_PID=$!
    echo $SERVER_PID > server_${POLICY}.pid
    
    # Wait for server to start
    sleep 2
    
    # Check if server is running
    if ! ps -p $SERVER_PID > /dev/null; then
        echo -e "${RED}ERROR: Server failed to start${NC}"
        cat server_${POLICY}.log
        continue
    fi
    
    echo -e "${GREEN}✓ ${POLICY^^} server started (PID: $SERVER_PID)${NC}"
    
    # Run benchmark
    echo -e "${YELLOW}Running benchmark...${NC}"
    python3 benchmark_client.py $TRACE_FILE localhost $PORT $MAX_BENCH_REQUESTS | tee benchmark_${POLICY}.log
    
    # Extract hit ratio
    HIT_RATIO=$(grep "HIT RATIO:" benchmark_${POLICY}.log | awk '{print $3}')
    
    echo "" >> $RESULTS_FILE
    echo "Policy: ${POLICY^^}" >> $RESULTS_FILE
    echo "Port: $PORT" >> $RESULTS_FILE
    echo "Hit Ratio: $HIT_RATIO" >> $RESULTS_FILE
    grep -A 20 "BENCHMARK RESULTS" benchmark_${POLICY}.log >> $RESULTS_FILE
    
    # Stop server
    echo -e "${YELLOW}Stopping ${POLICY^^} server...${NC}"
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    rm -f server_${POLICY}.pid
    
    sleep 1
    echo -e "${GREEN}✓ ${POLICY^^} benchmark complete${NC}"
done

# Step 5: Summary
echo -e "\n${YELLOW}[5/5] Generating summary...${NC}"

echo -e "\n${BLUE}================================================================${NC}"
echo -e "${BLUE}                    BENCHMARK SUMMARY${NC}"
echo -e "${BLUE}================================================================${NC}"

echo -e "\n${YELLOW}Hit Ratio Comparison:${NC}"
echo "----------------------------------------"

for POLICY in "${POLICIES[@]}"; do
    if [ -f "benchmark_${POLICY}.log" ]; then
        HIT_RATIO=$(grep "HIT RATIO:" benchmark_${POLICY}.log | awk '{print $3}' || echo "N/A")
        printf "%-10s %s\n" "${POLICY^^}:" "$HIT_RATIO"
    fi
done

echo "----------------------------------------"

# Find best policy
echo -e "\n${YELLOW}Detailed Results:${NC}"
cat $RESULTS_FILE

echo -e "\n${GREEN}All benchmarks complete!${NC}"
echo -e "${GREEN}Results saved to: $RESULTS_FILE${NC}"
echo -e "\n${YELLOW}Individual logs:${NC}"
ls -lh benchmark_*.log server_*.log 2>/dev/null || true

echo -e "\n${BLUE}================================================================${NC}"
echo -e "${BLUE}To view detailed results for a specific policy:${NC}"
echo -e "${BLUE}  cat benchmark_lru.log${NC}"
echo -e "${BLUE}  cat server_lru.log${NC}"
echo -e "${BLUE}================================================================${NC}"
