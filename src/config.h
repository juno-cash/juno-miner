#ifndef CONFIG_H
#define CONFIG_H

#include <string>

struct MinerConfig {
    // RPC connection
    std::string rpc_url;
    std::string rpc_user;
    std::string rpc_password;

    // Mining parameters
    unsigned int num_threads;
    bool auto_threads;

    // Display settings
    unsigned int update_interval_seconds;

    // Block check interval (how often to check for new blocks)
    unsigned int block_check_interval_seconds;

    // Debug and logging
    bool debug_mode;
    std::string log_file;
    bool log_to_console;

    // RandomX mode
    bool fast_mode;  // Use full dataset (~2GB shared) for 2x hashrate

    // Skip wallet balance checking
    bool no_balance;

    // ZMQ for instant block notifications
    std::string zmq_url;  // e.g., "tcp://127.0.0.1:28332"

    MinerConfig()
        : rpc_url("http://127.0.0.1:8232")
        , rpc_user("")
        , rpc_password("")
        , num_threads(0)
        , auto_threads(true)
        , update_interval_seconds(5)
        , block_check_interval_seconds(2)
        , debug_mode(false)
        , log_file("")
        , log_to_console(false)
        , fast_mode(false)
        , no_balance(false)
        , zmq_url("") {}
};

bool parse_config(int argc, char* argv[], MinerConfig& config);
void print_usage(const char* program_name);

#endif // CONFIG_H
