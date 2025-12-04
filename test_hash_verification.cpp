#include "src/miner.h"
#include "src/utils.h"
#include "randomx/randomx.h"
#include <iostream>
#include <iomanip>
#include <fstream>

int main() {
    // Read the block template
    std::ifstream file("/tmp/template.json");
    if (!file.is_open()) {
        std::cerr << "Failed to open /tmp/template.json" << std::endl;
        return 1;
    }

    std::string json_str((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(json_str, root)) {
        std::cerr << "Failed to parse JSON" << std::endl;
        return 1;
    }

    BlockTemplate bt = parse_block_template(root["result"]);

    std::cout << "========================================" << std::endl;
    std::cout << "Block Template Analysis" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Height: " << bt.height << std::endl;
    std::cout << "Version: " << bt.version << std::endl;
    std::cout << "Target: " << bt.target_hex << std::endl;
    std::cout << "Bits: 0x" << std::hex << bt.bits << std::dec << std::endl;
    std::cout << "Time: " << bt.time << std::endl;
    std::cout << std::endl;

    std::cout << "Previous block hash: " << bt.previous_block_hash << std::endl;
    std::cout << "Merkle root: " << bt.merkle_root << std::endl;
    std::cout << "Block commitments: " << bt.block_commitments_hash << std::endl;
    std::cout << std::endl;

    // Show header construction (first 108 bytes - everything except nonce)
    std::cout << "Header base (108 bytes, before nonce):" << std::endl;
    std::cout << utils::bytes_to_hex(bt.header_base.data(), 108) << std::endl;
    std::cout << std::endl;

    // Show what the header bytes represent
    std::cout << "Header breakdown:" << std::endl;
    std::cout << "  [0-3]    Version:     " << utils::bytes_to_hex(&bt.header_base[0], 4) << std::endl;
    std::cout << "  [4-35]   Prev hash:   " << utils::bytes_to_hex(&bt.header_base[4], 32) << std::endl;
    std::cout << "  [36-67]  Merkle root: " << utils::bytes_to_hex(&bt.header_base[36], 32) << std::endl;
    std::cout << "  [68-99]  Commitments: " << utils::bytes_to_hex(&bt.header_base[68], 32) << std::endl;
    std::cout << "  [100-103] Time:       " << utils::bytes_to_hex(&bt.header_base[100], 4) << std::endl;
    std::cout << "  [104-107] Bits:       " << utils::bytes_to_hex(&bt.header_base[104], 4) << std::endl;
    std::cout << std::endl;

    // Initialize RandomX with CORRECT key
    std::cout << "Initializing RandomX with key 'ZcashRandomXPoW'..." << std::endl;
    randomx_flags flags = randomx_get_flags();
    randomx_cache* cache = randomx_alloc_cache(flags);
    const char* key = "ZcashRandomXPoW";
    randomx_init_cache(cache, key, strlen(key));
    randomx_vm* vm = randomx_create_vm(flags, cache, nullptr);
    std::cout << std::endl;

    // Test with nonce = all zeros
    std::vector<uint8_t> hash_input(140);
    std::copy(bt.header_base.begin(), bt.header_base.begin() + 108, hash_input.begin());

    // Nonce = all zeros
    std::vector<uint8_t> nonce(32, 0);
    std::copy(nonce.begin(), nonce.end(), hash_input.begin() + 108);

    std::cout << "Test 1: Nonce = all zeros" << std::endl;
    std::cout << "Full hash input (140 bytes):" << std::endl;
    std::cout << utils::bytes_to_hex(hash_input.data(), 140) << std::endl;
    std::cout << std::endl;

    uint8_t hash[32];
    randomx_calculate_hash(vm, hash_input.data(), hash_input.size(), hash);

    std::cout << "RandomX hash output:" << std::endl;
    std::cout << utils::bytes_to_hex(hash, 32) << std::endl;
    std::cout << std::endl;

    // Show hash as LE32 words
    std::cout << "Hash as LE32 words (high to low):" << std::endl;
    for (int word = 7; word >= 0; word--) {
        uint32_t w = utils::read_le32(hash + word * 4);
        std::cout << "  Word[" << word << "] = 0x" << std::hex << std::setw(8) << std::setfill('0') << w << std::dec << std::endl;
    }
    std::cout << std::endl;

    // Show target as LE32 words - use the converted target bytes directly
    std::cout << "Target as LE32 words (high to low):" << std::endl;
    for (int word = 7; word >= 0; word--) {
        uint32_t w = utils::read_le32(bt.target.data() + word * 4);
        std::cout << "  Word[" << word << "] = 0x" << std::hex << std::setw(8) << std::setfill('0') << w << std::dec << std::endl;
    }
    std::cout << std::endl;

    // Compare
    std::cout << "Comparison (hash vs target):" << std::endl;
    bool meets_target = true;
    for (int word = 7; word >= 0; word--) {
        uint32_t hash_word = utils::read_le32(hash + word * 4);
        uint32_t target_word = utils::read_le32(bt.target.data() + word * 4);
        std::cout << "  Word[" << word << "]: 0x" << std::hex << std::setw(8) << hash_word
                  << " vs 0x" << std::setw(8) << target_word << std::dec;
        if (hash_word < target_word) {
            std::cout << " < PASS" << std::endl;
            break;
        } else if (hash_word > target_word) {
            std::cout << " > FAIL" << std::endl;
            meets_target = false;
            break;
        } else {
            std::cout << " = (continue)" << std::endl;
        }
    }
    std::cout << "Result: " << (meets_target ? "MEETS TARGET" : "FAILS TARGET") << std::endl;
    std::cout << std::endl;

    // Test with nonce = 1
    nonce[0] = 1;
    std::copy(nonce.begin(), nonce.end(), hash_input.begin() + 108);

    std::cout << "========================================" << std::endl;
    std::cout << "Test 2: Nonce = 1" << std::endl;
    std::cout << "Nonce bytes: " << utils::bytes_to_hex(nonce.data(), 32) << std::endl;

    randomx_calculate_hash(vm, hash_input.data(), hash_input.size(), hash);
    std::cout << "Hash: " << utils::bytes_to_hex(hash, 32) << std::endl;

    std::cout << "Hash as LE32 words: ";
    for (int word = 7; word >= 0; word--) {
        std::cout << std::hex << std::setw(8) << std::setfill('0') << utils::read_le32(hash + word * 4);
        if (word > 0) std::cout << " ";
    }
    std::cout << std::dec << std::endl;

    bool meets = utils::hash_meets_target(hash, bt.target);
    std::cout << "Meets target: " << (meets ? "YES" : "NO") << std::endl;
    std::cout << std::endl;

    // Test with a few more nonces
    std::cout << "========================================" << std::endl;
    std::cout << "Testing first 10 nonces..." << std::endl;
    std::cout << std::endl;

    for (int i = 0; i < 10; i++) {
        std::fill(nonce.begin(), nonce.end(), 0);
        nonce[0] = i;
        std::copy(nonce.begin(), nonce.end(), hash_input.begin() + 108);

        randomx_calculate_hash(vm, hash_input.data(), hash_input.size(), hash);
        bool meets = utils::hash_meets_target(hash, bt.target);

        std::cout << "Nonce " << std::setw(2) << i << ": " << utils::bytes_to_hex(hash, 32);
        if (meets) {
            std::cout << " *** VALID BLOCK FOUND! ***";
        }
        std::cout << std::endl;
    }

    // Cleanup
    randomx_destroy_vm(vm);
    randomx_release_cache(cache);

    return 0;
}
