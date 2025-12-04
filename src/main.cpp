#include <iostream>
#include <iomanip>
#include <sstream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <deque>
#include <ctime>
#include <limits>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include "config.h"
#include "utils.h"
#include "rpc_client.h"
#include "miner.h"
#include "logger.h"

std::atomic<bool> running(true);
std::atomic<bool> refresh_ui(false);
Miner* global_miner = nullptr;

struct termios orig_termios;

void restore_terminal() {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

void set_nonblocking_input() {
    struct termios new_termios;
    tcgetattr(STDIN_FILENO, &orig_termios);
    new_termios = orig_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
}

char check_key_pressed() {
    char c;
    if (read(STDIN_FILENO, &c, 1) == 1) {
        return c;
    }
    return '\0';
}

void signal_handler(int signal) {
    // Signal handlers should be async-safe and non-blocking
    // Only set atomic flags here, let main loop handle cleanup
    std::cout << std::endl << "Received signal " << signal << ", shutting down..." << std::endl;
    running = false;
    // Don't call miner->stop() here as it blocks on thread.join()
    // The main loop will handle cleanup when it sees running == false
}

void clear_screen() {
    std::cout << "\033[2J\033[H";
}

void hide_cursor() {
    std::cout << "\033[?25l" << std::flush;
}

void show_cursor() {
    std::cout << "\033[?25h" << std::flush;
}

// ============================================================================
// Beautiful UI Helper Functions
// ============================================================================

// Calculate visible length of string (excluding ANSI escape codes)
static size_t visibleLength(const std::string& str) {
    size_t len = 0;
    bool inEscape = false;
    for (size_t i = 0; i < str.length(); ) {
        unsigned char c = str[i];

        if (c == '\e') {
            inEscape = true;
            i++;
        } else if (inEscape && c == 'm') {
            inEscape = false;
            i++;
        } else if (!inEscape) {
            // Count this as one character and skip UTF-8 continuation bytes
            len++;
            // UTF-8: if byte starts with 11xxxxxx, count following 10xxxxxx bytes
            if ((c & 0x80) == 0) {
                i++; // ASCII (0xxxxxxx)
            } else if ((c & 0xE0) == 0xC0) {
                i += 2; // 2-byte UTF-8 (110xxxxx 10xxxxxx)
            } else if ((c & 0xF0) == 0xE0) {
                i += 3; // 3-byte UTF-8 (1110xxxx 10xxxxxx 10xxxxxx)
            } else if ((c & 0xF8) == 0xF0) {
                i += 4; // 4-byte UTF-8 (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
            } else {
                i++; // Invalid UTF-8, just skip
            }
        } else {
            i++;
        }
    }
    return len;
}

// Draw a horizontal line with optional title
static void drawLine(const std::string& title = "", const std::string& left = "├", const std::string& right = "┤", const std::string& fill = "─", int width = 72) {
    std::cout << left;
    if (!title.empty()) {
        int titleLen = title.length() + 2; // +2 for spaces
        int leftPad = (width - titleLen) / 2;
        int rightPad = width - titleLen - leftPad;
        for (int i = 0; i < leftPad; i++) std::cout << fill;
        std::cout << " \e[1;37m" << title << "\e[0m ";
        for (int i = 0; i < rightPad; i++) std::cout << fill;
    } else {
        for (int i = 0; i < width; i++) std::cout << fill;
    }
    std::cout << right << std::endl;
}

// Draw top border of box
static void drawBoxTop(const std::string& title = "", int width = 72) {
    drawLine(title, "┌", "┐", "─", width);
}

// Draw bottom border of box
static void drawBoxBottom(int width = 72) {
    drawLine("", "└", "┘", "─", width);
}

// Draw a data row inside a box with label and value
static void drawRow(const std::string& label, const std::string& value, int width = 72) {
    int labelLen = visibleLength(label);
    int valueLen = visibleLength(value);
    int padding = width - labelLen - valueLen - 2; // -2 for the two spaces (after | and before |)

    std::cout << "│ \e[1;36m" << label << "\e[0m";
    for (int i = 0; i < padding; i++) std::cout << " ";
    std::cout << "\e[1;33m" << value << "\e[0m │" << std::endl;
}

// Draw a centered text line in a box
static void drawCentered(const std::string& text, const std::string& color = "", int width = 72) {
    int textLen = visibleLength(text);
    int padding = (width - textLen) / 2;
    int rightPad = width - textLen - padding;

    std::cout << "│";
    for (int i = 0; i < padding; i++) std::cout << " ";
    if (!color.empty()) std::cout << color;
    std::cout << text;
    if (!color.empty()) std::cout << "\e[0m";
    for (int i = 0; i < rightPad; i++) std::cout << " ";
    std::cout << "│" << std::endl;
}

// Forward declaration
void add_update_message(const std::string& msg);

int get_thread_count_input(const utils::SystemResources& resources) {
    // Small delay to ensure the 'T' keypress is fully processed
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // First, flush any pending input from the non-blocking stdin
    tcflush(STDIN_FILENO, TCIFLUSH);

    // Restore terminal to blocking mode and configure for line input
    struct termios input_termios;
    tcgetattr(STDIN_FILENO, &input_termios);
    input_termios.c_lflag |= (ICANON | ECHO);  // Enable canonical mode and echo
    tcsetattr(STDIN_FILENO, TCSANOW, &input_termios);

    // Set stdin to blocking mode
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);

    show_cursor();

    // Calculate recommended max threads based on mode
    // Note: This function doesn't know the mode, so we show both recommendations
    unsigned int light_mode_max = utils::calculate_optimal_threads(resources, false);
    unsigned int fast_mode_max = utils::calculate_optimal_threads(resources, true);

    std::cout << "\n\n";
    drawBoxTop("ADJUST THREAD COUNT");
    drawRow("CPU Cores", std::to_string(resources.cpu_cores));
    drawRow("Available RAM", std::to_string(resources.available_ram_mb) + " MB");
    drawRow("Light Mode Max", std::to_string(light_mode_max) + " threads");
    if (fast_mode_max > 0) {
        drawRow("Fast Mode Max", std::to_string(fast_mode_max) + " threads");
    } else {
        drawRow("Fast Mode", "Insufficient RAM (<2.5GB)");
    }
    drawCentered("(Light: ~256MB shared, Fast: ~2GB shared)", "\e[0;37m");
    drawBoxBottom();
    std::cout << std::endl;
    std::cout << "Enter thread count (1-" << resources.cpu_cores << "): ";
    std::cout << std::flush;

    // Read input using direct read() in a loop until newline
    char input_buffer[32];
    int pos = 0;
    while (pos < 31) {
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) {
            add_update_message("Failed to read input - thread count unchanged");
            set_nonblocking_input();
            hide_cursor();
            return -1;
        }
        if (c == '\n') {
            input_buffer[pos] = '\0';
            break;
        }
        input_buffer[pos++] = c;
    }
    input_buffer[pos] = '\0';

    // Restore non-blocking input
    set_nonblocking_input();

    // Clear stdin buffer after switching back to non-blocking
    tcflush(STDIN_FILENO, TCIFLUSH);

    hide_cursor();

    // Parse input
    int thread_count = 0;
    if (sscanf(input_buffer, "%d", &thread_count) != 1) {
        add_update_message("Invalid input - thread count unchanged");
        return -1;
    }

    if (thread_count < 1) {
        add_update_message("Invalid thread count (minimum 1)");
        return -1;
    }

    if (thread_count > static_cast<int>(resources.cpu_cores)) {
        std::ostringstream msg;
        msg << "Warning: " << thread_count << " threads exceeds "
            << resources.cpu_cores << " CPU cores";
        add_update_message(msg.str());
    }

    return thread_count;
}

std::string format_hashrate(double hashrate) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    if (hashrate > 1000000) {
        ss << (hashrate / 1000000.0) << " MH/s";
    } else if (hashrate > 1000) {
        ss << (hashrate / 1000.0) << " KH/s";
    } else {
        ss << hashrate << " H/s";
    }
    return ss.str();
}

// Global update log for scrolling messages
std::deque<std::string> update_log;
const size_t MAX_UPDATE_LINES = 4;

void add_update_message(const std::string& msg) {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto tm_now = *std::localtime(&time_t_now);

    char timestamp[16];
    snprintf(timestamp, sizeof(timestamp), "%02d:%02d:%02d",
             tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);

    std::string timestamped_msg = std::string("[") + timestamp + "] " + msg;
    update_log.push_back(timestamped_msg);

    // Keep only last MAX_UPDATE_LINES messages
    while (update_log.size() > MAX_UPDATE_LINES) {
        update_log.pop_front();
    }
}

void print_status_screen(
    uint64_t current_height,
    uint64_t seed_height,
    const std::vector<uint8_t>& seed_hash,
    double local_hashrate,
    uint64_t hash_count,
    double network_hashrate,
    double difficulty,
    double mature_balance,
    double immature_balance,
    double total_balance,
    uint64_t blocks_mined,
    int uptime_seconds,
    unsigned int num_threads,
    bool fast_mode,
    bool no_balance,
    const std::string& status = "ACTIVE"
) {
    std::cout << "\033[H"; // Move cursor to home

    // Format uptime
    char uptime_str[32];
    snprintf(uptime_str, sizeof(uptime_str), "%02dh %02dm %02ds",
             uptime_seconds / 3600, (uptime_seconds % 3600) / 60, uptime_seconds % 60);

    // Header with title
    drawBoxTop("");
    drawCentered("JUNO CASH RANDOMX MINER", "\e[1;33m");
    drawCentered(std::string("Uptime: ") + uptime_str, "\e[0;37m");
    drawBoxBottom();
    std::cout << std::endl;

    // Mining Status Box
    drawBoxTop("MINING");
    std::string status_display;
    if (status == "ACTIVE") {
        status_display = "\e[1;32m● ACTIVE\e[0m";
    } else if (status == "DISCONNECTED") {
        status_display = "\e[1;31m● DISCONNECTED\e[0m";
    } else {
        status_display = "\e[1;33m● " + status + "\e[0m";
    }
    drawRow("Status", status_display);
    drawRow("Block Height", std::to_string(current_height));

    // Format RandomX Epoch with last 8 hex chars of seed hash
    std::string epoch_display = std::to_string((seed_height / 2048) + 1);
    if (seed_hash.size() >= 4) {
        char hex_suffix[16];
        snprintf(hex_suffix, sizeof(hex_suffix), " (%02x%02x%02x%02x)",
                 seed_hash[seed_hash.size() - 4],
                 seed_hash[seed_hash.size() - 3],
                 seed_hash[seed_hash.size() - 2],
                 seed_hash[seed_hash.size() - 1]);
        epoch_display += hex_suffix;
    }
    drawRow("RandomX Epoch", epoch_display);

    std::string mode_display = fast_mode ? "\e[1;32mFAST\e[0m" : "\e[1;33mLIGHT\e[0m";
    drawRow("Mode", mode_display);
    drawRow("Threads", std::to_string(num_threads));
    drawRow("Local Hashrate", format_hashrate(local_hashrate));
    drawRow("Hashes", std::to_string(hash_count));
    drawRow("Blocks Mined", std::to_string(blocks_mined));
    drawBoxBottom();
    std::cout << std::endl;

    // Network Status Box
    drawBoxTop("NETWORK");
    drawRow("Hashrate", format_hashrate(network_hashrate));
    std::ostringstream diff_ss;
    diff_ss << std::fixed << std::setprecision(2) << difficulty;
    drawRow("Difficulty", diff_ss.str());
    drawBoxBottom();
    std::cout << std::endl;

    // Wallet Status Box (skip if --no-balance)
    if (!no_balance) {
        drawBoxTop("WALLET");
        char mature_str[32], immature_str[32], total_str[32];
        snprintf(mature_str, sizeof(mature_str), "%.8f JNO", mature_balance);
        snprintf(immature_str, sizeof(immature_str), "%.8f JNO", immature_balance);
        snprintf(total_str, sizeof(total_str), "%.8f JNO", total_balance);
        drawRow("Mature Balance", mature_str);
        drawRow("Immature Balance", immature_str);
        drawRow("Total Balance", total_str);
        drawBoxBottom();
        std::cout << std::endl;
    }

    // Updates Box
    drawBoxTop("UPDATES");
    if (update_log.empty()) {
        drawCentered("(no updates)", "\e[0;37m");
    } else {
        for (size_t i = 0; i < MAX_UPDATE_LINES; i++) {
            if (i < update_log.size()) {
                std::cout << "│ " << update_log[i];
                // Pad the rest of the line
                int msg_len = visibleLength(update_log[i]);
                for (int j = msg_len; j < 70; j++) std::cout << " ";
                std::cout << " │" << std::endl;
            } else {
                std::cout << "│";
                for (int j = 0; j < 72; j++) std::cout << " ";
                std::cout << "│" << std::endl;
            }
        }
    }
    drawBoxBottom();
    std::cout << std::endl;

    // Controls footer
    drawBoxTop("CONTROLS");
    drawCentered("\e[1;37m[SPACE]\e[0m Refresh  \e[1;37m[T]\e[0m Adjust Threads  \e[1;37m[Ctrl+C]\e[0m Stop");
    drawBoxBottom();

    std::cout << "\033[K"; // Clear to end of line
    std::flush(std::cout);
}

void print_system_info(const utils::SystemResources& resources) {
    drawBoxTop("SYSTEM RESOURCES");
    drawRow("CPU Cores", std::to_string(resources.cpu_cores));
    drawRow("Total RAM", std::to_string(resources.total_ram_mb) + " MB");
    drawRow("Available RAM", std::to_string(resources.available_ram_mb) + " MB");
    drawRow("Optimal Threads", std::to_string(resources.optimal_threads));
    drawBoxBottom();
    std::cout << std::endl;
}

int main(int argc, char* argv[]) {
    // Parse configuration
    MinerConfig config;
    if (!parse_config(argc, argv, config)) {
        return 1;
    }

    // Initialize logger
    if (config.debug_mode || !config.log_file.empty()) {
        Logger::instance().set_debug_mode(config.debug_mode);
        if (!config.log_file.empty()) {
            Logger::instance().enable_file_logging(config.log_file);
        }
        if (config.log_to_console) {
            Logger::instance().enable_console_logging(true);
        }
        LOG_INFO("=== Juno Miner Starting ===");
        if (config.debug_mode) {
            LOG_DEBUG("Debug logging enabled");
        }
    }

    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Set up non-blocking keyboard input
    set_nonblocking_input();
    atexit(restore_terminal);

    drawBoxTop("");
    drawCentered("JUNO CASH RANDOMX MINER", "\e[1;33m");
    drawCentered("Privacy Money for All", "\e[1;36m");
    drawBoxBottom();
    std::cout << std::endl;

    // Detect system resources
    LOG_DEBUG("Detecting system resources");
    utils::SystemResources resources = utils::detect_system_resources();
    print_system_info(resources);
    LOG_DEBUG_STREAM("System: " << resources.cpu_cores << " cores, "
                     << (resources.total_ram_mb / 1024.0) << " GB RAM, optimal threads: "
                     << resources.optimal_threads);

    // Check fast mode feasibility
    bool fast_mode = config.fast_mode;
    if (fast_mode) {
        unsigned int fast_mode_threads = utils::calculate_optimal_threads(resources, true);
        if (fast_mode_threads == 0) {
            std::cout << "Warning: Insufficient RAM for fast mode (need ~2.5GB)" << std::endl;
            std::cout << "Falling back to light mode" << std::endl;
            fast_mode = false;
        }
    }

    // Determine thread count based on mode
    unsigned int optimal_threads = utils::calculate_optimal_threads(resources, fast_mode);
    unsigned int num_threads = config.auto_threads ? optimal_threads : config.num_threads;
    LOG_DEBUG_STREAM("Thread count: " << num_threads << " (auto: " << (config.auto_threads ? "yes" : "no") << ")");
    LOG_DEBUG_STREAM("Mode: " << (fast_mode ? "FAST" : "LIGHT"));

    if (num_threads > resources.cpu_cores) {
        std::cout << "Warning: Requested " << num_threads << " threads, but only "
                  << resources.cpu_cores << " CPU cores available" << std::endl;
    }

    std::string mode_str = fast_mode ? "FAST (2x hashrate)" : "LIGHT";
    std::cout << "Mode: " << mode_str << std::endl;
    std::cout << "Using " << num_threads << " mining thread(s)" << std::endl;
    std::cout << std::endl;

    // Initialize RPC client
    LOG_DEBUG_STREAM("Initializing RPC client: " << config.rpc_url);
    RPCClient rpc(config.rpc_url, config.rpc_user, config.rpc_password);

    // Test RPC connection
    std::cout << "Testing RPC connection to " << config.rpc_url << "..." << std::endl;
    Json::Value blockchain_info;
    if (!rpc.get_blockchain_info(blockchain_info)) {
        std::cerr << "Failed to connect to RPC server" << std::endl;
        std::cerr << "Please check your RPC URL, username, and password" << std::endl;
        LOG_ERROR("RPC connection failed");
        return 1;
    }

    std::cout << "Connected to node:" << std::endl;
    std::cout << "  Chain: " << blockchain_info["chain"].asString() << std::endl;
    std::cout << "  Block: " << blockchain_info["blocks"].asUInt() << std::endl;
    std::cout << std::endl;
    LOG_INFO_STREAM("Connected to " << blockchain_info["chain"].asString()
                    << " at block " << blockchain_info["blocks"].asUInt());

    // Get initial block template to determine seed
    std::cout << "Fetching initial block template to determine RandomX seed..." << std::endl;
    LOG_DEBUG("Requesting initial block template");
    Json::Value initial_template_data;
    if (!rpc.get_block_template(initial_template_data, "")) {
        std::cerr << "Failed to get initial block template" << std::endl;
        LOG_ERROR("Failed to get initial block template");
        return 1;
    }

    // Parse initial template to get seed
    BlockTemplate initial_template = parse_block_template(initial_template_data);
    LOG_DEBUG_STREAM("Initial template: height=" << initial_template.height
                     << " seed_height=" << initial_template.seed_height);

    // Initialize miner with seed
    LOG_DEBUG("Initializing miner and RandomX cache");
    Miner miner(num_threads, fast_mode);
    global_miner = &miner;
    if (!miner.initialize(initial_template.seed_hash)) {
        std::cerr << "Failed to initialize miner" << std::endl;
        LOG_ERROR("Miner initialization failed");
        return 1;
    }
    LOG_INFO("Miner initialized successfully");

    std::cout << std::endl;
    std::cout << "Starting mining..." << std::endl;
    std::cout << std::endl;

    // Initialize status variables
    uint64_t blocks_mined = 0;
    auto start_time = std::chrono::steady_clock::now();
    auto last_stats_update = std::chrono::steady_clock::now();
    const int stats_update_interval = 10; // Update network stats every 10 seconds
    uint64_t current_block_height = 0;
    std::vector<uint8_t> current_seed_hash = initial_template.seed_hash;
    double network_hashrate = 0.0;
    double difficulty = 0.0;
    double mature_balance = 0.0;
    double immature_balance = 0.0;
    double total_balance = 0.0;
    bool ui_initialized = false;

    // Add initial update message
    add_update_message("Mining started");

    // Track connection state for reconnection messages
    bool was_disconnected = false;

    // Main mining loop
    while (running.load()) {
        // Initialize UI on first iteration
        if (!ui_initialized) {
            std::cout << "Requesting block template..." << std::endl;
            clear_screen();
            hide_cursor();
            ui_initialized = true;
        }

        // Get block template (node will use wallet's default address for coinbase)
        Json::Value template_data;
        if (!rpc.get_block_template(template_data, "")) {
            add_update_message(rpc.get_last_error());

            // Update the display to show the error
            auto now = std::chrono::steady_clock::now();
            auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            print_status_screen(
                current_block_height,
                RandomX_SeedHeight(current_block_height > 0 ? current_block_height : 0),
                current_seed_hash,
                0.0, // hashrate
                miner.get_hash_count(),
                network_hashrate,
                difficulty,
                mature_balance,
                immature_balance,
                total_balance,
                blocks_mined,
                uptime,
                num_threads,
                fast_mode,
                config.no_balance,
                "DISCONNECTED"
            );

            was_disconnected = true;
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        // Check if we just reconnected
        if (was_disconnected) {
            add_update_message("RPC reconnected - resuming mining");
            LOG_INFO("RPC connection restored, resuming mining");
            was_disconnected = false;
        }

        // Parse template
        BlockTemplate block_template = parse_block_template(template_data);
        current_block_height = block_template.height;

        // Check if epoch changed (seed hash changed)
        if (block_template.seed_hash != current_seed_hash) {
            uint64_t old_epoch = RandomX_SeedHeight(current_block_height - 1);
            uint64_t new_epoch = RandomX_SeedHeight(current_block_height);

            std::ostringstream msg;
            msg << "EPOCH TRANSITION: " << old_epoch << " -> " << new_epoch;
            add_update_message(msg.str());
            add_update_message("Updating RandomX cache...");
            LOG_INFO_STREAM("Epoch transition detected: " << old_epoch << " -> " << new_epoch);
            LOG_DEBUG_STREAM("Old seed: " << utils::bytes_to_hex(current_seed_hash.data(), 32));
            LOG_DEBUG_STREAM("New seed: " << utils::bytes_to_hex(block_template.seed_hash.data(), 32));

            if (!miner.update_seed(block_template.seed_hash)) {
                std::cerr << "Failed to update seed for new epoch" << std::endl;
                add_update_message("ERROR: Failed to update seed for new epoch!");
                LOG_ERROR("Failed to update RandomX seed for new epoch");
                return 1;
            }

            current_seed_hash = block_template.seed_hash;
            add_update_message("Epoch transition complete!");
            LOG_INFO("Epoch transition completed successfully");

            // Don't reinitialize UI - just continue with updated cache
        }

        // Start mining in background threads
        miner.start_mining(block_template);

        // Progress reporting
        auto last_update = std::chrono::steady_clock::now();
        auto last_block_check = std::chrono::steady_clock::now();
        bool block_changed = false;
        int consecutive_rpc_failures = 0;
        const int max_rpc_failures = 2; // Stop mining after 2 consecutive failures

        while (miner.is_mining() && running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            // Check for keyboard input
            char key = check_key_pressed();
            if (key == ' ') {
                // Space key: refresh UI
                clear_screen();
                ui_initialized = false;
                add_update_message("UI refreshed by user");
            } else if (key == 't' || key == 'T') {
                // T key: adjust thread count
                miner.stop();
                clear_screen();

                int new_thread_count = get_thread_count_input(resources);
                if (new_thread_count > 0) {
                    LOG_DEBUG_STREAM("User requested thread count change: " << num_threads
                                    << " -> " << new_thread_count);
                    if (miner.set_thread_count(static_cast<unsigned int>(new_thread_count))) {
                        num_threads = static_cast<unsigned int>(new_thread_count);
                        std::ostringstream msg;
                        msg << "Thread count changed to " << num_threads;
                        add_update_message(msg.str());
                        LOG_INFO_STREAM("Thread count changed to " << num_threads);
                    } else {
                        add_update_message("Failed to adjust thread count");
                        LOG_ERROR("Failed to adjust thread count");
                    }
                }

                ui_initialized = false;
                // Break to get new template and restart mining
                break;
            }

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_update).count();
            auto block_check_elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_block_check).count();
            auto stats_elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_stats_update).count();
            auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();

            // Check for new blocks on the network
            if (block_check_elapsed >= config.block_check_interval_seconds) {
                Json::Value blockchain_info;
                if (rpc.get_blockchain_info(blockchain_info)) {
                    consecutive_rpc_failures = 0; // Reset on success
                    uint64_t network_height = blockchain_info["blocks"].asUInt64();
                    if (network_height > current_block_height) {
                        std::ostringstream msg;
                        msg << "New block on network! Height " << current_block_height
                            << " -> " << network_height;
                        add_update_message(msg.str());
                        LOG_INFO_STREAM("New block detected on network: height " << current_block_height
                                       << " -> " << network_height);

                        block_changed = true;
                        miner.stop();
                        // Don't reinitialize UI - just restart with new template
                        break;
                    }
                } else {
                    // RPC failed - track consecutive failures
                    consecutive_rpc_failures++;
                    LOG_WARNING_STREAM("RPC check failed (" << consecutive_rpc_failures << "/" << max_rpc_failures << ")");

                    if (consecutive_rpc_failures >= max_rpc_failures) {
                        add_update_message("RPC connection lost - stopping mining");
                        LOG_WARNING("RPC connection lost - stopping mining threads");
                        miner.stop();
                        break; // Exit inner loop to try reconnecting in outer loop
                    }
                }
                last_block_check = now;
            }

            // Update network stats and wallet balance periodically
            if (stats_elapsed >= stats_update_interval) {
                // Get mining info (includes network hashrate and difficulty)
                Json::Value mining_info;
                if (rpc.get_mining_info(mining_info)) {
                    if (mining_info.isMember("networksolps")) {
                        network_hashrate = mining_info["networksolps"].asDouble();
                    }
                    if (mining_info.isMember("difficulty")) {
                        difficulty = mining_info["difficulty"].asDouble();
                    }
                }

                // Get wallet balance (skip if --no-balance)
                if (!config.no_balance) {
                    Json::Value balance_info;
                    if (rpc.get_wallet_balance(balance_info)) {
                        if (balance_info.isMember("transparent_mature")) {
                            mature_balance = balance_info["transparent_mature"].asInt64() / 100000000.0;
                        }
                        if (balance_info.isMember("transparent_immature")) {
                            immature_balance = balance_info["transparent_immature"].asInt64() / 100000000.0;
                        }
                        if (balance_info.isMember("transparent_total")) {
                            total_balance = balance_info["transparent_total"].asInt64() / 100000000.0;
                        }
                    }
                }

                last_stats_update = now;
            }

            // Update status screen
            if (elapsed >= 1) {
                double hashrate = miner.get_hashrate();
                uint64_t hash_count = miner.get_hash_count();
                uint64_t current_seed_height = RandomX_SeedHeight(current_block_height);

                print_status_screen(
                    current_block_height,
                    current_seed_height,
                    current_seed_hash,
                    hashrate,
                    hash_count,
                    network_hashrate,
                    difficulty,
                    mature_balance,
                    immature_balance,
                    total_balance,
                    blocks_mined,
                    uptime,
                    num_threads,
                    fast_mode,
                    config.no_balance
                );

                last_update = now;
            }
        }

        // If interrupted, stop mining
        if (!running.load()) {
            miner.stop();
        }

        // If block changed, skip solution check and get new template
        if (block_changed) {
            continue;
        }

        // Get the result
        std::vector<uint8_t> solution_header;
        std::vector<uint8_t> solution_hash;
        BlockTemplate solution_template;
        if (miner.get_solution(solution_header, solution_hash, solution_template)) {
            LOG_INFO_STREAM("Solution found! Height: " << solution_template.height
                           << " PoW hash: " << utils::bytes_to_hex(solution_hash.data(), 32));

            // Serialize the full block (header + nSolution + transactions)
            std::string block_hex = utils::serialize_block(
                solution_header,
                solution_hash,
                solution_template.coinbase_txn_hex,
                solution_template.txn_hex
            );
            LOG_DEBUG_STREAM("Block serialized, size: " << block_hex.size() << " bytes");

            add_update_message("Submitting block...");

            // In Juno Cash, the block hash IS the RandomX PoW hash (stored in nSolution)
            // See CBlockHeader::GetHash() in src/primitives/block.cpp
            std::string block_hash_hex = utils::bytes_to_hex_reversed(solution_hash.data(), 32);

            // Submit block
            std::string result;

            if (rpc.submit_block(block_hex, result)) {
                blocks_mined++;
                // Format success message with block details
                std::ostringstream msg;
                msg << "BLOCK ACCEPTED";
                if (result != "accepted") {
                    msg << " (" << result << ")";
                }
                msg << "! Height " << solution_template.height
                    << " (Total: " << blocks_mined << ")";
                add_update_message(msg.str());

                std::ostringstream hash_msg;
                hash_msg << "Block hash: " << block_hash_hex;
                add_update_message(hash_msg.str());

                LOG_INFO_STREAM("BLOCK ACCEPTED (" << result << ")! Height: " << solution_template.height
                               << " Total mined: " << blocks_mined);
                LOG_INFO_STREAM("  Block hash (RandomX): " << block_hash_hex);
            } else {
                // Format rejection message
                std::ostringstream msg;
                msg << "Block rejected: " << result;
                add_update_message(msg.str());

                std::ostringstream hash_msg;
                hash_msg << "Block hash: " << block_hash_hex;
                add_update_message(hash_msg.str());

                LOG_WARNING_STREAM("Block rejected: " << result);
                LOG_WARNING_STREAM("  Block hash (RandomX): " << block_hash_hex);
            }

            // Don't reinitialize UI - just continue mining with new template
        } else if (!running.load()) {
            show_cursor();
            clear_screen();
            std::cout << "Mining stopped" << std::endl;
            break;
        }
    }

    show_cursor();
    restore_terminal();
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Mining Summary" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Blocks mined: " << blocks_mined << std::endl;
    std::cout << std::endl;

    return 0;
}
