#ifndef CACHE_SERVER_ENHANCED_H
#define CACHE_SERVER_ENHANCED_H

#include <string>
#include <unordered_map>
#include <vector>
#include <list>
#include <queue>
#include <cstdint>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <sys/epoll.h>

// Constants
constexpr size_t CACHE_SIZE = 100ULL * 1024 * 1024; // 100 MB
constexpr size_t PAGE_SIZE = 40 * 1024; // 40 KB
constexpr size_t TOTAL_PAGES = CACHE_SIZE / PAGE_SIZE;
constexpr int MAX_EVENTS = 64;
constexpr int BUFFER_SIZE = 4096;
constexpr int NUM_WORKER_THREADS = 4;

// Eviction policies
enum class EvictionPolicy {
    LRU,    // Least Recently Used
    FIFO,   // First In First Out
    SIEVE,  // SIEVE algorithm (https://cachemon.github.io/SIEVE-website/)
    CLOCK   // Clock (Second Chance)
};

// Page structure
struct Page {
    uint8_t data[PAGE_SIZE];
    bool is_free;
    
    Page() : is_free(true) {}
};

// Cache entry metadata
struct CacheEntry {
    std::string key;
    std::string client_id;
    size_t start_page;
    size_t num_pages;
    size_t data_size;
    
    // Policy-specific data
    std::list<std::string>::iterator lru_iter;  // For LRU
    size_t insertion_order;                      // For FIFO
    bool visited;                                // For SIEVE
    bool reference_bit;                          // For Clock
    size_t clock_position;                       // For Clock
    
    CacheEntry() : start_page(0), num_pages(0), data_size(0), 
                   insertion_order(0), visited(false), 
                   reference_bit(false), clock_position(0) {}
};

// Client connection state
struct ClientConnection {
    int fd;
    std::string client_id;
    std::string buffer;
    bool authenticated;
    
    ClientConnection() : fd(-1), authenticated(false) {}
    ClientConnection(int socket_fd) 
        : fd(socket_fd), authenticated(false) {}
};

// Protocol command
struct Command {
    std::string method;
    std::string key;
    std::string value;
    bool valid;
    
    Command() : valid(false) {}
};

// Work item for thread pool
struct WorkItem {
    int client_fd;
    std::string data;
};

// Cache statistics
struct CacheStats {
    std::atomic<uint64_t> total_requests{0};
    std::atomic<uint64_t> hits{0};
    std::atomic<uint64_t> misses{0};
    std::atomic<uint64_t> evictions{0};
    std::atomic<uint64_t> adds{0};
    std::atomic<uint64_t> updates{0};
    std::atomic<uint64_t> deletes{0};
    
    double getHitRatio() const {
        uint64_t total = total_requests.load();
        return total > 0 ? (double)hits.load() / total : 0.0;
    }
    
    void reset() {
        total_requests = 0;
        hits = 0;
        misses = 0;
        evictions = 0;
        adds = 0;
        updates = 0;
        deletes = 0;
    }
};

// Enhanced Cache Server class
class CacheServerEnhanced {
private:
    int server_fd;
    int epoll_fd;
    std::vector<Page> cache;
    std::unordered_map<std::string, CacheEntry> entries;
    std::unordered_map<int, ClientConnection> clients;
    
    // Eviction policy
    EvictionPolicy policy;
    
    // LRU data structures
    std::list<std::string> lru_list;
    
    // FIFO data structures
    std::queue<std::string> fifo_queue;
    size_t fifo_counter;
    
    // SIEVE data structures
    std::list<std::string> sieve_list;
    std::list<std::string>::iterator sieve_hand;
    
    // Clock data structures
    std::vector<std::string> clock_list;
    size_t clock_hand;
    
    // Thread pool
    std::vector<std::thread> worker_threads;
    std::queue<WorkItem> work_queue;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::atomic<bool> should_stop{false};
    
    // Cache mutex for thread safety
    std::mutex cache_mutex;
    
    // Statistics
    CacheStats stats;
    
    // Private methods
    bool initializeCache();
    bool setupServer(int port);
    bool setupEpoll();
    void startWorkerThreads();
    void stopWorkerThreads();
    void workerThreadFunction();
    
    void handleNewConnection();
    void handleClientData(int client_fd);
    void handleClientDisconnect(int client_fd);
    
    Command parseCommand(const std::string& message);
    std::string processCommand(const Command& cmd, const std::string& client_id);
    
    // Cache operations
    std::string addKey(const std::string& key, const std::string& value, const std::string& client_id);
    std::string updateKey(const std::string& key, const std::string& value, const std::string& client_id);
    std::string getKey(const std::string& key, const std::string& client_id);
    std::string deleteKey(const std::string& key, const std::string& client_id);
    
    // Memory management
    bool allocatePages(const std::string& key, size_t data_size, const std::string& client_id);
    void freePages(const std::string& key);
    bool findContiguousFreePages(size_t num_pages, size_t& start_page);
    size_t calculateRequiredPages(size_t data_size);
    
    // Eviction policies
    bool evict(size_t required_pages);
    bool evictLRU(size_t required_pages);
    bool evictFIFO(size_t required_pages);
    bool evictSIEVE(size_t required_pages);
    bool evictClock(size_t required_pages);
    
    // Policy-specific updates
    void updatePolicy(const std::string& key);
    void updateLRU(const std::string& key);
    void updateFIFO(const std::string& key);
    void updateSIEVE(const std::string& key);
    void updateClock(const std::string& key);
    
    // Data operations
    bool writeToPages(size_t start_page, const std::string& data);
    std::string readFromPages(size_t start_page, size_t data_size);
    
    // Utility
    void sendResponse(int client_fd, const std::string& response);
    std::string getClientId(int client_fd);

public:
    CacheServerEnhanced(EvictionPolicy eviction_policy = EvictionPolicy::LRU);
    ~CacheServerEnhanced();
    
    bool start(int port);
    void run();
    void stop();
    
    // Statistics
    const CacheStats& getStats() const { return stats; }
    void resetStats() { stats.reset(); }
    void printStats() const;
};

#endif // CACHE_SERVER_ENHANCED_H
