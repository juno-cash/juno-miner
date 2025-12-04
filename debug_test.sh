#!/bin/bash

# Debug script to examine block template and hash calculation

echo "=== Fetching Block Template ==="
zcash-cli getblocktemplate | tee /tmp/block_template.json | jq -r '
{
  height: .height,
  target: .target,
  bits: .bits,
  version: .version,
  previousblockhash: .previousblockhash,
  curtime: .curtime,
  merkleroot: .defaultroots.merkleroot,
  blockcommitmentshash: .defaultroots.blockcommitmentshash
}'

echo ""
echo "=== Starting Miner with GDB ==="
echo "We'll set a breakpoint when a potential block is found"
echo ""

# Create GDB script
cat > /tmp/gdb_commands.txt << 'EOF'
# Break when hash meets target
break miner.cpp:99
commands
  silent
  printf "\n=== HASH MEETS TARGET ===\n"
  printf "Thread ID: %d\n", thread_id
  printf "Hash input size: %zu\n", hash_input.size()

  # Print header bytes (first 108 bytes)
  printf "Header (108 bytes):\n"
  set $i = 0
  while $i < 108
    printf "%02x", hash_input[$i]
    set $i = $i + 1
    if $i % 32 == 0
      printf "\n"
    end
  end
  printf "\n"

  # Print nonce (bytes 108-139)
  printf "\nNonce (32 bytes):\n"
  set $i = 108
  while $i < 140
    printf "%02x", hash_input[$i]
    set $i = $i + 1
  end
  printf "\n"

  # Print hash result
  printf "\nHash result (32 bytes):\n"
  set $i = 0
  while $i < 32
    printf "%02x", hash[$i]
    set $i = $i + 1
  end
  printf "\n"

  # Print target
  printf "\nTarget: %s\n", block_template.target.c_str()

  continue
end

# Also break at the start of worker_thread to verify header construction
break miner.cpp:73
commands
  silent
  printf "\n=== Worker Thread Started ===\n"
  printf "Thread ID: %d\n", thread_id
  printf "Block height: %u\n", block_template.height
  printf "Target: %s\n", block_template.target.c_str()
  printf "Header base size: %zu\n", block_template.header_base.size()

  # Print first 140 bytes of header_base
  printf "\nFull header_base (140 bytes):\n"
  set $i = 0
  while $i < 140
    printf "%02x", block_template.header_base[$i]
    set $i = $i + 1
    if $i % 32 == 0
      printf "\n"
    end
  end
  printf "\n"

  # Only break once per thread
  disable 2
  continue
end

run --rpc-user miner --rpc-password miner --threads 1
EOF

gdb -batch -x /tmp/gdb_commands.txt ./build/juno-miner
