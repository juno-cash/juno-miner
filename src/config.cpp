#include "config.h"
#include <iostream>
#include <cstring>
#include <cstdlib>

void print_usage(const char* program_name) {
    std::cout << "Juno Cash RandomX Miner" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage: " << program_name << " [OPTIONS]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --rpc-url URL          RPC server URL (default: http://127.0.0.1:8232)" << std::endl;
    std::cout << "  --rpc-user USER        RPC username" << std::endl;
    std::cout << "  --rpc-password PASS    RPC password" << std::endl;
    std::cout << "  --threads N            Number of mining threads (default: auto-detect)" << std::endl;
    std::cout << "  --update-interval N    Stats update interval in seconds (default: 5)" << std::endl;
    std::cout << "  --block-check N        Block check interval in seconds (default: 2)" << std::endl;
    std::cout << "  --zmq-url URL          ZMQ endpoint for instant block notifications (e.g., tcp://127.0.0.1:28332)" << std::endl;
    std::cout << "  --fast-mode            Use full RandomX dataset (~2GB shared) for 2x hashrate" << std::endl;
    std::cout << "  --no-balance           Skip wallet balance checks (don't query or display balance)" << std::endl;
    std::cout << "  --debug                Enable debug logging" << std::endl;
    std::cout << "  --log-file FILE        Write debug logs to file (default: juno-miner.log)" << std::endl;
    std::cout << "  --log-console          Write debug logs to console (in addition to UI)" << std::endl;
    std::cout << "  --help                 Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Example:" << std::endl;
    std::cout << "  " << program_name << " --rpc-user miner --rpc-password secret --threads 4" << std::endl;
    std::cout << std::endl;
    std::cout << "The miner will automatically:" << std::endl;
    std::cout << "  - Detect available RAM and CPU cores" << std::endl;
    std::cout << "  - Calculate optimal thread count (if not specified)" << std::endl;
    std::cout << "  - Handle RandomX epoch transitions" << std::endl;
    std::cout << "  - Submit found blocks to the node" << std::endl;
    std::cout << std::endl;
}

bool parse_config(int argc, char* argv[], MinerConfig& config) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return false;
        } else if (arg == "--rpc-url") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --rpc-url requires an argument" << std::endl;
                return false;
            }
            config.rpc_url = argv[++i];
        } else if (arg == "--rpc-user") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --rpc-user requires an argument" << std::endl;
                return false;
            }
            config.rpc_user = argv[++i];
        } else if (arg == "--rpc-password") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --rpc-password requires an argument" << std::endl;
                return false;
            }
            config.rpc_password = argv[++i];
        } else if (arg == "--threads") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --threads requires an argument" << std::endl;
                return false;
            }
            config.num_threads = std::atoi(argv[++i]);
            config.auto_threads = false;
            if (config.num_threads == 0) {
                std::cerr << "Error: invalid thread count" << std::endl;
                return false;
            }
        } else if (arg == "--update-interval") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --update-interval requires an argument" << std::endl;
                return false;
            }
            config.update_interval_seconds = std::atoi(argv[++i]);
            if (config.update_interval_seconds == 0) {
                std::cerr << "Error: invalid update interval" << std::endl;
                return false;
            }
        } else if (arg == "--block-check") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --block-check requires an argument" << std::endl;
                return false;
            }
            config.block_check_interval_seconds = std::atoi(argv[++i]);
            if (config.block_check_interval_seconds == 0) {
                std::cerr << "Error: invalid block check interval" << std::endl;
                return false;
            }
        } else if (arg == "--zmq-url") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --zmq-url requires an argument" << std::endl;
                return false;
            }
            config.zmq_url = argv[++i];
        } else if (arg == "--fast-mode") {
            config.fast_mode = true;
        } else if (arg == "--no-balance") {
            config.no_balance = true;
        } else if (arg == "--debug") {
            config.debug_mode = true;
            if (config.log_file.empty()) {
                config.log_file = "juno-miner.log";
            }
        } else if (arg == "--log-file") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --log-file requires an argument" << std::endl;
                return false;
            }
            config.log_file = argv[++i];
        } else if (arg == "--log-console") {
            config.log_to_console = true;
        } else {
            std::cerr << "Error: unknown option: " << arg << std::endl;
            std::cerr << "Use --help for usage information" << std::endl;
            return false;
        }
    }

    return true;
}
