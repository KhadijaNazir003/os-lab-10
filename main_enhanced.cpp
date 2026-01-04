#include "cache_server_enhanced.h"
#include <iostream>
#include <csignal>
#include <cstring>

CacheServerEnhanced* g_server = nullptr;

void signalHandler(int signum) {
    std::cout << "\nInterrupt signal (" << signum << ") received." << std::endl;
    if (g_server) {
        g_server->printStats();
        g_server->stop();
        delete g_server;
        g_server = nullptr;
    }
    exit(0);
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [policy] [port]\n\n";
    std::cout << "Eviction Policies:\n";
    std::cout << "  lru    - Least Recently Used (default)\n";
    std::cout << "  fifo   - First In First Out\n";
    std::cout << "  sieve  - SIEVE (state-of-the-art)\n";
    std::cout << "  clock  - Clock (Second Chance)\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program << " lru 8080\n";
    std::cout << "  " << program << " fifo 8081\n";
    std::cout << "  " << program << " sieve 8082\n";
    std::cout << "  " << program << " clock 8083\n";
}

int main(int argc, char* argv[]) {
    EvictionPolicy policy = EvictionPolicy::LRU;
    int port = 8080;
    
    if (argc > 1) {
        std::string policy_str = argv[1];
        
        if (policy_str == "lru") {
            policy = EvictionPolicy::LRU;
        } else if (policy_str == "fifo") {
            policy = EvictionPolicy::FIFO;
        } else if (policy_str == "sieve") {
            policy = EvictionPolicy::SIEVE;
        } else if (policy_str == "clock") {
            policy = EvictionPolicy::CLOCK;
        } else if (policy_str == "-h" || policy_str == "--help") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown policy: " << policy_str << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }
    
    if (argc > 2) {
        port = std::atoi(argv[2]);
    }
    
    std::cout << "==================================" << std::endl;
    std::cout << "Enhanced Cache Server with Multi-Threading" << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout << "Cache Size: 100 MB" << std::endl;
    std::cout << "Page Size: 40 KB" << std::endl;
    std::cout << "Total Pages: " << TOTAL_PAGES << std::endl;
    std::cout << "Worker Threads: " << NUM_WORKER_THREADS << std::endl;
    std::cout << "==================================" << std::endl;
    
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    g_server = new CacheServerEnhanced(policy);
    
    if (!g_server->start(port)) {
        std::cerr << "Failed to start server" << std::endl;
        delete g_server;
        return 1;
    }
    
    g_server->run();
    
    g_server->printStats();
    delete g_server;
    return 0;
}
