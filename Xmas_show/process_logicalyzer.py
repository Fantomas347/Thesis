# -*- coding: utf-8 -*-
"""
Created on Sat Apr 19 15:38:46 2025

@author: Zsolt
"""

import re

# === CONFIGURATION ===
SAMPLE_RATE = 20000  # Hz (change this if you used 20 kHz, etc.)
INPUT_FILE = "20kHz_rm_logicalyzer_llgpio_30buffer"  # Input text file from PulseView
OUTPUT_FILE = "20kHz_rm_logicalyzer_llgpio_30buffer.txt"  # Output file for your LED+music sequencer
# ======================

SAMPLE_PERIOD_MS = 1000.0 / SAMPLE_RATE  # e.g. 0.02 ms for 50kHz

def hex_to_split_bin(hex_str):
    """Convert hex to 8-bit binary, reverse it (little-endian), and insert a dot in the middle."""
    bin_str = format(int(hex_str, 16), '08b')
    reversed_bin = bin_str[::-1]  # Reverse the bit order
    return f"{reversed_bin[:4]}.{reversed_bin[4:]}"

def main():
    with open(INPUT_FILE, "r") as f:
        input_lines = f.readlines()

    output_lines = []
    prev_end = None

    for line in input_lines:
        # Accept standard hyphen or en-dash
        match = re.match(r"(\d+)[–-](\d+)\s+Parallel: Items: (\w+)", line.strip(), re.IGNORECASE)
        if not match:
            continue

        start_sample = int(match.group(1))
        end_sample = int(match.group(2))
        hex_value = match.group(3)

        if prev_end is None:
            prev_end = start_sample

        delay_samples = end_sample - prev_end
        delay_ms = round(delay_samples * SAMPLE_PERIOD_MS)
        prev_end = end_sample

        bin_value = hex_to_split_bin(hex_value)
        output_lines.append(f"{delay_ms:04d} {bin_value}")

    with open(OUTPUT_FILE, "w") as f:
        f.write("\n".join(output_lines))

    print(f"✅ Done! Output saved to '{OUTPUT_FILE}'.")

if __name__ == "__main__":
    main()
