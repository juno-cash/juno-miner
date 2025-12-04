#include "miner.h"
#include "rpc_client.h"
#include "utils.h"
#include "logger.h"
#include <iostream>
#include <iomanip>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <sstream>

#ifdef HAVE_NUMA
#include <numa.h>
#include <numaif.h>
#include <sched.h>
#endif

Miner::Miner(unsigned int num_threads, bool fast_mode)
    : num_threads_(num_threads)
    , fast_mode_(fast_mode)
    , dataset_(nullptr)
    , numa_available_(false)
    , num_numa_nodes_(0)
    , legacy_cache_(nullptr)
    , mining_(false)
    , found_(false)
    , hash_count_(0) {
    detect_numa_topology();
}

void Miner::detect_numa_topology() {
#ifdef HAVE_NUMA
    if (numa_available() == -1) {
        std::cout << "NUMA not available on this system, using single-node mode" << std::endl;
        numa_available_ = false;
        num_numa_nodes_ = 1;
        return;
    }

    num_numa_nodes_ = numa_num_configured_nodes();
    if (num_numa_nodes_ <= 1) {
        std::cout << "Single NUMA node detected, using standard mode" << std::endl;
        numa_available_ = false;
        num_numa_nodes_ = 1;
        return;
    }

    numa_available_ = true;
    numa_nodes_.resize(num_numa_nodes_);

    std::cout << "NUMA topology detected: " << num_numa_nodes_ << " nodes" << std::endl;

    // Get CPUs for each NUMA node
    int total_cpus = numa_num_configured_cpus();
    for (int node = 0; node < num_numa_nodes_; node++) {
        numa_nodes_[node].node_id = node;
        struct bitmask* cpumask = numa_allocate_cpumask();
        if (numa_node_to_cpus(node, cpumask) == 0) {
            for (int cpu = 0; cpu < total_cpus; cpu++) {
                if (numa_bitmask_isbitset(cpumask, cpu)) {
                    numa_nodes_[node].cpu_ids.push_back(cpu);
                }
            }
        }
        numa_free_cpumask(cpumask);
        std::cout << "  Node " << node << ": " << numa_nodes_[node].cpu_ids.size() << " CPUs" << std::endl;
    }

    // Distribute threads across NUMA nodes and assign to specific CPUs
    thread_to_cpu_.resize(num_threads_);
    thread_to_node_.resize(num_threads_);

    // Round-robin distribution across nodes, then within each node
    std::vector<size_t> node_thread_count(num_numa_nodes_, 0);
    for (unsigned int t = 0; t < num_threads_; t++) {
        int node = t % num_numa_nodes_;
        size_t cpu_index = node_thread_count[node] % numa_nodes_[node].cpu_ids.size();
        thread_to_node_[t] = node;
        thread_to_cpu_[t] = numa_nodes_[node].cpu_ids[cpu_index];
        node_thread_count[node]++;
    }

    LOG_DEBUG_STREAM("NUMA: Distributed " << num_threads_ << " threads across " << num_numa_nodes_ << " nodes");
#else
    numa_available_ = false;
    num_numa_nodes_ = 1;
    std::cout << "NUMA support not compiled in, using single-node mode" << std::endl;
#endif
}

bool Miner::set_thread_affinity(int cpu_id) {
#ifdef HAVE_NUMA
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        LOG_ERROR_STREAM("Failed to set thread affinity to CPU " << cpu_id << ": " << strerror(rc));
        return false;
    }
    return true;
#else
    (void)cpu_id;
    return false;
#endif
}

randomx_vm* Miner::get_vm_for_thread(int thread_id) {
    if (numa_available_ && thread_id < (int)thread_to_node_.size()) {
        int node = thread_to_node_[thread_id];
        // Find this thread's VM index within its node
        int vm_index = 0;
        for (int t = 0; t < thread_id; t++) {
            if (thread_to_node_[t] == node) {
                vm_index++;
            }
        }
        if (node < (int)numa_nodes_.size() && vm_index < (int)numa_nodes_[node].vms.size()) {
            return numa_nodes_[node].vms[vm_index];
        }
    }
    // Fallback to legacy
    if (thread_id < (int)legacy_vms_.size()) {
        return legacy_vms_[thread_id];
    }
    return nullptr;
}

Miner::~Miner() {
    stop();

    // Clean up NUMA-aware resources
    for (auto& node : numa_nodes_) {
        for (auto vm : node.vms) {
            if (vm) {
                randomx_destroy_vm(vm);
            }
        }
        node.vms.clear();
        if (node.cache) {
            randomx_release_cache(node.cache);
            node.cache = nullptr;
        }
    }
    numa_nodes_.clear();

    // Clean up legacy resources
    for (auto vm : legacy_vms_) {
        if (vm) {
            randomx_destroy_vm(vm);
        }
    }
    legacy_vms_.clear();

    if (legacy_cache_) {
        randomx_release_cache(legacy_cache_);
        legacy_cache_ = nullptr;
    }

    // Clean up dataset (fast mode)
    if (dataset_) {
        randomx_release_dataset(dataset_);
        dataset_ = nullptr;
    }
}

bool Miner::initialize(const std::vector<uint8_t>& seed_hash) {
    std::string mode_str = fast_mode_ ? "FAST (full dataset)" : "LIGHT (cache only)";
    std::cout << "Initializing RandomX in " << mode_str << " mode..." << std::endl;
    LOG_DEBUG_STREAM("Initializing RandomX with seed: " << utils::bytes_to_hex(seed_hash.data(), 32));
    LOG_DEBUG_STREAM("Mode: " << mode_str);

    if (seed_hash.size() != 32) {
        std::cerr << "Invalid seed hash size: " << seed_hash.size() << " (expected 32)" << std::endl;
        LOG_ERROR_STREAM("Invalid seed hash size: " << seed_hash.size());
        return false;
    }

    // Get recommended flags (matching daemon's approach exactly)
    randomx_flags flags = randomx_get_flags();
    flags |= RANDOMX_FLAG_JIT;  // Explicitly enable JIT like daemon does

    // Add FULL_MEM flag for fast mode
    randomx_flags vm_flags = flags;
    if (fast_mode_) {
        vm_flags |= RANDOMX_FLAG_FULL_MEM;
    }
    LOG_DEBUG_STREAM("RandomX flags: " << std::hex << flags << " (VM flags: " << vm_flags << ")" << std::dec);

    current_seed_hash_ = seed_hash;

    // In fast mode, we need a single shared dataset (not per-NUMA-node)
    // First allocate cache (needed to initialize dataset)
    legacy_cache_ = randomx_alloc_cache(flags);
    if (!legacy_cache_) {
        std::cerr << "Failed to allocate RandomX cache" << std::endl;
        LOG_ERROR("Failed to allocate RandomX cache");
        return false;
    }
    LOG_DEBUG("RandomX cache allocated");

    // Initialize cache with seed
    randomx_init_cache(legacy_cache_, seed_hash.data(), seed_hash.size());
    LOG_DEBUG("RandomX cache initialized with seed");

    if (fast_mode_) {
        // Fast mode: allocate and initialize the full dataset (~2GB)
        std::cout << "Allocating RandomX dataset (~2GB)..." << std::endl;
        dataset_ = randomx_alloc_dataset(flags);
        if (!dataset_) {
            std::cerr << "Failed to allocate RandomX dataset (need ~2GB RAM)" << std::endl;
            LOG_ERROR("Failed to allocate RandomX dataset");
            randomx_release_cache(legacy_cache_);
            legacy_cache_ = nullptr;
            return false;
        }
        LOG_DEBUG("RandomX dataset allocated");

        // Initialize dataset from cache using multiple threads for speed
        std::cout << "Initializing RandomX dataset (this may take a moment)..." << std::endl;
        unsigned long item_count = randomx_dataset_item_count();
        unsigned int init_threads = std::min(num_threads_, (unsigned int)std::thread::hardware_concurrency());
        if (init_threads == 0) init_threads = 1;

        // Parallel dataset initialization
        std::vector<std::thread> init_threads_vec;
        unsigned long items_per_thread = item_count / init_threads;
        unsigned long remainder = item_count % init_threads;

        for (unsigned int t = 0; t < init_threads; t++) {
            unsigned long start = t * items_per_thread;
            unsigned long count = items_per_thread;
            if (t == init_threads - 1) {
                count += remainder;  // Last thread handles remainder
            }
            init_threads_vec.emplace_back([this, start, count]() {
                randomx_init_dataset(dataset_, legacy_cache_, start, count);
            });
        }

        // Wait for all init threads to complete
        for (auto& t : init_threads_vec) {
            t.join();
        }
        LOG_DEBUG_STREAM("RandomX dataset initialized with " << init_threads << " threads");
        std::cout << "Dataset initialization complete" << std::endl;
    }

#ifdef HAVE_NUMA
    if (numa_available_ && !fast_mode_) {
        // NUMA-aware cache mode (only in light mode - fast mode uses shared dataset)
        std::cout << "Initializing NUMA-aware RandomX (" << num_numa_nodes_ << " nodes)..." << std::endl;

        // Count threads per node
        std::vector<int> threads_per_node(num_numa_nodes_, 0);
        for (unsigned int t = 0; t < num_threads_; t++) {
            threads_per_node[thread_to_node_[t]]++;
        }

        // Allocate cache and VMs for each NUMA node
        for (int node = 0; node < num_numa_nodes_; node++) {
            if (threads_per_node[node] == 0) {
                continue;  // No threads assigned to this node
            }

            // Bind memory allocation to this NUMA node
            numa_set_preferred(node);

            // Allocate cache on this node
            numa_nodes_[node].cache = randomx_alloc_cache(flags);
            if (!numa_nodes_[node].cache) {
                std::cerr << "Failed to allocate RandomX cache on NUMA node " << node << std::endl;
                LOG_ERROR_STREAM("Failed to allocate RandomX cache on NUMA node " << node);
                return false;
            }

            // Initialize cache with seed
            randomx_init_cache(numa_nodes_[node].cache, seed_hash.data(), seed_hash.size());

            // Create VMs for threads on this node
            numa_nodes_[node].vms.resize(threads_per_node[node]);
            for (int v = 0; v < threads_per_node[node]; v++) {
                numa_nodes_[node].vms[v] = randomx_create_vm(vm_flags, numa_nodes_[node].cache, nullptr);
                if (!numa_nodes_[node].vms[v]) {
                    std::cerr << "Failed to create RandomX VM on NUMA node " << node << std::endl;
                    LOG_ERROR_STREAM("Failed to create RandomX VM on NUMA node " << node);
                    return false;
                }
            }

            std::cout << "  Node " << node << ": cache + " << threads_per_node[node] << " VMs allocated" << std::endl;
        }

        // Reset NUMA policy to default
        numa_set_preferred(-1);

        std::cout << "NUMA-aware RandomX initialization complete (" << num_threads_ << " threads across "
                  << num_numa_nodes_ << " nodes)" << std::endl;
        return true;
    }
#endif

    // Create VMs for each thread (non-NUMA path or fast mode)
    legacy_vms_.resize(num_threads_);

    for (unsigned int i = 0; i < num_threads_; i++) {
        if (fast_mode_) {
            // Fast mode: VMs use dataset, cache can be NULL
            legacy_vms_[i] = randomx_create_vm(vm_flags, nullptr, dataset_);
        } else {
            // Light mode: VMs use cache, dataset is NULL
            legacy_vms_[i] = randomx_create_vm(vm_flags, legacy_cache_, nullptr);
        }
        if (!legacy_vms_[i]) {
            std::cerr << "Failed to create RandomX VM #" << i << std::endl;
            LOG_ERROR_STREAM("Failed to create RandomX VM #" << i);
            return false;
        }
    }
    LOG_DEBUG_STREAM("Created " << num_threads_ << " RandomX VMs");

    std::cout << "RandomX initialization complete (" << num_threads_ << " threads, " << mode_str << ")" << std::endl;
    return true;
}

void Miner::worker_thread(int thread_id, const BlockTemplate& block_template) {
    // Set CPU affinity if NUMA is available
    if (numa_available_ && thread_id < (int)thread_to_cpu_.size()) {
        int cpu_id = thread_to_cpu_[thread_id];
        if (set_thread_affinity(cpu_id)) {
            LOG_DEBUG_STREAM("Thread " << thread_id << " pinned to CPU " << cpu_id
                           << " (NUMA node " << thread_to_node_[thread_id] << ")");
        }
    }

    // Get the VM for this thread (NUMA-aware or legacy)
    randomx_vm* vm = get_vm_for_thread(thread_id);
    if (!vm) {
        LOG_ERROR_STREAM("No VM available for thread " << thread_id);
        return;
    }

    // Following the exact approach of the internal miner (src/miner.cpp:915-918):
    // 1. Serialize CEquihashInput (header without nonce/solution): version(4) + prevhash(32) +
    //    merkleroot(32) + commitments(32) + time(4) + bits(4) = 108 bytes
    // 2. Append nNonce (32 bytes)
    // 3. Hash the combined 140 bytes

    // Extract header_base (first 108 bytes = header without nonce)
    std::vector<uint8_t> header_without_nonce(block_template.header_base.begin(),
                                              block_template.header_base.begin() + 108);

    // Initialize nonce exactly like the node's internal miner:
    // 1. Generate random 256-bit nonce
    // 2. Clear bottom 16 bits (bytes 0-1) and top 16 bits (bytes 30-31)
    // This gives 224 random bits, making collisions between threads/instances
    // astronomically unlikely (~10^-47 probability)
    std::vector<uint8_t> nonce(32, 0);
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    if (urandom) {
        urandom.read(reinterpret_cast<char*>(nonce.data()), 32);
    }
    // Clear bottom 16 bits (bytes 0-1) and top 16 bits (bytes 30-31)
    // matching node's: nonce <<= 32; nonce >>= 16;
    nonce[0] = 0;
    nonce[1] = 0;
    nonce[30] = 0;
    nonce[31] = 0;

    // Buffer for full input: header (108 bytes) + nonce (32 bytes) = 140 bytes
    std::vector<uint8_t> hash_input(140);
    std::copy(header_without_nonce.begin(), header_without_nonce.end(), hash_input.begin());

    uint8_t hash[32];

    while (mining_.load() && !found_.load()) {
        // Copy nonce into hash input at position 108 (matching internal miner's approach)
        std::copy(nonce.begin(), nonce.end(), hash_input.begin() + 108);

        // Calculate RandomX hash (matching internal miner's RandomX_Hash_Block call)
        randomx_calculate_hash(vm, hash_input.data(), hash_input.size(), hash);

        // Increment hash count
        hash_count_.fetch_add(1);

        // Check if hash meets target (matching internal miner's UintToArith256(hash) <= hashTarget)
        if (utils::hash_meets_target(hash, block_template.target)) {
            // Found a solution!
            bool expected = false;
            if (found_.compare_exchange_strong(expected, true)) {
                // We're the first to find it
                solution_nonce_ = nonce;
                solution_hash_.assign(hash, hash + 32);

                // Store the full 140-byte header (header_without_nonce + nonce)
                std::vector<uint8_t> header_with_nonce(140);
                std::copy(header_without_nonce.begin(), header_without_nonce.end(), header_with_nonce.begin());
                std::copy(nonce.begin(), nonce.end(), header_with_nonce.begin() + 108);
                solution_header_ = header_with_nonce;
                solution_template_ = block_template;

                // Signal all threads to stop
                mining_ = false;
            }
            break;
        }

        // Increment full 256-bit nonce by 1 (same as node's internal miner)
        bool carry = true;
        for (int i = 0; i < 32 && carry; i++) {
            if (nonce[i] == 255) {
                nonce[i] = 0;
            } else {
                nonce[i]++;
                carry = false;
            }
        }
        // Full 256-bit overflow is astronomically unlikely and harmless
    }
}

void Miner::start_mining(const BlockTemplate& block_template) {
    // Stop any existing mining
    stop();

    LOG_DEBUG_STREAM("Starting mining: height=" << block_template.height
                    << " target=" << block_template.target_hex.substr(0, 16) << "...");

    mining_ = true;
    found_ = false;
    hash_count_ = 0;
    solution_nonce_.clear();
    solution_hash_.clear();
    solution_header_.clear();

    start_time_ = std::chrono::steady_clock::now();

    // Start worker threads
    threads_.clear();
    for (unsigned int i = 0; i < num_threads_; i++) {
        threads_.emplace_back(&Miner::worker_thread, this, i, std::cref(block_template));
    }
    LOG_DEBUG_STREAM("Started " << num_threads_ << " worker threads");
}

bool Miner::get_solution(std::vector<uint8_t>& solution_header, std::vector<uint8_t>& solution_hash, BlockTemplate& template_out) {
    // Wait for threads to finish if still mining
    if (mining_.load()) {
        stop();
    }

    if (found_.load() && !solution_header_.empty()) {
        solution_header = solution_header_;
        solution_hash = solution_hash_;
        template_out = solution_template_;
        return true;
    }

    return false;
}

void Miner::stop() {
    // Signal threads to stop
    mining_ = false;

    // Wait for all threads to finish with timeout
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    threads_.clear();
}

double Miner::get_hashrate() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
    if (elapsed == 0) return 0.0;
    return static_cast<double>(hash_count_.load()) / elapsed;
}

bool Miner::update_seed(const std::vector<uint8_t>& new_seed_hash) {
    LOG_DEBUG_STREAM("Updating RandomX seed: " << utils::bytes_to_hex(new_seed_hash.data(), 32));

    if (new_seed_hash.size() != 32) {
        std::cerr << "Invalid seed hash size: " << new_seed_hash.size() << " (expected 32)" << std::endl;
        LOG_ERROR_STREAM("Invalid seed hash size for update: " << new_seed_hash.size());
        return false;
    }

    // Check if seed actually changed
    if (new_seed_hash == current_seed_hash_) {
        LOG_DEBUG("Seed unchanged, skipping update");
        return true; // Nothing to do
    }

    // Stop mining if active
    bool was_mining = mining_.load();
    if (was_mining) {
        LOG_DEBUG("Stopping mining for seed update");
        stop();
    }

    randomx_flags flags = randomx_get_flags();
    flags |= RANDOMX_FLAG_JIT;

    randomx_flags vm_flags = flags;
    if (fast_mode_) {
        vm_flags |= RANDOMX_FLAG_FULL_MEM;
    }

#ifdef HAVE_NUMA
    if (numa_available_ && !fast_mode_) {
        // NUMA path only for light mode
        LOG_DEBUG("Reinitializing NUMA-aware RandomX caches with new seed");
        current_seed_hash_ = new_seed_hash;

        for (auto& node : numa_nodes_) {
            if (!node.cache) continue;

            // Reinitialize cache
            randomx_init_cache(node.cache, new_seed_hash.data(), new_seed_hash.size());

            // Recreate VMs
            for (size_t i = 0; i < node.vms.size(); i++) {
                if (node.vms[i]) {
                    randomx_destroy_vm(node.vms[i]);
                }
                node.vms[i] = randomx_create_vm(vm_flags, node.cache, nullptr);
                if (!node.vms[i]) {
                    std::cerr << "Failed to recreate RandomX VM on NUMA node " << node.node_id << std::endl;
                    return false;
                }
            }
        }
        return true;
    }
#endif

    // Legacy/fast mode path
    if (legacy_cache_) {
        LOG_DEBUG("Reinitializing RandomX cache with new seed");
        randomx_init_cache(legacy_cache_, new_seed_hash.data(), new_seed_hash.size());
        current_seed_hash_ = new_seed_hash;

        // In fast mode, reinitialize the dataset from the updated cache
        if (fast_mode_ && dataset_) {
            LOG_DEBUG("Reinitializing RandomX dataset with new seed");
            std::cout << "Reinitializing dataset for new epoch..." << std::endl;

            unsigned long item_count = randomx_dataset_item_count();
            unsigned int init_threads = std::min(num_threads_, (unsigned int)std::thread::hardware_concurrency());
            if (init_threads == 0) init_threads = 1;

            // Parallel dataset reinitialization
            std::vector<std::thread> init_threads_vec;
            unsigned long items_per_thread = item_count / init_threads;
            unsigned long remainder = item_count % init_threads;

            for (unsigned int t = 0; t < init_threads; t++) {
                unsigned long start = t * items_per_thread;
                unsigned long count = items_per_thread;
                if (t == init_threads - 1) {
                    count += remainder;
                }
                init_threads_vec.emplace_back([this, start, count]() {
                    randomx_init_dataset(dataset_, legacy_cache_, start, count);
                });
            }

            for (auto& t : init_threads_vec) {
                t.join();
            }
            LOG_DEBUG("RandomX dataset reinitialized");
            std::cout << "Dataset reinitialization complete" << std::endl;

            // In fast mode, just update the dataset reference in VMs
            for (size_t i = 0; i < legacy_vms_.size(); i++) {
                if (legacy_vms_[i]) {
                    randomx_vm_set_dataset(legacy_vms_[i], dataset_);
                }
            }
        } else {
            // Light mode: recreate VMs with new cache
            LOG_DEBUG_STREAM("Recreating " << legacy_vms_.size() << " RandomX VMs");
            for (size_t i = 0; i < legacy_vms_.size(); i++) {
                if (legacy_vms_[i]) {
                    randomx_destroy_vm(legacy_vms_[i]);
                }
                legacy_vms_[i] = randomx_create_vm(vm_flags, legacy_cache_, nullptr);
                if (!legacy_vms_[i]) {
                    std::cerr << "Failed to recreate RandomX VM #" << i << std::endl;
                    return false;
                }
            }
        }
        return true;
    }

    std::cerr << "Failed to update seed: cache not initialized" << std::endl;
    return false;
}

bool Miner::set_thread_count(unsigned int new_thread_count) {
    if (new_thread_count == 0) {
        std::cerr << "Thread count must be at least 1" << std::endl;
        return false;
    }

    if (new_thread_count == num_threads_) {
        return true; // Nothing to change
    }

    // Stop mining if active
    bool was_mining = mining_.load();
    if (was_mining) {
        stop();
    }

    // Save current seed hash for re-initialization
    std::vector<uint8_t> saved_seed = current_seed_hash_;

    // Clean up all existing resources
#ifdef HAVE_NUMA
    if (numa_available_) {
        for (auto& node : numa_nodes_) {
            for (auto vm : node.vms) {
                if (vm) randomx_destroy_vm(vm);
            }
            node.vms.clear();
            if (node.cache) {
                randomx_release_cache(node.cache);
                node.cache = nullptr;
            }
        }
    }
#endif

    for (auto vm : legacy_vms_) {
        if (vm) randomx_destroy_vm(vm);
    }
    legacy_vms_.clear();

    if (legacy_cache_) {
        randomx_release_cache(legacy_cache_);
        legacy_cache_ = nullptr;
    }

    // Clean up dataset (fast mode) - will be recreated by initialize()
    if (dataset_) {
        randomx_release_dataset(dataset_);
        dataset_ = nullptr;
    }

    // Update thread count and re-detect NUMA topology for new distribution
    num_threads_ = new_thread_count;

#ifdef HAVE_NUMA
    if (numa_available_) {
        // Redistribute threads across NUMA nodes
        thread_to_cpu_.resize(num_threads_);
        thread_to_node_.resize(num_threads_);

        std::vector<size_t> node_thread_count(num_numa_nodes_, 0);
        for (unsigned int t = 0; t < num_threads_; t++) {
            int node = t % num_numa_nodes_;
            size_t cpu_index = node_thread_count[node] % numa_nodes_[node].cpu_ids.size();
            thread_to_node_[t] = node;
            thread_to_cpu_[t] = numa_nodes_[node].cpu_ids[cpu_index];
            node_thread_count[node]++;
        }
    }
#endif

    // Re-initialize with the saved seed
    if (!saved_seed.empty()) {
        return initialize(saved_seed);
    }

    return true;
}

// Rewritten from scratch to exactly match the internal miner's approach:
// Internal miner (src/miner.cpp:914-918):
//   1. CEquihashInput I{*pblock};        // Block header without nonce/solution
//   2. CDataStream ss << I << nNonce;     // Serialize: header(108) + nonce(32) = 140 bytes
//   3. RandomX_Hash_Block(ss.data(), ss.size(), hash);
//
// CEquihashInput serializes (src/primitives/block.h):
//   - nVersion (4 bytes)
//   - hashPrevBlock (32 bytes)
//   - hashMerkleRoot (32 bytes)
//   - hashBlockCommitments (32 bytes)
//   - nTime (4 bytes)
//   - nBits (4 bytes)
//   Total: 108 bytes
//
// All uint256 hashes are serialized in INTERNAL byte order (little-endian storage).
// When getblocktemplate returns hex strings, we must determine which format each field uses:
//   - previousblockhash: DISPLAY order (GetHex()) - must REVERSE
//   - merkleroot: DISPLAY order (GetHex()) - must REVERSE
//   - blockcommitmentshash: DISPLAY order (GetHex()) - must REVERSE
//   - randomxseedhash: INTERNAL order (HexStr(begin, end)) - use AS-IS

BlockTemplate parse_block_template(const Json::Value& template_data) {
    BlockTemplate bt;

    // Parse template fields
    if (!template_data.isMember("version")) {
        throw std::runtime_error("Missing version in block template");
    }
    bt.version = template_data["version"].asUInt();

    if (!template_data.isMember("previousblockhash")) {
        throw std::runtime_error("Missing previousblockhash in block template");
    }
    bt.previous_block_hash = template_data["previousblockhash"].asString();

    if (!template_data.isMember("curtime")) {
        throw std::runtime_error("Missing curtime in block template");
    }
    bt.time = template_data["curtime"].asUInt();

    if (!template_data.isMember("bits")) {
        throw std::runtime_error("Missing bits in block template");
    }
    bt.bits = std::stoul(template_data["bits"].asString(), nullptr, 16);

    if (!template_data.isMember("height")) {
        throw std::runtime_error("Missing height in block template");
    }
    bt.height = template_data["height"].asUInt();

    // Parse RandomX seed information
    if (!template_data.isMember("randomxseedheight")) {
        throw std::runtime_error("Missing randomxseedheight in block template");
    }
    bt.seed_height = template_data["randomxseedheight"].asUInt64();

    if (!template_data.isMember("randomxseedhash")) {
        throw std::runtime_error("Missing randomxseedhash in block template");
    }
    std::string seed_hash_hex = template_data["randomxseedhash"].asString();
    if (seed_hash_hex.length() != 64) {
        throw std::runtime_error("Invalid randomxseedhash length");
    }
    // Seed hash is in INTERNAL order (from HexStr(begin, end)), use as-is
    bt.seed_hash = utils::hex_to_bytes(seed_hash_hex);

    // Parse next seed hash if available
    if (template_data.isMember("randomxnextseedhash")) {
        std::string next_seed_hex = template_data["randomxnextseedhash"].asString();
        if (next_seed_hex.length() == 64) {
            bt.next_seed_hash = utils::hex_to_bytes(next_seed_hex);
        }
    }

    // Parse target
    bt.target = utils::compact_to_target(bt.bits);
    if (template_data.isMember("target")) {
        bt.target_hex = template_data["target"].asString();
    }

    // Parse merkle root from defaultroots
    if (template_data.isMember("defaultroots") &&
        template_data["defaultroots"].isMember("merkleroot")) {
        bt.merkle_root = template_data["defaultroots"]["merkleroot"].asString();
    } else {
        throw std::runtime_error("Missing defaultroots.merkleroot in block template");
    }
    if (bt.merkle_root.length() != 64) {
        throw std::runtime_error("Invalid merkleroot length");
    }

    // Parse block commitments hash from defaultroots
    if (template_data.isMember("defaultroots") &&
        template_data["defaultroots"].isMember("blockcommitmentshash")) {
        bt.block_commitments_hash = template_data["defaultroots"]["blockcommitmentshash"].asString();
    } else if (template_data.isMember("blockcommitmentshash")) {
        bt.block_commitments_hash = template_data["blockcommitmentshash"].asString();
    } else {
        throw std::runtime_error("Missing blockcommitmentshash in block template");
    }
    if (bt.block_commitments_hash.length() != 64) {
        throw std::runtime_error("Invalid blockcommitmentshash length");
    }

    // Parse coinbase transaction
    if (template_data.isMember("coinbasetxn") &&
        template_data["coinbasetxn"].isMember("data")) {
        bt.coinbase_txn_hex = template_data["coinbasetxn"]["data"].asString();
    } else {
        throw std::runtime_error("Missing coinbasetxn.data in block template");
    }

    // Parse other transactions
    if (template_data.isMember("transactions") && template_data["transactions"].isArray()) {
        const Json::Value& txns = template_data["transactions"];
        for (Json::ArrayIndex i = 0; i < txns.size(); i++) {
            if (txns[i].isMember("data")) {
                bt.txn_hex.push_back(txns[i]["data"].asString());
            }
        }
    }

    // Build the 140-byte block header exactly as CEquihashInput does when serialized
    // This MUST match the daemon's CBlockHeader serialization format EXACTLY.
    //
    // CEquihashInput::SerializationOp (src/primitives/block.h:159-166):
    //   READWRITE(nVersion);                 // 4 bytes, little-endian
    //   READWRITE(hashPrevBlock);            // 32 bytes, internal order
    //   READWRITE(hashMerkleRoot);           // 32 bytes, internal order
    //   READWRITE(hashBlockCommitments);     // 32 bytes, internal order
    //   READWRITE(nTime);                    // 4 bytes, little-endian
    //   READWRITE(nBits);                    // 4 bytes, little-endian
    //
    // Total: 108 bytes
    // Then nNonce is appended: 32 bytes
    // Grand total: 140 bytes

    bt.header_base.resize(140);
    size_t offset = 0;

    // nVersion (4 bytes, little-endian)
    utils::write_le32(&bt.header_base[offset], bt.version);
    offset += 4;

    // hashPrevBlock (32 bytes, internal order)
    // getblocktemplate returns this in DISPLAY order, so REVERSE it
    std::vector<uint8_t> prev_hash_bytes = utils::hex_to_bytes(bt.previous_block_hash);
    if (prev_hash_bytes.size() != 32) {
        throw std::runtime_error("Invalid previousblockhash size");
    }
    std::reverse(prev_hash_bytes.begin(), prev_hash_bytes.end());
    std::copy(prev_hash_bytes.begin(), prev_hash_bytes.end(), bt.header_base.begin() + offset);
    offset += 32;

    // hashMerkleRoot (32 bytes, internal order)
    // getblocktemplate returns this in DISPLAY order, so REVERSE it
    std::vector<uint8_t> merkle_bytes = utils::hex_to_bytes(bt.merkle_root);
    if (merkle_bytes.size() != 32) {
        throw std::runtime_error("Invalid merkleroot size");
    }
    std::reverse(merkle_bytes.begin(), merkle_bytes.end());
    std::copy(merkle_bytes.begin(), merkle_bytes.end(), bt.header_base.begin() + offset);
    offset += 32;

    // hashBlockCommitments (32 bytes, internal order)
    // getblocktemplate returns this in DISPLAY order, so REVERSE it
    std::vector<uint8_t> commitments_bytes = utils::hex_to_bytes(bt.block_commitments_hash);
    if (commitments_bytes.size() != 32) {
        throw std::runtime_error("Invalid blockcommitmentshash size");
    }
    std::reverse(commitments_bytes.begin(), commitments_bytes.end());
    std::copy(commitments_bytes.begin(), commitments_bytes.end(), bt.header_base.begin() + offset);
    offset += 32;

    // nTime (4 bytes, little-endian)
    utils::write_le32(&bt.header_base[offset], bt.time);
    offset += 4;

    // nBits (4 bytes, little-endian)
    utils::write_le32(&bt.header_base[offset], bt.bits);
    offset += 4;

    // Verify we're at offset 108 (CEquihashInput size)
    if (offset != 108) {
        throw std::runtime_error("Internal error: CEquihashInput size mismatch");
    }

    // nNonce (32 bytes) will be filled by miner threads at offset 108-139
    // For now, zero it out
    std::fill(bt.header_base.begin() + offset, bt.header_base.end(), 0);

    return bt;
}
