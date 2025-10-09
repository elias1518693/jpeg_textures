#pragma once

#include <stdio.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <cmath>
#include <map>
#include <bitset>
#include "BitReader.h"
#include <string>

#define PRINT_DEBUG false

class JPEGIndexer {
private:
    struct Component {
        int h_sampling;
        int v_sampling;
    };
    std::string jpeg_path;
    int compressed_data_start = 0;
    std::vector<Component> components;
    std::map<int, std::map<std::string, int>> dc_huffman_tables;
    std::map<int, std::map<std::string, int>> ac_huffman_tables;

    std::vector<uint8_t> encoded_stream;

    std::vector<int>starts;
    std::vector<int>ranges;
    int only_ac_bit_count = 0;
    

    void append_ac_bits(const std::vector<uint8_t>& src, int start_bit, int num_bits) {
        
        for (int i = 0; i < num_bits; i++) {
            int src_bit_index = start_bit + i;
            int byte_index = src_bit_index / 8;
            int bit_in_byte = src_bit_index % 8;
            int bit = (src[byte_index] >> (7 - bit_in_byte)) & 1;
            int dest_byte_index = only_ac_bit_count / 8;
            int dest_bit_offset = only_ac_bit_count % 8;
            if (dest_byte_index >= only_ac_data.size())
                only_ac_data.push_back(0);
            only_ac_data[dest_byte_index] |= (bit << (7 - dest_bit_offset));
            only_ac_bit_count++;
        }
    }



    void parse_jpeg_headers(std::vector<uint8_t> data) {
        int index = 0;
        int length = 0;
        int precision = 0;
        while (index < data.size()) {
            uint16_t marker = (data[index] << 8) | data[index + 1];
            index += 2;

            switch (marker) {
            case 0xFFD8:
                if (PRINT_DEBUG) std::cout << "Start of Image (SOI) found\n";
                break;
            case 0xFFC0:
                if (PRINT_DEBUG) std::cout << "Start of Frame (SOF0)\n";
                length = (data[index] << 8) | data[index + 1];
                index += 2;
                precision = data[index++];
                height = (data[index] << 8) | data[index + 1];
                index += 2;
                width = (data[index] << 8) | data[index + 1];
                index += 2;
                color_components = data[index++];

                if (PRINT_DEBUG) std::cout << "Image size: " << width << "x" << height << "\n";
                if (PRINT_DEBUG) std::cout << "Number of color components: " << color_components << "\n";
                components.clear();
                for (int i = 0; i < color_components; ++i) {
                    int component_id = data[index++];
                    int sampling_factors = data[index++];
                    int quant_table_id = data[index++];

                    int h_sampling = (sampling_factors >> 4) & 0x0F;
                    int v_sampling = sampling_factors & 0x0F;
                    components.push_back({ h_sampling, v_sampling });
                    if (PRINT_DEBUG) std::cout << "Component ID: " << component_id
                        << ", h_sampling: " << h_sampling
                        << ", v_sampling: " << v_sampling
                        << ", Quantization Table ID: " << quant_table_id << "\n";
                }

                index += (length - (8 + color_components * 3));
                break;
            case 0xFFC4: // DHT (Define Huffman Table)
                if (PRINT_DEBUG) std::cout << "Define Huffman Table\n";
                parse_huffman_table(data, index);
                break;
            case 0xFFDB: // DQT (Define Quantization Table)
                if (PRINT_DEBUG) std::cout << "Define Quantization Table\n";
                parse_quantization_table(data, index);
                break;
            case 0xFFDA: // SOS (Start of Scan)
            {
                length = (data[index] << 8) | data[index + 1];
                int num_components_in_scan = data[index + 2];
                std::vector<uint8_t> sos_data(data.begin() + index + 2, data.begin() + index + length);

                for (int i = 0; i < num_components_in_scan; i++) {
                    int component_id = sos_data[1 + 2 * i];
                    int huffman_table_assignment = sos_data[2 + 2 * i];
                    int dc_huff_table_id = (huffman_table_assignment >> 4) & 0x0F;
                    int ac_huff_table_id = huffman_table_assignment & 0x0F;
                    huffman_tables_components[0][i] = huffman_tables[0][dc_huff_table_id];
                    huffman_tables_components[1][i] = huffman_tables[1][ac_huff_table_id];

                    if (PRINT_DEBUG) std::cout << "Component ID: " << component_id
                        << ", DC Table: " << dc_huff_table_id
                        << ", AC Table: " << ac_huff_table_id << "\n";
                }
                index += length;
                compressed_data_start = index;
                if (PRINT_DEBUG) std::cout << "Start of Scan (SOS) found\n";
                if (PRINT_DEBUG) std::cout << "Number of components in scan: " << num_components_in_scan << "\n";
                return;
            }
            }
        }
    }

    std::map<std::string, int> build_huffman_table(int num_codes_per_bit_length[16], const std::vector<int>& huffman_values) {
        std::map<std::string, int> huffman_table;
        int code = 0;
        int value_index = 0;
        for (int bit_length = 1; bit_length <= 16; ++bit_length) {
            int num_codes = num_codes_per_bit_length[bit_length - 1];

            for (int i = 0; i < num_codes; ++i) {
                std::string binary_code = std::bitset<16>(code).to_string().substr(16 - bit_length);
                huffman_table[binary_code] = huffman_values[value_index++];
                code++;
            }
            code <<= 1;
        }
        return huffman_table;
    }

    void parse_huffman_table(const std::vector<uint8_t>& data, int& index) {
        int length = (data[index] << 8) | data[index + 1];
        int end = index + length;
        index += 2;
        while (index < end) {
            uint8_t table_info = data[index++];
            int table_class = (table_info >> 4) & 0x0F;
            int table_id = table_info & 0x0F;
            int num_codes_per_bit_length[16];
            for (int i = 0; i < 16; ++i) {
                num_codes_per_bit_length[i] = data[index++];
            }
            int total_codes = 0;
            for (int i = 0; i < 16; ++i) total_codes += num_codes_per_bit_length[i];
            std::vector<int> huffman_values(data.begin() + index, data.begin() + index + total_codes);
            index += total_codes;
            std::map<std::string, int> huffman_table = build_huffman_table(num_codes_per_bit_length, huffman_values);
            huffman_tables[table_class][table_id] = huffman_table;
            if (PRINT_DEBUG)
                std::cout << (table_class == 0 ? "DC" : "AC") << " Huffman Table (ID=" << table_id << ") parsed.\n";
        }
    }


    void parse_quantization_table(const std::vector<uint8_t>& data, int& index) {
        int length = (data[index] << 8) | data[index + 1];
        int end = index + length;
        index += 2;

        while (index < end) {
            uint8_t table_info = data[index++];
            int precision = table_info >> 4;
            int table_id = table_info & 0x0F;
            int table_size = (precision == 0) ? 64 : 128;
            std::vector<int> quant_table;
            if (precision == 0) {
                quant_table.assign(data.begin() + index, data.begin() + index + 64);
            }
            else {
                quant_table.reserve(64);
                for (int i = 0; i < 64; ++i) {
                    int val = (data[index + i * 2] << 8) | data[index + i * 2 + 1];
                    quant_table.push_back(val);
                }
            }
            quantization_tables[table_id] = quant_table;
            index += table_size;
        }
    }

    std::vector<uint8_t> RemoveFF00(const std::vector<uint8_t>& data) {
        std::vector<uint8_t> cleaned_data;
        size_t length = data.size();
        for (size_t i = 0; i < length; ++i) {
            if (data[i] == 0xFF) {
                if (i + 1 < length && data[i + 1] == 0x00) {
                    cleaned_data.push_back(data[i]);
                    i++;
                }
                else if (i + 1 < length && data[i + 1] == 0xD9) {
                    break;
                }
            }
            else {
                cleaned_data.push_back(data[i]);
            }
        }
        return cleaned_data;
    }

    int extend_value(int value, int size) {
        if (size == 0) return 0;
        int mask = (1 << (size - 1));
        if (value < mask) {
            value -= (1 << size) - 1;
        }
        return value;
    }

    int decode_huffman(BitReader& bit_reader, const std::map<std::string, int>& huffman_table, bool ac = false) {
        std::string code = "";
        while (!bit_reader.is_at_end()) {
            code += std::to_string(bit_reader.read_bit());
            auto it = huffman_table.find(code);
            if (it != huffman_table.end()) {
                return it->second;
            }
        }
        throw std::runtime_error("End of Stream");
    }

    int decode_dc(int component_id, BitReader& bit_reader) {
        const auto& huffman_table = huffman_tables_components[0][component_id];
        int huffman_code = decode_huffman(bit_reader, huffman_table);
        if (huffman_code > 0) {
            int dc_difference = bit_reader.read_bits(huffman_code);
            int dc_value = extend_value(dc_difference, huffman_code);
            return dc_value;
        }
        else {
            return 0;
        }
    }


    void decode_ac(int component_id, BitReader& bit_reader) {
        const auto& huffman_table = huffman_tables_components[1][component_id];
        int ac_start_bit = bit_reader.get_bit_position();
        int idx = 0;
        while (idx < 63) {
            int huffman_code = decode_huffman(bit_reader, huffman_table, true);
            if (huffman_code == 0x00) {
                break;
            }
            int size = huffman_code;
            if (huffman_code > 15) {
                int run_length = huffman_code >> 4;
                size = huffman_code & 0x0F;
                if (run_length + idx > 63)
                    break;
                idx += run_length;
            }
            int ac_value = bit_reader.read_bits(size);
            idx++;
        }

        int ac_end_bit = bit_reader.get_bit_position();
        int bits_consumed = ac_end_bit - ac_start_bit;
        starts.push_back(ac_start_bit);
        ranges.push_back(bits_consumed);
    }

    void build_mcu_index(std::vector<uint8_t> compressed_data) {
        // Remove FF00 markers and store the cleaned stream in encoded_stream.
        encoded_stream = RemoveFF00(compressed_data);
        // Reset our bit-accurate AC buffer.
        only_ac_bit_count = 0;

        BitReader bit_reader(encoded_stream);
        for (const auto& comp : components) {
            previous_dc_values.push_back(0);
        }

        int idx = 0;
        uint32_t packed_offset = 0;
        uint32_t absolute_offset = 0;
        int relative_offsets_count = 0;
        int largest_relative_offset = 0;
        uint32_t last_absolute_offset = 0;  // in bits
        int offset_counter = 0;

        while (!bit_reader.is_at_end()) {
            for (size_t comp_idx = 0; comp_idx < components.size(); ++comp_idx) {
                const auto& comp = components[comp_idx];
                int h_sampling = comp.h_sampling;
                int v_sampling = comp.v_sampling;
                for (int vs = 0; vs < v_sampling; ++vs) {
                    for (int hs = 0; hs < h_sampling; ++hs) {
                        try {
                            if (bit_reader.is_at_end())
                                return;
                            int dc_coefficient = 0;
                            // Decode the DC coefficient (which is not part of only_ac_data).
                            if (idx % 6 == 0) {
                                absoluteMcuOffsets.push_back(only_ac_bit_count);
                                uint32_t current_ac_offset = only_ac_bit_count;
                                if (offset_counter % 9 == 0) {
                                    last_absolute_offset = current_ac_offset;
                                    packed_offset = last_absolute_offset;
                                    mcu_index.push_back(packed_offset);
                                    packed_offset = 0;
                                    relative_offsets_count = 0;
                                }
                                else {
                                    // Calculate the relative offset (in bits) from the last absolute offset.
                                    uint32_t relative_offset = current_ac_offset - last_absolute_offset;
                                    if (relative_offset > largest_relative_offset) {
                                        largest_relative_offset = relative_offset;
                                    }
                                    // Pack the relative offset into 16 bits (two per 32-bit word).
                                    packed_offset |= ((relative_offset & 0xFFFF) << (16 * (1 - relative_offsets_count)));
                                    relative_offsets_count++;
                                    if (relative_offsets_count == 2) {
                                        mcu_index.push_back(packed_offset);
                                        packed_offset = 0;
                                        relative_offsets_count = 0;

                                    }
                                }
                                offset_counter++;
                            }
                            if (idx % 6 == 0 || idx % 6 == 4 || idx % 6 == 5) {
                                int dc_start_bit = bit_reader.get_bit_position();
                                dc_coefficient = previous_dc_values[comp_idx] + decode_dc(comp_idx, bit_reader);
                                int dc_end_bit = bit_reader.get_bit_position();
                                int dc_numBits = dc_end_bit - dc_start_bit;

                                bitsPerDUsDC[idx % 6] += dc_numBits;
                                DUsDCCounter[idx % 6] += 1;

                                uint16_t lsb_12 = (dc_coefficient + 2048) & 0x0FFF;

                                for (int i = 11; i >= 0; i--) {
                                    int bit = (lsb_12 >> i) & 1;

                                    int byte_index = only_ac_bit_count / 8;
                                    int bit_in_byte = only_ac_bit_count % 8;

                                    if (byte_index >= only_ac_data.size())
                                        only_ac_data.push_back(0);

                                    // Write into bit position (7 - bit_in_byte) to stay MSB-first
                                    only_ac_data[byte_index] |= (bit << (7 - bit_in_byte));

                                    only_ac_bit_count++;  // Advance to next bit
                                }

                            }
                            else {
                                int dc_start_bit = bit_reader.get_bit_position();
                                dc_coefficient = previous_dc_values[comp_idx] + decode_dc(comp_idx, bit_reader);
                                int dc_end_bit = bit_reader.get_bit_position();
                                int dc_numBits = dc_end_bit - dc_start_bit;

                                bitsPerDUsDC[idx % 6] += dc_numBits;
                                DUsDCCounter[idx % 6] += 1;

                                append_ac_bits(encoded_stream, dc_start_bit, dc_end_bit - dc_start_bit);
                            }
                            previous_dc_values[comp_idx] = dc_coefficient;
                            decode_ac(comp_idx, bit_reader);
                            if (idx % 6 == 5) {
                                for (int i = 0; i < starts.size(); i++) {
                                    append_ac_bits(encoded_stream, starts[i], ranges[i]);

                                }
                                starts.clear();
                                ranges.clear();
                            }
                        }
                        catch (const std::exception& e) {
                            if (PRINT_DEBUG)std::cerr << "Decoding error at MCU " << mcu_index.size()
                                << " and bit position " << bit_reader.get_bit_position()
                                << ": " << e.what() << std::endl;
                            if (PRINT_DEBUG)std::cout << "Largest relative offset is: " << largest_relative_offset << "\n";
                            if (PRINT_DEBUG)std::cout << "JPEG bytes: " << image_data.size() << "\n";
                            return;
                        }
                        idx++;
                    }
                }
            }
        }
        if (packed_offset != 0) {
            mcu_index.push_back(packed_offset);
        }

        if (PRINT_DEBUG) std::cout << "Largest relative offset is: " << largest_relative_offset << "\n";
        if (PRINT_DEBUG) std::cout << "JPEG bytes: " << image_data.size() * 8 << "\n";

    }

public:

    std::vector<uint8_t> image_data;
    std::map<int, std::map<int, std::map<std::string, int>>> huffman_tables;
    std::map<int, std::map<int, std::map<std::string, int>>> huffman_tables_components;
    std::map<int, std::vector<int>> quantization_tables;
    std::vector<int> previous_dc_values;
    std::vector<uint32_t> mcu_index;
    std::vector<uint8_t> only_ac_data;

    JPEGIndexer(){}

    JPEGIndexer(std::vector<uint8_t>& data) {
        parse_jpeg_headers(data);
        this->image_data = std::vector<uint8_t>(data.begin() + compressed_data_start, data.end());
        build_mcu_index(this->image_data);
    }

    int width = 0;
    int height = 0;
    int color_components = 0;

	int bitsPerDUsDC[6] = {0};
	int DUsDCCounter[6] = {0};
	int mipMapLevel = 0;
	vector<uint32_t> absoluteMcuOffsets;
   
};
