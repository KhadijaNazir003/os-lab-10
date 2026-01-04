#!/usr/bin/env python3
"""
Benchmark client for cache server
Replays trace file with GET-then-ADD pattern
"""

import socket
import time
import sys
from collections import defaultdict

class CacheBenchmark:
    def __init__(self, host='localhost', port=8080):
        self.host = host
        self.port = port
        self.sock = None
        self.stats = {
            'total_requests': 0,
            'gets': 0,
            'adds': 0,
            'hits': 0,
            'misses': 0,
            'errors': 0
        }
    
    def connect(self):
        """Connect to cache server"""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((self.host, self.port))
        # Receive welcome message
        welcome = self.sock.recv(4096).decode()
        print(f"Connected: {welcome.strip()}")
    
    def send_command(self, method, key, value=None):
        """Send command to server and get response"""
        command = f"Method:{method}\r\nKey:{key}\r\n"
        if value is not None:
            command += f"Value:{value}\r\n"
        command += "\r\n"
        
        self.sock.sendall(command.encode())
        response = self.sock.recv(4096).decode()
        return response
    
    def get(self, key):
        self.stats['total_requests'] += 1
        self.stats['gets'] += 1
        
        response = self.send_command('GET', key)
        
        if response.startswith('OK'):
            self.stats['hits'] += 1
            return True
        else:
            self.stats['misses'] += 1
            return False
    
    def add(self, key, value='dummy'):        
        self.stats['total_requests'] += 1
        self.stats['adds'] += 1
        
        response = self.send_command('ADD', key, value)
        
        if not response.startswith('OK'):
            self.stats['errors'] += 1
            return False
        return True
    
    def run_trace(self, trace_file, max_requests=None):
        """
        Run benchmark from trace file
        Algorithm:
        1. GET(key)
        2. If not hit, ADD(key, dummy_value)
        3. Repeat until trace ends
        """
        print(f"\nRunning benchmark from {trace_file}")
        print(f"Algorithm: GET → If miss then ADD")
        print(f"{'='*60}\n")
        
        start_time = time.time()
        request_count = 0
        
        with open(trace_file, 'r') as f:
            for line in f:
                key = line.strip()
                if not key:
                    continue
                
                # Step 1: GET
                hit = self.get(key)
                
                # Step 2: If miss, ADD
                if not hit:
                    self.add(key, f"data_{key}")
                
                request_count += 1
                
                # Progress reporting
                if request_count % 10000 == 0:
                    elapsed = time.time() - start_time
                    rate = request_count / elapsed if elapsed > 0 else 0
                    hit_ratio = (self.stats['hits'] / self.stats['total_requests'] * 100) if self.stats['total_requests'] > 0 else 0
                    print(f"Progress: {request_count:,} requests | "
                          f"Hit Ratio: {hit_ratio:.2f}% | "
                          f"Rate: {rate:.0f} req/s")
                
                if max_requests and request_count >= max_requests:
                    break
        
        end_time = time.time()
        elapsed = end_time - start_time
        
        self.print_results(elapsed)
    
    def print_results(self, elapsed_time):
        print(f"\n{'='*60}")
        print("BENCHMARK RESULTS")
        print(f"{'='*60}")
        print(f"Total Time:       {elapsed_time:.2f} seconds")
        print(f"Total Requests:   {self.stats['total_requests']:,}")
        print(f"  - GETs:         {self.stats['gets']:,}")
        print(f"  - ADDs:         {self.stats['adds']:,}")
        print(f"Request Rate:     {self.stats['total_requests']/elapsed_time:.0f} req/s")
        print(f"\nCache Performance:")
        print(f"  Hits:           {self.stats['hits']:,}")
        print(f"  Misses:         {self.stats['misses']:,}")
        
        if self.stats['gets'] > 0:
            hit_ratio = (self.stats['hits'] / self.stats['gets']) * 100
            print(f"  HIT RATIO:      {hit_ratio:.4f}%")
        
        if self.stats['errors'] > 0:
            print(f"\nErrors:           {self.stats['errors']:,}")
        
        print(f"{'='*60}\n")
    
    def close(self):
        if self.sock:
            self.sock.close()

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 benchmark_client.py <trace_file> [host] [port] [max_requests]")
        print("\nExample:")
        print("  python3 benchmark_client.py cache_trace.txt")
        print("  python3 benchmark_client.py cache_trace.txt localhost 8080 50000")
        sys.exit(1)
    
    trace_file = sys.argv[1]
    host = sys.argv[2] if len(sys.argv) > 2 else 'localhost'
    port = int(sys.argv[3]) if len(sys.argv) > 3 else 8080
    max_requests = int(sys.argv[4]) if len(sys.argv) > 4 else None
    
    print("="*60)
    print("CACHE BENCHMARK CLIENT")
    print("="*60)
    print(f"Server:      {host}:{port}")
    print(f"Trace File:  {trace_file}")
    if max_requests:
        print(f"Max Requests: {max_requests:,}")
    print("="*60)
    
    benchmark = CacheBenchmark(host, port)
    
    try:
        benchmark.connect()
        benchmark.run_trace(trace_file, max_requests)
    except ConnectionRefusedError:
        print("\nERROR: Could not connect to server.")
        print("Make sure the cache server is running.")
    except FileNotFoundError:
        print(f"\nERROR: Trace file '{trace_file}' not found.")
    except Exception as e:
        print(f"\nERROR: {e}")
        import traceback
        traceback.print_exc()
    finally:
        benchmark.close()

if __name__ == "__main__":
    main()
