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

    std::cout << "Mining test for block " << bt.height << std::endl;
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

    // Try mining for 10000 nonces
    std::vector<uint8_t> nonce(32, 0);
    uint8_t hash[32];

    std::cout << "Trying 10000 nonces..." << std::endl;
    int valid_count = 0;

    for (int i = 0; i < 10000; i++) {
        // Set nonce
        std::fill(nonce.begin(), nonce.end(), 0);
        nonce[0] = i & 0xFF;
        nonce[1] = (i >> 8) & 0xFF;

        // Reverse nonce like other hashes!
        std::vector<uint8_t> nonce_reversed(nonce.rbegin(), nonce.rend());
        std::copy(nonce_reversed.begin(), nonce_reversed.end(), hash_input.begin() + 108);

        // Hash
        randomx_calculate_hash(vm, hash_input.data(), hash_input.size(), hash);

        // Check against target
        bool meets = utils::hash_meets_target(hash, bt.target);

        if (meets) {
            std::cout << "VALID HASH FOUND!" << std::endl;
            std::cout << "  Nonce " << i << ": " << utils::bytes_to_hex(nonce.data(), 32) << std::endl;
            std::cout << "  Hash: " << utils::bytes_to_hex(hash, 32) << std::endl;
            valid_count++;
        }

        if (i % 1000 == 0) {
            std::cout << "  Tried " << i << " nonces..." << std::endl;
        }
    }

    std::cout << std::endl;
    std::cout << "Found " << valid_count << " valid hashes out of 10000 tries" << std::endl;
    std::cout << "Expected: ~" << (10000.0 / 6695.0) << " valid hashes" << std::endl;

    // Cleanup
    randomx_destroy_vm(vm);
    randomx_release_cache(cache);

    return 0;
}
