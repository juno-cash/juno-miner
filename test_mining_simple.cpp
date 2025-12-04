#include "src/miner.h"
#include "src/utils.h"
#include "randomx/randomx.h"
#include <iostream>
#include <fstream>
#include <algorithm>

int main() {
    // Read template
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

    std::cout << "Testing mining for block " << bt.height << std::endl;
    std::cout << "Target: " << bt.target_hex << std::endl;
    std::cout << std::endl;

    // Initialize RandomX
    randomx_flags flags = randomx_get_flags();
    randomx_cache* cache = randomx_alloc_cache(flags);
    const char* key = "ZcashRandomXPoW";
    randomx_init_cache(cache, key, strlen(key));
    randomx_vm* vm = randomx_create_vm(flags, cache, nullptr);

    // Prepare header
    std::vector<uint8_t> hash_input(140);
    std::copy(bt.header_base.begin(), bt.header_base.begin() + 108, hash_input.begin());

    // Try 100,000 nonces
    std::vector<uint8_t> nonce(32, 0);
    uint8_t hash[32];
    int valid_count = 0;
    uint32_t best_hash_word7 = 0xFFFFFFFF;

    std::cout << "Trying 100,000 nonces..." << std::endl;

    for (int i = 0; i < 100000; i++) {
        // Set nonce
        std::fill(nonce.begin(), nonce.end(), 0);
        nonce[0] = i & 0xFF;
        nonce[1] = (i >> 8) & 0xFF;
        nonce[2] = (i >> 16) & 0xFF;

        // Reverse nonce
        std::vector<uint8_t> nonce_reversed(nonce.rbegin(), nonce.rend());
        std::copy(nonce_reversed.begin(), nonce_reversed.end(), hash_input.begin() + 108);

        // Hash
        randomx_calculate_hash(vm, hash_input.data(), hash_input.size(), hash);

        // Track best hash (lowest high word)
        uint32_t hash_word7 = utils::read_le32(hash + 28);
        if (hash_word7 < best_hash_word7) {
            best_hash_word7 = hash_word7;
            std::cout << "  Best so far at nonce " << i << ": word[7]=0x" << std::hex << hash_word7 << std::dec << std::endl;
            std::cout << "    Full hash: " << utils::bytes_to_hex(hash, 32) << std::endl;
        }

        // Check against target
        bool meets = utils::hash_meets_target(hash, bt.target);
        if (meets) {
            std::cout << "VALID HASH FOUND at nonce " << i << "!" << std::endl;
            std::cout << "  Hash: " << utils::bytes_to_hex(hash, 32) << std::endl;
            valid_count++;
        }

        if (i % 10000 == 0 && i > 0) {
            std::cout << "  Tried " << i << " nonces..." << std::endl;
        }
    }

    std::cout << std::endl;
    std::cout << "Found " << valid_count << " valid hashes out of 100,000 tries" << std::endl;
    std::cout << "Expected: ~" << (100000.0 / 6695.0) << " valid hashes" << std::endl;
    std::cout << "Best hash word[7]: 0x" << std::hex << best_hash_word7 << std::dec << std::endl;

    // Cleanup
    randomx_destroy_vm(vm);
    randomx_release_cache(cache);

    return 0;
}
