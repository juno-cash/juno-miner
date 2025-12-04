#include "utils.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <cstring>
#include <chrono>
#include <sys/sysinfo.h>
#include <openssl/sha.h>

namespace utils {

SystemResources detect_system_resources() {
    SystemResources resources;

    // Get CPU core count
    resources.cpu_cores = std::thread::hardware_concurrency();
    if (resources.cpu_cores == 0) {
        resources.cpu_cores = 1;
    }

    // Get RAM information from /proc/meminfo
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    resources.total_ram_mb = 0;
    resources.available_ram_mb = 0;

    while (std::getline(meminfo, line)) {
        if (line.find("MemTotal:") == 0) {
            std::istringstream iss(line);
            std::string label;
            size_t value;
            iss >> label >> value;
            resources.total_ram_mb = value / 1024; // Convert KB to MB
        } else if (line.find("MemAvailable:") == 0) {
            std::istringstream iss(line);
            std::string label;
            size_t value;
            iss >> label >> value;
            resources.available_ram_mb = value / 1024; // Convert KB to MB
        }
    }

    // Fallback to sysinfo if meminfo parsing failed
    if (resources.total_ram_mb == 0) {
        struct sysinfo info;
        if (sysinfo(&info) == 0) {
            resources.total_ram_mb = (info.totalram * info.mem_unit) / (1024 * 1024);
            resources.available_ram_mb = (info.freeram * info.mem_unit) / (1024 * 1024);
        }
    }

    // Default optimal_threads to CPU cores (will be recalculated based on mode)
    resources.optimal_threads = resources.cpu_cores;
    if (resources.optimal_threads == 0) {
        resources.optimal_threads = 1;
    }

    return resources;
}

unsigned int calculate_optimal_threads(const SystemResources& resources, bool fast_mode) {
    unsigned int max_threads;

    if (fast_mode) {
        // Fast mode: ~2GB shared dataset + ~256MB cache + ~2MB per thread scratchpad
        // Need at least 2.5GB for the shared dataset+cache, then threads are cheap
        const size_t FAST_MODE_BASE_MB = 2560;  // 2.5GB for dataset + cache
        const size_t FAST_MODE_PER_THREAD_MB = 4;  // ~2MB scratchpad + overhead

        if (resources.available_ram_mb < FAST_MODE_BASE_MB) {
            // Not enough RAM for fast mode
            return 0;
        }

        size_t remaining_ram = resources.available_ram_mb - FAST_MODE_BASE_MB;
        max_threads = remaining_ram / FAST_MODE_PER_THREAD_MB;

        // In fast mode, RAM isn't usually the limit after the base allocation
        // CPU cores will typically be the constraint
        if (max_threads > resources.cpu_cores) {
            max_threads = resources.cpu_cores;
        }
    } else {
        // Light mode: ~256MB shared cache + ~2MB per thread scratchpad
        // RAM is rarely a constraint in light mode
        const size_t LIGHT_MODE_BASE_MB = 300;  // ~256MB cache + overhead
        const size_t LIGHT_MODE_PER_THREAD_MB = 4;  // ~2MB scratchpad + overhead

        if (resources.available_ram_mb < LIGHT_MODE_BASE_MB) {
            return 1;  // Minimum
        }

        size_t remaining_ram = resources.available_ram_mb - LIGHT_MODE_BASE_MB;
        max_threads = remaining_ram / LIGHT_MODE_PER_THREAD_MB;

        // CPU cores are typically the constraint in light mode
        if (max_threads > resources.cpu_cores) {
            max_threads = resources.cpu_cores;
        }
    }

    // Ensure at least 1 thread
    if (max_threads == 0) {
        max_threads = 1;
    }

    return max_threads;
}

std::string bytes_to_hex(const uint8_t* data, size_t len) {
    std::ostringstream oss;
    for (size_t i = 0; i < len; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)data[i];
    }
    return oss.str();
}

std::string bytes_to_hex_reversed(const uint8_t* data, size_t len) {
    // Display in reversed byte order (big-endian) for block hashes
    std::ostringstream oss;
    for (int i = len - 1; i >= 0; i--) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)data[i];
    }
    return oss.str();
}

std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byte_str = hex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::stoi(byte_str, nullptr, 16));
        bytes.push_back(byte);
    }
    return bytes;
}

uint32_t read_le32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

void write_le32(uint8_t* data, uint32_t value) {
    data[0] = value & 0xff;
    data[1] = (value >> 8) & 0xff;
    data[2] = (value >> 16) & 0xff;
    data[3] = (value >> 24) & 0xff;
}

uint64_t read_le64(const uint8_t* data) {
    return static_cast<uint64_t>(data[0]) |
           (static_cast<uint64_t>(data[1]) << 8) |
           (static_cast<uint64_t>(data[2]) << 16) |
           (static_cast<uint64_t>(data[3]) << 24) |
           (static_cast<uint64_t>(data[4]) << 32) |
           (static_cast<uint64_t>(data[5]) << 40) |
           (static_cast<uint64_t>(data[6]) << 48) |
           (static_cast<uint64_t>(data[7]) << 56);
}

void write_le64(uint8_t* data, uint64_t value) {
    data[0] = value & 0xff;
    data[1] = (value >> 8) & 0xff;
    data[2] = (value >> 16) & 0xff;
    data[3] = (value >> 24) & 0xff;
    data[4] = (value >> 32) & 0xff;
    data[5] = (value >> 40) & 0xff;
    data[6] = (value >> 48) & 0xff;
    data[7] = (value >> 56) & 0xff;
}

// Convert compact bits format to 256-bit target (replicates arith_uint256::SetCompact)
std::vector<uint8_t> compact_to_target(uint32_t compact_bits) {
    // Extract size (exponent) and mantissa from compact format
    int size = compact_bits >> 24;
    uint32_t word = compact_bits & 0x007fffff;

    // Initialize 256-bit target as zeros
    std::vector<uint8_t> target(32, 0);

    if (size <= 3) {
        // For small sizes, shift right
        word >>= 8 * (3 - size);
        // Write mantissa bytes at the beginning (little-endian)
        for (int i = 0; i < 3 && i < size; i++) {
            target[i] = (word >> (i * 8)) & 0xff;
        }
    } else if (size <= 32) {
        // The mantissa is 3 bytes, written starting at position (size - 3)
        // This places the mantissa at the correct position in little-endian format
        int pos = size - 3;
        target[pos] = word & 0xff;
        target[pos + 1] = (word >> 8) & 0xff;
        target[pos + 2] = (word >> 16) & 0xff;
    }

    return target;
}

// Hash comparison using 256-bit integer comparison (replicates UintToArith256 comparison)
bool hash_meets_target(const uint8_t* hash, const std::vector<uint8_t>& target) {
    // Compare as little-endian 32-bit words from high to low
    // hash <= target (hash must be less than OR equal to target)
    for (int word = 7; word >= 0; word--) {
        uint32_t hash_word = read_le32(hash + word * 4);
        uint32_t target_word = read_le32(target.data() + word * 4);

        if (hash_word < target_word) return true;
        if (hash_word > target_word) return false;
    }
    return true; // Equal is valid (hash == target is acceptable)
}

// Legacy hex string comparison (kept for compatibility)
bool hash_meets_target_hex(const uint8_t* hash, const std::string& target_hex) {
    std::vector<uint8_t> target = hex_to_bytes(target_hex);
    return hash_meets_target(hash, target);
}

uint64_t get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::seconds>(duration).count();
}

std::string encode_varint(uint64_t n) {
    std::string result;
    if (n < 0xfd) {
        result += static_cast<char>(n);
    } else if (n <= 0xffff) {
        result += static_cast<char>(0xfd);
        result += static_cast<char>(n & 0xff);
        result += static_cast<char>((n >> 8) & 0xff);
    } else if (n <= 0xffffffff) {
        result += static_cast<char>(0xfe);
        result += static_cast<char>(n & 0xff);
        result += static_cast<char>((n >> 8) & 0xff);
        result += static_cast<char>((n >> 16) & 0xff);
        result += static_cast<char>((n >> 24) & 0xff);
    } else {
        result += static_cast<char>(0xff);
        result += static_cast<char>(n & 0xff);
        result += static_cast<char>((n >> 8) & 0xff);
        result += static_cast<char>((n >> 16) & 0xff);
        result += static_cast<char>((n >> 24) & 0xff);
        result += static_cast<char>((n >> 32) & 0xff);
        result += static_cast<char>((n >> 40) & 0xff);
        result += static_cast<char>((n >> 48) & 0xff);
        result += static_cast<char>((n >> 56) & 0xff);
    }
    return result;
}

std::string serialize_block(const std::vector<uint8_t>& header,
                           const std::vector<uint8_t>& solution,
                           const std::string& coinbase_hex,
                           const std::vector<std::string>& txn_hex) {
    std::string result;

    // 1. Block header (140 bytes: header_base + nonce)
    for (uint8_t byte : header) {
        result += static_cast<char>(byte);
    }

    // 2. nSolution (compact_size vector: size + data)
    // For RandomX, solution is always 32 bytes, so size = 0x20
    result += encode_varint(solution.size());
    for (uint8_t byte : solution) {
        result += static_cast<char>(byte);
    }

    // 3. Transaction count (varint)
    uint64_t tx_count = 1 + txn_hex.size(); // coinbase + other txs
    result += encode_varint(tx_count);

    // 4. Coinbase transaction
    std::vector<uint8_t> coinbase_bytes = hex_to_bytes(coinbase_hex);
    for (uint8_t byte : coinbase_bytes) {
        result += static_cast<char>(byte);
    }

    // 5. Other transactions
    for (const auto& tx_hex : txn_hex) {
        std::vector<uint8_t> tx_bytes = hex_to_bytes(tx_hex);
        for (uint8_t byte : tx_bytes) {
            result += static_cast<char>(byte);
        }
    }

    // Convert to hex string
    return bytes_to_hex(reinterpret_cast<const uint8_t*>(result.data()), result.size());
}


} // namespace utils
