#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <vector>
#include <cstdint>

namespace utils {

// System resource detection
struct SystemResources {
    size_t total_ram_mb;
    size_t available_ram_mb;
    unsigned int cpu_cores;
    unsigned int optimal_threads;
};

SystemResources detect_system_resources();

// Calculate optimal thread count based on RandomX mode
// fast_mode: true = full dataset (~2GB shared), false = light mode (~256MB per cache)
unsigned int calculate_optimal_threads(const SystemResources& resources, bool fast_mode);

// Hex conversion utilities
std::string bytes_to_hex(const uint8_t* data, size_t len);
std::string bytes_to_hex_reversed(const uint8_t* data, size_t len); // For displaying block hashes
std::vector<uint8_t> hex_to_bytes(const std::string& hex);

// Endianness conversions
uint32_t read_le32(const uint8_t* data);
void write_le32(uint8_t* data, uint32_t value);
uint64_t read_le64(const uint8_t* data);
void write_le64(uint8_t* data, uint64_t value);

// Compact bits conversion (like Bitcoin/Zcash SetCompact)
// Converts compact bits format (e.g., 0x1f09daa8) to 256-bit target
std::vector<uint8_t> compact_to_target(uint32_t compact_bits);

// Hash comparison using 256-bit integer comparison
bool hash_meets_target(const uint8_t* hash, const std::vector<uint8_t>& target);

// Legacy hex string comparison (kept for compatibility)
bool hash_meets_target_hex(const uint8_t* hash, const std::string& target_hex);

// Time utilities
uint64_t get_current_timestamp();

// Varint encoding (for transaction count)
std::string encode_varint(uint64_t n);

// Block serialization
std::string serialize_block(const std::vector<uint8_t>& header,
                           const std::vector<uint8_t>& solution,
                           const std::string& coinbase_hex,
                           const std::vector<std::string>& txn_hex);

} // namespace utils

#endif // UTILS_H
