#!/bin/bash

success_count=0
failure_count=0
output_file="checker_log.txt"

# Clear previous log
> "$output_file"

for i in {1..50}; do
    echo "Running checker.sh - Attempt $i" | tee -a "$output_file"
    output=$(../checker/checker.sh)
    echo "$output" | tee -a "$output_file"
    
    # Check if the output contains "40/40"
    if echo "$output" | grep -q "40/40"; then
        ((success_count++))
    else
        ((failure_count++))
        echo "Test failed on attempt $i" | tee -a "$output_file"
    fi
done

echo "-------------------------------------" | tee -a "$output_file"
echo "Summary:" | tee -a "$output_file"
echo "Successes (40/40): $success_count" | tee -a "$output_file"
echo "Failures (x/40): $failure_count" | tee -a "$output_file"
