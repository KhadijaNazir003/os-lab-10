#include "cache_server_enhanced.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <iomanip>

const char* policyName(EvictionPolicy policy) {
    switch(policy) {
        case EvictionPolicy::LRU: return "LRU";
        case EvictionPolicy::FIFO: return "FIFO";
        case EvictionPolicy::SIEVE: return "SIEVE";
        case EvictionPolicy::CLOCK: return "CLOCK";
        default: return "UNKNOWN";
    }
}

CacheServerEnhanced::CacheServerEnhanced(EvictionPolicy eviction_policy) 
    : server_fd(-1), epoll_fd(-1), policy(eviction_policy), 
      fifo_counter(0), clock_hand(0) {
    cache.reserve(TOTAL_PAGES);
    sieve_hand = sieve_list.end();
}

CacheServerEnhanced::~CacheServerEnhanced() {
    stop();
}

bool CacheServerEnhanced::initializeCache() {
    try {
        std::cout << "Initializing cache with " << policyName(policy) << " eviction policy..." << std::endl;
        std::cout << "  Pages: " << TOTAL_PAGES << " x " << PAGE_SIZE << " bytes" << std::endl;
        
        cache.resize(TOTAL_PAGES);
        
        std::cout << "  Total cache size: " << (CACHE_SIZE / (1024.0 * 1024)) << " MB" << std::endl;
        std::cout << "Cache initialized successfully!" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize cache: " << e.what() << std::endl;
        return false;
    }
}

bool CacheServerEnhanced::setupServer(int port) {
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return false;
    }
    
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "Failed to set socket options" << std::endl;
        close(server_fd);
        return false;
    }
    
    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);
    
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Failed to bind to port " << port << std::endl;
        close(server_fd);
        return false;
    }
    
    if (listen(server_fd, SOMAXCONN) < 0) {
        std::cerr << "Failed to listen on socket" << std::endl;
        close(server_fd);
        return false;
    }
    
    std::cout << "Server listening on port " << port << std::endl;
    return true;
}

bool CacheServerEnhanced::setupEpoll() {
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        std::cerr << "Failed to create epoll instance" << std::endl;
        return false;
    }
    
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = server_fd;
    
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) < 0) {
        std::cerr << "Failed to add server socket to epoll" << std::endl;
        close(epoll_fd);
        return false;
    }
    
    return true;
}

void CacheServerEnhanced::startWorkerThreads() {
    std::cout << "Starting " << NUM_WORKER_THREADS << " worker threads..." << std::endl;
    
    for (int i = 0; i < NUM_WORKER_THREADS; ++i) {
        worker_threads.emplace_back(&CacheServerEnhanced::workerThreadFunction, this);
    }
}

void CacheServerEnhanced::stopWorkerThreads() {
    should_stop = true;
    queue_cv.notify_all();
    
    for (auto& thread : worker_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    worker_threads.clear();
}

void CacheServerEnhanced::workerThreadFunction() {
    while (!should_stop) {
        WorkItem item;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [this] { return !work_queue.empty() || should_stop; });
            
            if (should_stop && work_queue.empty()) {
                break;
            }
            
            if (!work_queue.empty()) {
                item = work_queue.front();
                work_queue.pop();
            } else {
                continue;
            }
        }
        
        // Process command
        Command cmd = parseCommand(item.data);
        std::string response;
        
        if (cmd.valid) {
            std::string client_id = getClientId(item.client_fd);
            response = processCommand(cmd, client_id);
        } else {
            response = "ERROR: Invalid command format.\r\n";
        }
        
        sendResponse(item.client_fd, response);
    }
}

void CacheServerEnhanced::handleNewConnection() {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            std::cerr << "Failed to accept connection" << std::endl;
        }
        return;
    }
    
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
    
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = client_fd;
    
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) < 0) {
        std::cerr << "Failed to add client to epoll" << std::endl;
        close(client_fd);
        return;
    }
    
    clients[client_fd] = ClientConnection(client_fd);
    std::string client_id = "client_" + std::to_string(client_fd);
    clients[client_fd].client_id = client_id;
    clients[client_fd].authenticated = true;
    
    std::string welcome = "OK: Connected as " + client_id + "\r\n";
    sendResponse(client_fd, welcome);
}

void CacheServerEnhanced::handleClientData(int client_fd) {
    char buffer[BUFFER_SIZE];
    
    while (true) {
        ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        
        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            handleClientDisconnect(client_fd);
            return;
        }
        
        if (bytes_read == 0) {
            handleClientDisconnect(client_fd);
            return;
        }
        
        buffer[bytes_read] = '\0';
        clients[client_fd].buffer += buffer;
        
        size_t cmd_end;
        while ((cmd_end = clients[client_fd].buffer.find("\r\n\r\n")) != std::string::npos ||
               (cmd_end = clients[client_fd].buffer.find("\n\n")) != std::string::npos) {
            
            std::string cmd_str = clients[client_fd].buffer.substr(0, cmd_end);
            clients[client_fd].buffer.erase(0, cmd_end + 4);
            
            // Add to work queue
            WorkItem item{client_fd, cmd_str};
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                work_queue.push(item);
            }
            queue_cv.notify_one();
        }
    }
}

void CacheServerEnhanced::handleClientDisconnect(int client_fd) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
    close(client_fd);
    clients.erase(client_fd);
}

Command CacheServerEnhanced::parseCommand(const std::string& message) {
    Command cmd;
    std::istringstream stream(message);
    std::string line;
    
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        if (line.empty()) continue;
        
        size_t colon_pos = line.find(':');
        if (colon_pos == std::string::npos) continue;
        
        std::string field = line.substr(0, colon_pos);
        std::string value = line.substr(colon_pos + 1);
        
        if (field == "Method") {
            cmd.method = value;
        } else if (field == "Key") {
            cmd.key = value;
        } else if (field == "Value") {
            cmd.value = value;
        }
    }
    
    if (!cmd.method.empty() && !cmd.key.empty()) {
        if (cmd.method == "ADD" || cmd.method == "UPDATE") {
            cmd.valid = !cmd.value.empty();
        } else if (cmd.method == "GET" || cmd.method == "DELETE") {
            cmd.valid = true;
        }
    }
    
    return cmd;
}

std::string CacheServerEnhanced::processCommand(const Command& cmd, const std::string& client_id) {
    if (cmd.method == "ADD") {
        return addKey(cmd.key, cmd.value, client_id);
    } else if (cmd.method == "UPDATE") {
        return updateKey(cmd.key, cmd.value, client_id);
    } else if (cmd.method == "GET") {
        return getKey(cmd.key, client_id);
    } else if (cmd.method == "DELETE") {
        return deleteKey(cmd.key, client_id);
    }
    
    return "ERROR: Unknown method.\r\n";
}

std::string CacheServerEnhanced::addKey(const std::string& key, const std::string& value, const std::string& client_id) {
    std::lock_guard<std::mutex> lock(cache_mutex);
    
    stats.total_requests++;
    
    if (entries.find(key) != entries.end()) {
        return "ERROR: Key already exists.\r\n";
    }
    
    if (!allocatePages(key, value.size(), client_id)) {
        return "ERROR: Not enough contiguous space.\r\n";
    }
    
    CacheEntry& entry = entries[key];
    if (!writeToPages(entry.start_page, value)) {
        freePages(key);
        entries.erase(key);
        return "ERROR: Failed to write data.\r\n";
    }
    
    updatePolicy(key);
    stats.adds++;
    stats.misses++;
    
    return "OK: Key added successfully.\r\n";
}

std::string CacheServerEnhanced::updateKey(const std::string& key, const std::string& value, const std::string& client_id) {
    std::lock_guard<std::mutex> lock(cache_mutex);
    
    stats.total_requests++;
    
    auto it = entries.find(key);
    if (it == entries.end()) {
        stats.misses++;
        return "ERROR: Key not found.\r\n";
    }
    
    if (it->second.client_id != client_id) {
        return "ERROR: Access denied. Key belongs to another client.\r\n";
    }
    
    size_t required_pages = calculateRequiredPages(value.size());
    
    if (required_pages <= it->second.num_pages) {
        if (!writeToPages(it->second.start_page, value)) {
            return "ERROR: Failed to write data.\r\n";
        }
        it->second.data_size = value.size();
        updatePolicy(key);
        stats.updates++;
        stats.hits++;
        return "OK: Key updated successfully.\r\n";
    }
    
    std::string old_client = it->second.client_id;
    freePages(key);
    entries.erase(key);
    
    if (!allocatePages(key, value.size(), old_client)) {
        stats.misses++;
        return "ERROR: Not enough contiguous space.\r\n";
    }
    
    if (!writeToPages(entries[key].start_page, value)) {
        freePages(key);
        entries.erase(key);
        stats.misses++;
        return "ERROR: Failed to write data.\r\n";
    }
    
    updatePolicy(key);
    stats.updates++;
    stats.hits++;
    return "OK: Key updated successfully.\r\n";
}

std::string CacheServerEnhanced::getKey(const std::string& key, const std::string& client_id) {
    std::lock_guard<std::mutex> lock(cache_mutex);
    
    stats.total_requests++;
    
    auto it = entries.find(key);
    if (it == entries.end()) {
        stats.misses++;
        return "ERROR: Key not found.\r\n";
    }
    
    if (it->second.client_id != client_id) {
        return "ERROR: Access denied. Key belongs to another client.\r\n";
    }
    
    std::string value = readFromPages(it->second.start_page, it->second.data_size);
    updatePolicy(key);
    stats.hits++;
    
    return "OK: Value=" + value + "\r\n";
}

std::string CacheServerEnhanced::deleteKey(const std::string& key, const std::string& client_id) {
    std::lock_guard<std::mutex> lock(cache_mutex);
    
    auto it = entries.find(key);
    if (it == entries.end()) {
        return "ERROR: Key not found.\r\n";
    }
    
    if (it->second.client_id != client_id) {
        return "ERROR: Access denied. Key belongs to another client.\r\n";
    }
    
    freePages(key);
    entries.erase(key);
    stats.deletes++;
    
    return "OK: Key deleted successfully.\r\n";
}

bool CacheServerEnhanced::allocatePages(const std::string& key, size_t data_size, const std::string& client_id) {
    size_t required_pages = calculateRequiredPages(data_size);
    size_t start_page;
    
    if (!findContiguousFreePages(required_pages, start_page)) {
        if (!evict(required_pages)) {
            return false;
        }
        
        if (!findContiguousFreePages(required_pages, start_page)) {
            return false;
        }
    }
    
    for (size_t i = start_page; i < start_page + required_pages; ++i) {
        cache[i].is_free = false;
    }
    
    CacheEntry entry;
    entry.key = key;
    entry.client_id = client_id;
    entry.start_page = start_page;
    entry.num_pages = required_pages;
    entry.data_size = data_size;
    entry.insertion_order = fifo_counter++;
    entry.visited = false;
    entry.reference_bit = false;
    
    entries[key] = entry;
    
    return true;
}

void CacheServerEnhanced::freePages(const std::string& key) {
    auto it = entries.find(key);
    if (it == entries.end()) return;
    
    for (size_t i = it->second.start_page; i < it->second.start_page + it->second.num_pages; ++i) {
        cache[i].is_free = true;
    }
}

bool CacheServerEnhanced::findContiguousFreePages(size_t num_pages, size_t& start_page) {
    size_t consecutive = 0;
    
    for (size_t i = 0; i < TOTAL_PAGES; ++i) {
        if (cache[i].is_free) {
            if (consecutive == 0) {
                start_page = i;
            }
            ++consecutive;
            
            if (consecutive == num_pages) {
                return true;
            }
        } else {
            consecutive = 0;
        }
    }
    
    return false;
}

size_t CacheServerEnhanced::calculateRequiredPages(size_t data_size) {
    return (data_size + PAGE_SIZE - 1) / PAGE_SIZE;
}

bool CacheServerEnhanced::evict(size_t required_pages) {
    switch(policy) {
        case EvictionPolicy::LRU:
            return evictLRU(required_pages);
        case EvictionPolicy::FIFO:
            return evictFIFO(required_pages);
        case EvictionPolicy::SIEVE:
            return evictSIEVE(required_pages);
        case EvictionPolicy::CLOCK:
            return evictClock(required_pages);
        default:
            return false;
    }
}

// LRU implementation
bool CacheServerEnhanced::evictLRU(size_t required_pages) {
    size_t freed_pages = 0;
    std::vector<std::string> to_evict;
    
    for (auto rit = lru_list.rbegin(); rit != lru_list.rend() && freed_pages < required_pages; ++rit) {
        const std::string& key = *rit;
        auto it = entries.find(key);
        if (it != entries.end()) {
            to_evict.push_back(key);
            freed_pages += it->second.num_pages;
        }
    }
    
    if (freed_pages < required_pages) {
        return false;
    }
    
    for (const std::string& key : to_evict) {
        auto it = entries.find(key);
        lru_list.erase(it->second.lru_iter);
        freePages(key);
        entries.erase(key);
        stats.evictions++;
    }
    
    return true;
}

void CacheServerEnhanced::updateLRU(const std::string& key) {
    auto it = entries.find(key);
    if (it == entries.end()) return;
    
    if (it->second.lru_iter != lru_list.end()) {
        lru_list.erase(it->second.lru_iter);
    }
    
    lru_list.push_front(key);
    it->second.lru_iter = lru_list.begin();
}

// FIFO implementation
bool CacheServerEnhanced::evictFIFO(size_t required_pages) {
    size_t freed_pages = 0;
    std::vector<std::string> to_evict;
    
    // Find entries with oldest insertion order
    std::vector<std::pair<size_t, std::string>> ordered;
    for (const auto& [key, entry] : entries) {
        ordered.push_back({entry.insertion_order, key});
    }
    std::sort(ordered.begin(), ordered.end());
    
    for (const auto& [order, key] : ordered) {
        if (freed_pages >= required_pages) break;
        to_evict.push_back(key);
        freed_pages += entries[key].num_pages;
    }
    
    if (freed_pages < required_pages) {
        return false;
    }
    
    for (const std::string& key : to_evict) {
        freePages(key);
        entries.erase(key);
        stats.evictions++;
    }
    
    return true;
}

void CacheServerEnhanced::updateFIFO(const std::string& key) {
    // FIFO doesn't update on access
    (void)key;
}

// SIEVE implementation (state-of-the-art eviction algorithm)
bool CacheServerEnhanced::evictSIEVE(size_t required_pages) {
    size_t freed_pages = 0;
    
    // SIEVE hand moves through list, clearing visited bits
    // Evicts first non-visited entry found
    
    bool wrapped = false;
    
    while (freed_pages < required_pages) {
        if (sieve_hand == sieve_list.end()) {
            sieve_hand = sieve_list.begin();
            if (wrapped) break; // Full cycle completed
            wrapped = true;
        }
        
        if (sieve_hand == sieve_list.end()) break;
        
        std::string key = *sieve_hand;
        auto it = entries.find(key);
        
        if (it != entries.end()) {
            if (!it->second.visited) {
                // Evict this entry
                auto next = std::next(sieve_hand);
                sieve_list.erase(sieve_hand);
                sieve_hand = next;
                
                freed_pages += it->second.num_pages;
                freePages(key);
                entries.erase(key);
                stats.evictions++;
            } else {
                // Clear visited bit and move on
                it->second.visited = false;
                ++sieve_hand;
            }
        } else {
            ++sieve_hand;
        }
    }
    
    return freed_pages >= required_pages;
}

void CacheServerEnhanced::updateSIEVE(const std::string& key) {
    auto it = entries.find(key);
    if (it == entries.end()) {
        // New entry, add to list
        sieve_list.push_back(key);
        entries[key].visited = false;
    } else {
        // Mark as visited
        it->second.visited = true;
    }
}

// Clock (Second Chance) implementation
bool CacheServerEnhanced::evictClock(size_t required_pages) {
    size_t freed_pages = 0;
    
    if (clock_list.empty()) return false;
    
    bool wrapped = false;
    
    while (freed_pages < required_pages) {
        if (clock_hand >= clock_list.size()) {
            clock_hand = 0;
            if (wrapped) break;
            wrapped = true;
        }
        
        std::string& key = clock_list[clock_hand];
        auto it = entries.find(key);
        
        if (it != entries.end()) {
            if (!it->second.reference_bit) {
                // Evict
                freed_pages += it->second.num_pages;
                freePages(key);
                entries.erase(key);
                stats.evictions++;
                
                clock_list.erase(clock_list.begin() + clock_hand);
                if (clock_hand >= clock_list.size()) clock_hand = 0;
            } else {
                // Give second chance
                it->second.reference_bit = false;
                clock_hand++;
            }
        } else {
            clock_list.erase(clock_list.begin() + clock_hand);
            if (clock_hand >= clock_list.size()) clock_hand = 0;
        }
    }
    
    return freed_pages >= required_pages;
}

void CacheServerEnhanced::updateClock(const std::string& key) {
    auto it = entries.find(key);
    if (it == entries.end()) {
        // New entry
        clock_list.push_back(key);
        entries[key].reference_bit = true;
        entries[key].clock_position = clock_list.size() - 1;
    } else {
        // Set reference bit
        it->second.reference_bit = true;
    }
}

void CacheServerEnhanced::updatePolicy(const std::string& key) {
    switch(policy) {
        case EvictionPolicy::LRU:
            updateLRU(key);
            break;
        case EvictionPolicy::FIFO:
            updateFIFO(key);
            break;
        case EvictionPolicy::SIEVE:
            updateSIEVE(key);
            break;
        case EvictionPolicy::CLOCK:
            updateClock(key);
            break;
    }
}

bool CacheServerEnhanced::writeToPages(size_t start_page, const std::string& data) {
    size_t offset = 0;
    size_t page_idx = start_page;
    
    while (offset < data.size()) {
        if (page_idx >= TOTAL_PAGES) return false;
        
        size_t to_copy = std::min(PAGE_SIZE, data.size() - offset);
        std::memcpy(cache[page_idx].data, data.data() + offset, to_copy);
        
        offset += to_copy;
        ++page_idx;
    }
    
    return true;
}

std::string CacheServerEnhanced::readFromPages(size_t start_page, size_t data_size) {
    std::string result;
    result.reserve(data_size);
    
    size_t remaining = data_size;
    size_t page_idx = start_page;
    
    while (remaining > 0 && page_idx < TOTAL_PAGES) {
        size_t to_read = std::min(PAGE_SIZE, remaining);
        result.append(reinterpret_cast<char*>(cache[page_idx].data), to_read);
        
        remaining -= to_read;
        ++page_idx;
    }
    
    return result;
}

void CacheServerEnhanced::sendResponse(int client_fd, const std::string& response) {
    send(client_fd, response.c_str(), response.size(), 0);
}

std::string CacheServerEnhanced::getClientId(int client_fd) {
    auto it = clients.find(client_fd);
    if (it != clients.end()) {
        return it->second.client_id;
    }
    return "unknown";
}

bool CacheServerEnhanced::start(int port) {
    if (!initializeCache()) {
        return false;
    }
    
    if (!setupServer(port)) {
        return false;
    }
    
    if (!setupEpoll()) {
        close(server_fd);
        return false;
    }
    
    startWorkerThreads();
    
    std::cout << "Cache server started successfully!" << std::endl;
    std::cout << "Eviction policy: " << policyName(policy) << std::endl;
    std::cout << "Worker threads: " << NUM_WORKER_THREADS << std::endl;
    return true;
}

void CacheServerEnhanced::run() {
    struct epoll_event events[MAX_EVENTS];
    
    std::cout << "Server running... Press Ctrl+C to stop." << std::endl;
    
    while (!should_stop) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, 100);
        
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        
        for (int i = 0; i < n; ++i) {
            if (events[i].data.fd == server_fd) {
                handleNewConnection();
            } else {
                if (events[i].events & EPOLLIN) {
                    handleClientData(events[i].data.fd);
                }
            }
        }
    }
}

void CacheServerEnhanced::stop() {
    std::cout << "Stopping server..." << std::endl;
    
    should_stop = true;
    stopWorkerThreads();
    
    for (auto& pair : clients) {
        close(pair.first);
    }
    clients.clear();
    
    if (epoll_fd >= 0) {
        close(epoll_fd);
        epoll_fd = -1;
    }
    
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    
    std::cout << "Server stopped." << std::endl;
}

void CacheServerEnhanced::printStats() const {
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "Cache Statistics (" << policyName(policy) << " Policy)" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "Total Requests:   " << std::setw(10) << stats.total_requests.load() << std::endl;
    std::cout << "Cache Hits:       " << std::setw(10) << stats.hits.load() 
              << " (" << std::fixed << std::setprecision(2) << (stats.getHitRatio() * 100) << "%)" << std::endl;
    std::cout << "Cache Misses:     " << std::setw(10) << stats.misses.load() << std::endl;
    std::cout << "Evictions:        " << std::setw(10) << stats.evictions.load() << std::endl;
    std::cout << "Adds:             " << std::setw(10) << stats.adds.load() << std::endl;
    std::cout << "Updates:          " << std::setw(10) << stats.updates.load() << std::endl;
    std::cout << "Deletes:          " << std::setw(10) << stats.deletes.load() << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "HIT RATIO:        " << std::fixed << std::setprecision(4) 
              << (stats.getHitRatio() * 100) << "%" << std::endl;
    std::cout << std::string(60, '=') << "\n" << std::endl;
}
