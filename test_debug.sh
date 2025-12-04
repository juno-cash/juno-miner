#!/bin/bash

# Run miner for 10 seconds to see debug output
echo "Starting miner with debug output..."
echo "Will run for 10 seconds then stop"
echo ""

timeout 10 ./build/juno-miner --rpc-user miner --rpc-password miner --threads 1

echo ""
echo "Done!"
