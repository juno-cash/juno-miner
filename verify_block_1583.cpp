#include "src/utils.h"
#include "randomx/randomx.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cstring>
#include <algorithm>

int main() {
    std::cout << "Verifying Block 1583" << std::endl;
    std::cout << "=====================" << std::endl;

    // Block 1583 data
    uint32_t version = 4;
    std::string prev_hash_hex = "23d39ee3ec4600c3f507230519a64ea5f6c444b22e85633a9526289127f4aa17";
    std::string merkle_root_hex = "cf56010cd2de6b1323a0b0cf5f8f7354a4fa41c492eae5861c7929f2673e4f8e";
    std::string block_commitments_hex = "bf9cd388aa99b6d79402d285567ea326025936ef92d5a4c1ab7ae732acb942f5";
    uint32_t time = 1760323089;
    uint32_t bits = 0x1f09daa8;
    std::string nonce_hex = "00004b208177028c86cd2875902953277897cebc15806b139d16c180b25a1262";
    std::string expected_hash_hex = "4268bf0d59a72f3f086020274dcc869164c092442ecc52246d6e760b28a80500";

    // Build header (140 bytes)
    std::vector<uint8_t> header(140);

    // Version (4 bytes, little-endian)
    utils::write_le32(&header[0], version);

    // Previous hash (32 bytes, reversed)
    std::vector<uint8_t> prev_hash = utils::hex_to_bytes(prev_hash_hex);
    std::reverse(prev_hash.begin(), prev_hash.end());
    std::copy(prev_hash.begin(), prev_hash.end(), header.begin() + 4);

    // Merkle root (32 bytes, reversed)
    std::vector<uint8_t> merkle = utils::hex_to_bytes(merkle_root_hex);
    std::reverse(merkle.begin(), merkle.end());
    std::copy(merkle.begin(), merkle.end(), header.begin() + 36);

    // Block commitments (32 bytes, reversed)
    std::vector<uint8_t> commitments = utils::hex_to_bytes(block_commitments_hex);
    std::reverse(commitments.begin(), commitments.end());
    std::copy(commitments.begin(), commitments.end(), header.begin() + 68);

    // Time (4 bytes, little-endian)
    utils::write_le32(&header[100], time);

    // Bits (4 bytes, little-endian)
    utils::write_le32(&header[104], bits);

    // Nonce (32 bytes, REVERSED like other hashes!)
    std::vector<uint8_t> nonce = utils::hex_to_bytes(nonce_hex);
    std::reverse(nonce.begin(), nonce.end());
    std::copy(nonce.begin(), nonce.end(), header.begin() + 108);

    std::cout << "Header (140 bytes):" << std::endl;
    std::cout << utils::bytes_to_hex(header.data(), 140) << std::endl;
    std::cout << std::endl;

    // Initialize RandomX
    randomx_flags flags = randomx_get_flags();
    randomx_cache* cache = randomx_alloc_cache(flags);
    const char* key = "ZcashRandomXPoW";
    randomx_init_cache(cache, key, strlen(key));
    randomx_vm* vm = randomx_create_vm(flags, cache, nullptr);

    // Calculate hash
    uint8_t hash[32];
    randomx_calculate_hash(vm, header.data(), header.size(), hash);

    std::string calculated_hash_hex = utils::bytes_to_hex(hash, 32);

    std::cout << "Expected hash:   " << expected_hash_hex << std::endl;
    std::cout << "Calculated hash: " << calculated_hash_hex << std::endl;
    std::cout << std::endl;

    if (calculated_hash_hex == expected_hash_hex) {
        std::cout << "✓ MATCH! Hash calculation is CORRECT!" << std::endl;
    } else {
        std::cout << "✗ MISMATCH! Hash calculation is WRONG!" << std::endl;
    }

    // Cleanup
    randomx_destroy_vm(vm);
    randomx_release_cache(cache);

    return (calculated_hash_hex == expected_hash_hex) ? 0 : 1;
}
