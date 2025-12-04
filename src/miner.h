#ifndef MINER_H
#define MINER_H

#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <memory>
#include <cstdint>
#include <json/json.h>
#include "randomx.h"

#ifdef HAVE_NUMA
#include <numa.h>
#include <sched.h>
#endif

// RandomX epoch configuration (matches Juno Moneta daemon)
// Epoch: 2048 blocks (matches Monero, power of 2 for efficient bitmask operations)
// Lag: 96 blocks (50% longer than Monero's 64, giving more advance notice)
static const uint64_t RANDOMX_SEEDHASH_EPOCH_BLOCKS = 2048;
static const uint64_t RANDOMX_SEEDHASH_EPOCH_LAG = 96;

// Calculate seed height for a given block height
inline uint64_t RandomX_SeedHeight(uint64_t height) {
    if (height <= RANDOMX_SEEDHASH_EPOCH_BLOCKS + RANDOMX_SEEDHASH_EPOCH_LAG) {
        return 0;
    }
    // Bitmask operation works efficiently since RANDOMX_SEEDHASH_EPOCH_BLOCKS (2048) is a power of 2
    // This rounds down to the nearest multiple of epochBlocks
    return (height - RANDOMX_SEEDHASH_EPOCH_LAG - 1) & ~(RANDOMX_SEEDHASH_EPOCH_BLOCKS - 1);
}

struct BlockTemplate {
    uint32_t version;
    std::string previous_block_hash;
    std::string merkle_root;
    std::string block_commitments_hash;
    uint32_t time;
    uint32_t bits;
    std::vector<uint8_t> target;      // 256-bit target (converted from bits)
    std::string target_hex;           // Hex string for display only
    uint32_t height;
    uint64_t seed_height;             // Height of seed block (NEW: from randomxseedheight)
    std::vector<uint8_t> seed_hash;   // RandomX seed hash (32 bytes, from randomxseedhash)
    std::vector<uint8_t> next_seed_hash; // Next epoch's seed (32 bytes, from randomxnextseedhash, optional)
    std::vector<uint8_t> header_base; // Header without nonce
    std::string coinbase_txn_hex;     // Coinbase transaction (hex)
    std::vector<std::string> txn_hex; // Other transactions (hex)
};

// NUMA node resources - each node gets its own cache and VMs for local memory access
struct NumaNodeResources {
    int node_id;
    randomx_cache* cache;
    std::vector<randomx_vm*> vms;
    std::vector<int> cpu_ids;  // CPUs belonging to this node

    NumaNodeResources() : node_id(-1), cache(nullptr) {}
};

class Miner {
public:
    Miner(unsigned int num_threads, bool fast_mode = false);
    ~Miner();

    bool initialize(const std::vector<uint8_t>& seed_hash);
    void start_mining(const BlockTemplate& block_template);
    void stop();
    bool is_mining() const { return mining_.load(); }
    bool get_solution(std::vector<uint8_t>& solution_header, std::vector<uint8_t>& solution_hash, BlockTemplate& template_out);

    // Seed management
    bool update_seed(const std::vector<uint8_t>& new_seed_hash);
    const std::vector<uint8_t>& get_current_seed() const { return current_seed_hash_; }

    // Statistics
    uint64_t get_hash_count() const { return hash_count_.load(); }
    double get_hashrate() const;

    // Thread management
    bool set_thread_count(unsigned int new_thread_count);
    unsigned int get_thread_count() const { return num_threads_; }

    // Mode info
    bool is_fast_mode() const { return fast_mode_; }

private:
    unsigned int num_threads_;
    bool fast_mode_;  // True = full dataset mode, False = light/cache mode
    std::vector<std::thread> threads_;
    std::vector<uint8_t> current_seed_hash_;

    // Dataset for fast mode (shared across all threads)
    randomx_dataset* dataset_;

    // NUMA-aware resources
    std::vector<NumaNodeResources> numa_nodes_;
    std::vector<int> thread_to_cpu_;      // Maps thread_id -> CPU id
    std::vector<int> thread_to_node_;     // Maps thread_id -> NUMA node index
    bool numa_available_;
    int num_numa_nodes_;

    // Legacy single-node fallback (used when NUMA not available)
    randomx_cache* legacy_cache_;
    std::vector<randomx_vm*> legacy_vms_;

    std::atomic<bool> mining_;
    std::atomic<bool> found_;
    std::atomic<uint64_t> hash_count_;

    std::vector<uint8_t> solution_nonce_;
    std::vector<uint8_t> solution_hash_;
    std::vector<uint8_t> solution_header_;
    BlockTemplate solution_template_;  // Store template for block serialization

    std::chrono::steady_clock::time_point start_time_;

    void worker_thread(int thread_id, const BlockTemplate& block_template);
    void detect_numa_topology();
    bool set_thread_affinity(int cpu_id);
    randomx_vm* get_vm_for_thread(int thread_id);
};

BlockTemplate parse_block_template(const Json::Value& template_data);

#endif // MINER_H
