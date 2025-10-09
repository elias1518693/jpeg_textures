#pragma once
#include <vector>
#include <stdexcept>

class BitReader {
private:
    const std::vector<uint8_t>& data; // Reference to input data
    int byte_offset = 0;              // Current byte offset in data
    int bit_offset = 0;               // Current bit offset within the byte
    int total_bits = 0;               // Total bits to read
    int end_bit_position = 0;         // End bit position (exclusive)

public:
    // Constructor to read from the entire data
    BitReader(const std::vector<uint8_t>& data)
        : data(data), total_bits(data.size() * 8), end_bit_position(total_bits) {}

    // Constructor to read from a subrange of data
    BitReader(std::vector<uint8_t>& data, int start_bit_position, int end_bit_position)
        : data(data), end_bit_position(end_bit_position) {
        if (start_bit_position < 0 || end_bit_position > data.size() * 8 || start_bit_position >= end_bit_position) {
            throw std::invalid_argument("Invalid bit range");
        }
        total_bits = end_bit_position - start_bit_position;
        jump_to_position(start_bit_position);
    }

    // Check if the reader has reached the end of the bitstream
    bool is_at_end() {
        return get_bit_position() >= end_bit_position;
    }

    // Read a single bit from the data
    int read_bit() {
        if (is_at_end()) throw std::runtime_error("End of bitstream reached");
        int bit = (data[byte_offset] >> (7 - bit_offset)) & 1;
        if (++bit_offset == 8) {
            bit_offset = 0;
            byte_offset++;
        }
        return bit;
    }

    // Read multiple bits (up to 32) from the data
    int read_bits(int count) {
        if (count < 0 || count > 32) throw std::invalid_argument("Invalid bit count");
        int value = 0;
        for (int i = 0; i < count; i++) value = (value << 1) | read_bit();
        return value;
    }

    // Jump to a specific bit position in the data
    void jump_to_position(int bit_position) {
        if (bit_position < 0 || bit_position >= end_bit_position) {
            throw std::invalid_argument("Invalid bit position");
        }
        byte_offset = bit_position / 8;
        bit_offset = bit_position % 8;
    }

    // Get the current bit position in the data
    int get_bit_position() const {
        return byte_offset * 8 + bit_offset;
    }
};