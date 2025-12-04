#include "src/miner.h"
#include "src/utils.h"
#include "randomx/randomx.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <algorithm>

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "COMPARING BLOCK TEMPLATE WITH BLOCK 1583" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    // Read the block template from /tmp/template.json
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

    std::cout << "BLOCK TEMPLATE DATA:" << std::endl;
    std::cout << "-------------------" << std::endl;
    std::cout << "Height: " << bt.height << std::endl;
    std::cout << "Version: " << bt.version << " (0x" << std::hex << bt.version << std::dec << ")" << std::endl;
    std::cout << "Previous hash: " << bt.previous_block_hash << std::endl;
    std::cout << "Merkle root: " << bt.merkle_root << std::endl;
    std::cout << "Block commitments: " << bt.block_commitments_hash << std::endl;
    std::cout << "Time: " << bt.time << " (0x" << std::hex << bt.time << std::dec << ")" << std::endl;
    std::cout << "Bits: 0x" << std::hex << bt.bits << std::dec << std::endl;
    std::cout << "Target: " << bt.target_hex << std::endl;
    std::cout << std::endl;

    // Show the header we construct (first 108 bytes)
    std::cout << "CONSTRUCTED HEADER (first 108 bytes):" << std::endl;
    std::cout << utils::bytes_to_hex(bt.header_base.data(), 108) << std::endl;
    std::cout << std::endl;

    // Byte-by-byte breakdown
    std::cout << "HEADER BREAKDOWN:" << std::endl;
    std::cout << "Bytes 0-3 (version):     " << utils::bytes_to_hex(bt.header_base.data(), 4) << std::endl;
    std::cout << "Bytes 4-35 (prev hash):  " << utils::bytes_to_hex(bt.header_base.data() + 4, 32) << std::endl;
    std::cout << "Bytes 36-67 (merkle):    " << utils::bytes_to_hex(bt.header_base.data() + 36, 32) << std::endl;
    std::cout << "Bytes 68-99 (commits):   " << utils::bytes_to_hex(bt.header_base.data() + 68, 32) << std::endl;
    std::cout << "Bytes 100-103 (time):    " << utils::bytes_to_hex(bt.header_base.data() + 100, 4) << std::endl;
    std::cout << "Bytes 104-107 (bits):    " << utils::bytes_to_hex(bt.header_base.data() + 104, 4) << std::endl;
    std::cout << std::endl;

    // Now compare with block 1583
    std::cout << "========================================" << std::endl;
    std::cout << "BLOCK 1583 REFERENCE DATA:" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    uint32_t ref_version = 4;
    std::string ref_prev_hash = "23d39ee3ec4600c3f507230519a64ea5f6c444b22e85633a9526289127f4aa17";
    std::string ref_merkle = "cf56010cd2de6b1323a0b0cf5f8f7354a4fa41c492eae5861c7929f2673e4f8e";
    std::string ref_commits = "bf9cd388aa99b6d79402d285567ea326025936ef92d5a4c1ab7ae732acb942f5";
    uint32_t ref_time = 1760323089;
    uint32_t ref_bits = 0x1f09daa8;
    std::string ref_target = "0009daa800000000000000000000000000000000000000000000000000000000";

    std::cout << "Height: 1583" << std::endl;
    std::cout << "Version: " << ref_version << std::endl;
    std::cout << "Previous hash: " << ref_prev_hash << std::endl;
    std::cout << "Merkle root: " << ref_merkle << std::endl;
    std::cout << "Block commitments: " << ref_commits << std::endl;
    std::cout << "Time: " << ref_time << std::endl;
    std::cout << "Bits: 0x" << std::hex << ref_bits << std::dec << std::endl;
    std::cout << "Target: " << ref_target << std::endl;
    std::cout << std::endl;

    // Build reference header
    std::vector<uint8_t> ref_header(140);
    utils::write_le32(&ref_header[0], ref_version);

    std::vector<uint8_t> prev_hash = utils::hex_to_bytes(ref_prev_hash);
    std::reverse(prev_hash.begin(), prev_hash.end());
    std::copy(prev_hash.begin(), prev_hash.end(), ref_header.begin() + 4);

    std::vector<uint8_t> merkle = utils::hex_to_bytes(ref_merkle);
    std::reverse(merkle.begin(), merkle.end());
    std::copy(merkle.begin(), merkle.end(), ref_header.begin() + 36);

    std::vector<uint8_t> commits = utils::hex_to_bytes(ref_commits);
    std::reverse(commits.begin(), commits.end());
    std::copy(commits.begin(), commits.end(), ref_header.begin() + 68);

    utils::write_le32(&ref_header[100], ref_time);
    utils::write_le32(&ref_header[104], ref_bits);

    std::cout << "REFERENCE HEADER (first 108 bytes):" << std::endl;
    std::cout << utils::bytes_to_hex(ref_header.data(), 108) << std::endl;
    std::cout << std::endl;

    // Compare key fields
    std::cout << "========================================" << std::endl;
    std::cout << "COMPARISON:" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    if (bt.height == 1583) {
        std::cout << "NOTE: Template IS for block 1583 - direct comparison possible!" << std::endl;
        std::cout << std::endl;

        // Compare each field
        bool match = true;

        if (bt.version != ref_version) {
            std::cout << "VERSION MISMATCH: " << bt.version << " vs " << ref_version << std::endl;
            match = false;
        } else {
            std::cout << "Version matches" << std::endl;
        }

        if (bt.previous_block_hash != ref_prev_hash) {
            std::cout << "PREV HASH MISMATCH" << std::endl;
            match = false;
        } else {
            std::cout << "Previous hash matches" << std::endl;
        }

        if (bt.merkle_root != ref_merkle) {
            std::cout << "MERKLE ROOT MISMATCH" << std::endl;
            std::cout << "  Template: " << bt.merkle_root << std::endl;
            std::cout << "  Block:    " << ref_merkle << std::endl;
            match = false;
        } else {
            std::cout << "Merkle root matches" << std::endl;
        }

        if (bt.block_commitments_hash != ref_commits) {
            std::cout << "BLOCK COMMITMENTS MISMATCH" << std::endl;
            std::cout << "  Template: " << bt.block_commitments_hash << std::endl;
            std::cout << "  Block:    " << ref_commits << std::endl;
            match = false;
        } else {
            std::cout << "Block commitments match" << std::endl;
        }

        if (bt.time != ref_time) {
            std::cout << "TIME DIFFERENT: " << bt.time << " vs " << ref_time << " (expected for template)" << std::endl;
        } else {
            std::cout << "Time matches" << std::endl;
        }

        if (bt.bits != ref_bits) {
            std::cout << "BITS MISMATCH: 0x" << std::hex << bt.bits << " vs 0x" << ref_bits << std::dec << std::endl;
            match = false;
        } else {
            std::cout << "Bits match" << std::endl;
        }

        if (bt.target_hex != ref_target) {
            std::cout << "TARGET MISMATCH" << std::endl;
            std::cout << "  Template: " << bt.target_hex << std::endl;
            std::cout << "  Block:    " << ref_target << std::endl;
            match = false;
        } else {
            std::cout << "Target matches" << std::endl;
        }

        std::cout << std::endl;
        if (match) {
            std::cout << "ALL FIELDS MATCH (except time which is OK)" << std::endl;
        } else {
            std::cout << "MISMATCHES FOUND" << std::endl;
        }
    } else {
        std::cout << "NOTE: Template is for block " << bt.height << ", not 1583" << std::endl;
        std::cout << "Cannot do direct comparison, but can verify structure." << std::endl;
    }

    std::cout << std::endl;

    // Test a simple hash with nonce 0 to see what we get
    std::cout << "========================================" << std::endl;
    std::cout << "TEST HASH WITH NONCE=0:" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    randomx_flags flags = randomx_get_flags();
    randomx_cache* cache = randomx_alloc_cache(flags);
    const char* key = "ZcashRandomXPoW";
    randomx_init_cache(cache, key, strlen(key));
    randomx_vm* vm = randomx_create_vm(flags, cache, nullptr);

    std::vector<uint8_t> hash_input(140);
    std::copy(bt.header_base.begin(), bt.header_base.begin() + 108, hash_input.begin());

    // Nonce = 0 (32 bytes of zeros)
    std::vector<uint8_t> nonce(32, 0);
    std::copy(nonce.begin(), nonce.end(), hash_input.begin() + 108);

    uint8_t hash[32];
    randomx_calculate_hash(vm, hash_input.data(), hash_input.size(), hash);

    std::cout << "Hash with nonce=0: " << utils::bytes_to_hex(hash, 32) << std::endl;
    std::cout << "Target:            " << bt.target_hex << std::endl;

    bool meets = utils::hash_meets_target(hash, bt.target);
    std::cout << "Meets target: " << (meets ? "YES" : "NO") << std::endl;
    std::cout << std::endl;

    // Calculate target difficulty
    uint32_t target_word = utils::read_le32(bt.target.data() + 28);
    std::cout << "Target high word: 0x" << std::hex << target_word << std::dec << std::endl;
    std::cout << "Approximate difficulty: " << (0x0009daa8 * 1.0) << std::endl;
    std::cout << "Probability of success per hash: ~" << (1.0 / 6695.0) << std::endl;
    std::cout << std::endl;

    randomx_destroy_vm(vm);
    randomx_release_cache(cache);

    return 0;
}
