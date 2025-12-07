# Juno Cash RandomX Miner

A high-performance standalone miner for Juno Cash cryptocurrency using the RandomX proof-of-work algorithm.

## Features

- **Multi-threaded mining** with automatic optimal thread detection
- **Two mining modes**: Fast mode (memory hungry) and Light mode (low memory)
- **NUMA-aware** thread allocation for multi-socket systems
- **RPC integration** with Juno Cash node
- **ZMQ support** for instant block notifications (optional)
- **Real-time statistics** and hashrate monitoring
- **Interactive controls** for adjusting threads during mining

## Requirements

- C++17 compatible compiler (GCC 7+, Clang 5+)
- CMake 3.10 or higher
- libcurl development libraries
- OpenSSL development libraries
- JsonCpp development libraries

### RAM Requirements

**Fast Mode**:
- ~2.5 GB shared for RandomX dataset + cache
- ~4 MB per thread for scratchpad
- Example: 8 threads needs ~2.6 GB total

**Light Mode**:
- ~300 MB shared for RandomX cache
- ~4 MB per thread for scratchpad
- Example: 8 threads needs ~350 MB total

### Install Dependencies (Ubuntu/Debian)

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libcurl4-openssl-dev libssl-dev libjsoncpp-dev libzmq3-dev
```

### Install Dependencies (CentOS/RHEL)

```bash
sudo yum install -y gcc-c++ cmake libcurl-devel openssl-devel jsoncpp-devel zeromq-devel
```

## Building

```bash
./build.sh
```

This will create the `juno-miner` binary in the `build/` directory.

## Configuration

### Running a Juno Cash Node

Before mining, you need a running Juno Cash node with RPC enabled. Add to your `~/.junocash/junocashd.conf`:

```
server=1
rpcuser=yourusername
rpcpassword=yourpassword
rpcport=8232
rpcallowip=127.0.0.1
```

Start the node:

```bash
junocashd -daemon
```

## Usage

### Basic Usage (Light Mode, Auto-detect threads)

```bash
./build/juno-miner --rpc-user yourusername --rpc-password yourpassword
```

### Fast Mode

```bash
./build/juno-miner --rpc-user yourusername --rpc-password yourpassword --fast-mode
```

### Full Options

```bash
./build/juno-miner --help
```

Available options:
- `--rpc-url URL` - RPC server URL (default: http://127.0.0.1:8232)
- `--rpc-user USER` - RPC username
- `--rpc-password PASS` - RPC password
- `--threads N` - Number of mining threads (default: auto-detect)
- `--fast-mode` - Use full RandomX dataset (~2.5GB) for 2x hashrate
- `--update-interval N` - Stats update interval in seconds (default: 5)
- `--block-check N` - Block check interval in seconds (default: 2)
- `--zmq-url URL` - ZMQ endpoint for instant block notifications (e.g., tcp://127.0.0.1:28332)
- `--no-balance` - Skip wallet balance checks
- `--debug` - Enable debug logging
- `--log-file FILE` - Write debug logs to file (default: juno-miner.log)
- `--log-console` - Write debug logs to console
- `--help` - Show help message

## Interactive Controls

While mining, press:
- `SPACE` - Refresh the UI
- `T` - Adjust thread count
- `Ctrl+C` - Stop mining

## How It Works

1. **Resource Detection**: On startup, the miner detects available RAM and CPU cores to calculate optimal thread count
2. **RandomX Initialization**: Creates RandomX cache (light mode) or full dataset (fast mode)
3. **Mining**: Multiple threads hash block headers with random nonces
4. **Epoch Handling**: Automatically reinitializes RandomX when epoch changes (every 2048 blocks)
5. **Block Submission**: When a valid solution is found, submits the block via RPC

## ZMQ Block Notifications (Recommended)

For instant detection of new blocks on the network, enable ZMQ notifications. Without ZMQ, the miner polls the node every 2 seconds. With ZMQ, new blocks are detected in under 100ms, reducing wasted hashpower on stale blocks.

### Option 1: Direct Connection to Node

Configure ZMQ on your Juno Cash node. Add to `~/.junocash/junocashd.conf`:

```
zmqpubhashblock=tcp://127.0.0.1:28332
```

Restart the node after making this change.

Then run the miner with:

```bash
./build/juno-miner --rpc-user yourusername --rpc-password yourpassword --zmq-url tcp://127.0.0.1:28332
```

### Option 2: Via juno-proxy (Recommended for Multiple Miners)

If you're running multiple miners or want to isolate your node credentials, use juno-proxy as an intermediary. The proxy forwards ZMQ notifications to all connected miners.

1. Configure your node's ZMQ as shown above
2. Enable ZMQ proxy in juno-proxy's `config.toml`:
   ```toml
   [zmq]
   enabled = true
   upstream_url = "tcp://127.0.0.1:28332"
   listen = "tcp://0.0.0.0:28333"
   topic = "hashblock"
   ```
3. Connect miners to the proxy:
   ```bash
   ./build/juno-miner --rpc-url http://proxy-host:8233 --zmq-url tcp://proxy-host:28333
   ```

This setup allows multiple miners to receive instant block notifications without each connecting directly to your node.

The miner will display "(ZMQ)" in the status message when a new block is detected via ZMQ notification.

## Performance Tuning

### Fast Mode vs Light Mode

| Mode  | Memory    | Hashrate | Best For                    |
|-------|-----------|----------|-----------------------------|
| Fast  | ~2.5 GB   | 2x       | Dedicated mining machines   |
| Light | ~300 MB   | 1x       | Low-memory systems          |

### Thread Count

The miner automatically calculates optimal threads based on CPU cores. You can override with `--threads N`, but be aware:
- More threads than CPU cores = reduced efficiency
- Fast mode requires ~2.5GB RAM regardless of thread count

### NUMA Systems

On multi-socket systems with NUMA, the miner automatically:
- Detects NUMA topology
- Distributes threads across NUMA nodes
- Pins threads to local CPUs
- Allocates memory locally to each node (light mode)

## Troubleshooting

### RPC Connection Failed

- Verify Juno Cash node is running: `ps aux | grep junocash`
- Check RPC credentials in `~/.junocash/junocashd.conf`
- Verify RPC port (default 8232) is correct

### Out of Memory (Fast Mode)

- Switch to light mode (remove `--fast-mode`)
- Or ensure at least 2.5GB RAM is available

### Low Hashrate

- Use `--fast-mode` for 2x hashrate improvement
- Ensure optimal thread count (auto-detect usually best)
- Check CPU governor is set to "performance"
- Verify no CPU throttling due to thermal limits

### Build Errors

If you encounter missing dependencies:
```bash
# Ubuntu/Debian
sudo apt-get install -y build-essential cmake libcurl4-openssl-dev libssl-dev libjsoncpp-dev libzmq3-dev

# CentOS/RHEL
sudo yum install -y gcc-c++ cmake libcurl-devel openssl-devel jsoncpp-devel zeromq-devel
```

Note: libzmq is optional. If not installed, the miner will build without ZMQ support and use polling only.

## Security Notes

- Never expose RPC credentials in command history
- Consider using environment variables or config file for credentials
- Run the miner on the same machine as the node when possible
- Use strong RPC passwords

## License

This miner uses the RandomX library, which is licensed under the BSD 3-Clause License.

## Support

For issues, questions, or contributions, please contact the Juno Cash development team.
