#!/usr/bin/env python3
"""
Generate synthetic cache trace with realistic access patterns similar to Wikipedia
Uses Zipf distribution for popularity and temporal locality
"""

import random
import sys
from collections import defaultdict

def generate_trace(num_requests=100000, num_unique_keys=10000, output_file="cache_trace.txt"):
    """
    Generate cache trace with Zipf distribution (power law)
    - Few items are very popular (hot)
    - Many items are rarely accessed (cold)
    - Temporal locality: recently accessed items more likely to be accessed again
    """
    
    # Zipf parameter: higher = more skewed (more concentrated on popular items)
    zipf_param = 1.2
    
    # Generate key popularity using Zipf distribution
    keys = [f"key_{i:08d}" for i in range(num_unique_keys)]
    
    # Zipf weights
    weights = [1.0 / (i + 1) ** zipf_param for i in range(num_unique_keys)]
    total_weight = sum(weights)
    weights = [w / total_weight for w in weights]
    
    # Recent access cache for temporal locality (20% of requests from recent 100)
    recent_cache = []
    recent_size = 100
    
    requests = []
    
    print(f"Generating {num_requests:,} requests with {num_unique_keys:,} unique keys...")
    print(f"Using Zipf distribution (α={zipf_param}) for realistic workload")
    
    for i in range(num_requests):
        if i % 10000 == 0:
            print(f"  Progress: {i:,} / {num_requests:,} ({100*i/num_requests:.1f}%)")
        
        # 20% temporal locality: access recently accessed keys
        if recent_cache and random.random() < 0.2:
            key = random.choice(recent_cache)
        else:
            # 80% from Zipf distribution
            key = random.choices(keys, weights=weights)[0]
        
        requests.append(key)
        
        # Update recent cache
        if key in recent_cache:
            recent_cache.remove(key)
        recent_cache.append(key)
        if len(recent_cache) > recent_size:
            recent_cache.pop(0)
    
    # Write trace file
    print(f"\nWriting trace to {output_file}...")
    with open(output_file, 'w') as f:
        for key in requests:
            f.write(f"{key}\n")
    
    # Statistics
    unique_accessed = len(set(requests))
    access_counts = defaultdict(int)
    for key in requests:
        access_counts[key] += 1
    
    sorted_counts = sorted(access_counts.values(), reverse=True)
    
    print(f"\nTrace Statistics:")
    print(f"  Total requests: {num_requests:,}")
    print(f"  Unique keys accessed: {unique_accessed:,} / {num_unique_keys:,} ({100*unique_accessed/num_unique_keys:.1f}%)")
    print(f"  Most popular key accessed: {sorted_counts[0]:,} times")
    print(f"  Top 1% keys account for: {100*sum(sorted_counts[:num_unique_keys//100])/num_requests:.1f}% of requests")
    print(f"  Top 10% keys account for: {100*sum(sorted_counts[:num_unique_keys//10])/num_requests:.1f}% of requests")
    print(f"\nTrace file ready: {output_file}")

if __name__ == "__main__":
    num_requests = 100000  # 100K requests
    num_unique_keys = 10000  # 10K unique items
    output_file = "cache_trace.txt"
    
    if len(sys.argv) > 1:
        num_requests = int(sys.argv[1])
    if len(sys.argv) > 2:
        num_unique_keys = int(sys.argv[2])
    if len(sys.argv) > 3:
        output_file = sys.argv[3]
    
    generate_trace(num_requests, num_unique_keys, output_file)
