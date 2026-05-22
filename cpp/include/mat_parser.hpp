#ifndef MAT_PARSER_HPP
#define MAT_PARSER_HPP

#include "bolus_tracking_cpp.hpp"
#include <vector>
#include <string>
#include <utility>
#include <fstream>
#include <iostream>
#include <cstdint>
#include <stdexcept>
#include <cstring>
#include <zlib.h>

namespace MatParser {

inline std::vector<uint8_t> decompress_zlib(const uint8_t* compressed_data, size_t compressed_size) {
    std::vector<uint8_t> decompressed;
    z_stream strm;
    std::memset(&strm, 0, sizeof(strm));
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = static_cast<uInt>(compressed_size);
    strm.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compressed_data));

    if (inflateInit(&strm) != Z_OK) {
        throw std::runtime_error("inflateInit failed");
    }

    constexpr size_t buffer_size = 131072; // 128KB chunks
    std::vector<uint8_t> buffer(buffer_size);

    int ret;
    do {
        strm.avail_out = buffer_size;
        strm.next_out = reinterpret_cast<Bytef*>(buffer.data());
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_NEED_DICT || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
            inflateEnd(&strm);
            throw std::runtime_error("inflate failed with error " + std::to_string(ret));
        }
        size_t have = buffer_size - strm.avail_out;
        decompressed.insert(decompressed.end(), buffer.begin(), buffer.begin() + have);
    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);
    return decompressed;
}

inline std::vector<uint8_t> decompress_mat(const std::vector<uint8_t>& file_content) {
    if (file_content.size() < 128) {
        return {};
    }
    if ((file_content[126] != 'I' || file_content[127] != 'M') &&
        (file_content[126] != 'M' || file_content[127] != 'I')) {
        return {};
    }

    std::vector<uint8_t> uncompressed;
    size_t idx = 128;
    while (idx < file_content.size()) {
        if (idx + 8 > file_content.size()) {
            break;
        }

        uint32_t word1 = *reinterpret_cast<const uint32_t*>(&file_content[idx]);
        uint32_t word2 = *reinterpret_cast<const uint32_t*>(&file_content[idx + 4]);

        uint32_t actual_type;
        uint32_t actual_size;
        size_t tag_size;
        size_t padded_size;
        size_t data_start;

        if ((word1 >> 16) != 0) {
            actual_type = word1 & 0xFFFF;
            actual_size = word1 >> 16;
            tag_size = 4;
            data_start = idx + 4;
            padded_size = 4;
        } else {
            actual_type = word1;
            actual_size = word2;
            tag_size = 8;
            data_start = idx + 8;
            padded_size = ((actual_size + 7) / 8) * 8;
        }

        if (data_start + actual_size > file_content.size()) {
            break; // Out of bounds
        }

        if (actual_type == 15) { // miCOMPRESSED
            try {
                auto decomp = decompress_zlib(&file_content[data_start], actual_size);
                uncompressed.insert(uncompressed.end(), decomp.begin(), decomp.end());
            } catch (const std::exception& e) {
                std::cerr << "Decompression error: " << e.what() << std::endl;
            }
            idx = data_start + actual_size;
        } else {
            uncompressed.insert(uncompressed.end(), file_content.begin() + idx, file_content.begin() + data_start + padded_size);
            idx = data_start + padded_size;
        }
    }
    return uncompressed;
}

inline void parse_mat_variables(const uint8_t* buf, size_t start, size_t end, std::vector<std::vector<std::pair<double, double>>>& found_polygons) {
    size_t idx = start;
    while (idx < end) {
        if (idx + 8 > end) {
            break;
        }

        uint32_t word1 = *reinterpret_cast<const uint32_t*>(buf + idx);
        uint32_t word2 = *reinterpret_cast<const uint32_t*>(buf + idx + 4);

        uint32_t actual_type;
        uint32_t actual_size;
        size_t tag_size;
        size_t padded_size;
        size_t data_start;

        if ((word1 >> 16) != 0) {
            actual_type = word1 & 0xFFFF;
            actual_size = word1 >> 16;
            tag_size = 4;
            data_start = idx + 4;
            padded_size = 4;
        } else {
            actual_type = word1;
            actual_size = word2;
            tag_size = 8;
            data_start = idx + 8;
            padded_size = ((actual_size + 7) / 8) * 8;
        }

        if (data_start + actual_size > end) {
            break; // Out of bounds
        }

        if (actual_type == 14) { // miMATRIX
            size_t sub_idx = data_start;
            size_t mat_end = data_start + actual_size;

            // 1. Array Flags
            if (sub_idx + 8 <= mat_end) {
                uint32_t f_word1 = *reinterpret_cast<const uint32_t*>(buf + sub_idx);
                uint32_t f_word2 = *reinterpret_cast<const uint32_t*>(buf + sub_idx + 4);

                uint32_t f_size = (f_word1 >> 16) != 0 ? (f_word1 >> 16) : f_word2;
                size_t f_tag_size = (f_word1 >> 16) != 0 ? 4 : 8;
                size_t f_padded_size = (f_word1 >> 16) != 0 ? 4 : ((f_size + 7) / 8) * 8;

                size_t f_data_start = sub_idx + f_tag_size;
                if (f_data_start + f_size <= mat_end && f_size >= 4) {
                    uint32_t flags_word = *reinterpret_cast<const uint32_t*>(buf + f_data_start);
                    uint32_t mx_class = flags_word & 0xFF;

                    // 2. Dimensions Array
                    size_t dim_idx = f_data_start + f_padded_size;
                    if (dim_idx + 8 <= mat_end) {
                        uint32_t d_word1 = *reinterpret_cast<const uint32_t*>(buf + dim_idx);
                        uint32_t d_word2 = *reinterpret_cast<const uint32_t*>(buf + dim_idx + 4);

                        uint32_t d_type = (d_word1 >> 16) != 0 ? (d_word1 & 0xFFFF) : d_word1;
                        uint32_t d_size = (d_word1 >> 16) != 0 ? (d_word1 >> 16) : d_word2;
                        size_t d_tag_size = (d_word1 >> 16) != 0 ? 4 : 8;
                        size_t d_padded_size = (d_word1 >> 16) != 0 ? 4 : ((d_size + 7) / 8) * 8;

                        size_t d_data_start = dim_idx + d_tag_size;
                        if (d_data_start + d_size <= mat_end) {
                            std::vector<int32_t> dims;
                            if (d_type == 5) { // miINT32
                                size_t num_dims = d_size / 4;
                                dims.resize(num_dims);
                                for (size_t k = 0; k < num_dims; ++k) {
                                    dims[k] = *reinterpret_cast<const int32_t*>(buf + d_data_start + k * 4);
                                }
                            }

                            // 3. Array Name
                            size_t name_idx = d_data_start + d_padded_size;
                            if (name_idx + 8 <= mat_end) {
                                uint32_t n_word1 = *reinterpret_cast<const uint32_t*>(buf + name_idx);
                                uint32_t n_word2 = *reinterpret_cast<const uint32_t*>(buf + name_idx + 4);

                                uint32_t n_size = (n_word1 >> 16) != 0 ? (n_word1 >> 16) : n_word2;
                                size_t n_tag_size = (n_word1 >> 16) != 0 ? 4 : 8;
                                size_t n_padded_size = (n_word1 >> 16) != 0 ? 4 : ((n_size + 7) / 8) * 8;

                                size_t n_data_start = name_idx + n_tag_size;
                                if (n_data_start + n_size <= mat_end) {
                                    std::string name(reinterpret_cast<const char*>(buf + n_data_start), n_size);

                                    // 4. Array Data
                                    size_t val_idx = name_idx + n_tag_size + n_padded_size;

                                    // Parse variables recursively if they are __function_workspace__ or unnamed nested MAT workspaces
                                    bool is_workspace = (name == "__function_workspace__");
                                    if (!is_workspace && name == "" && mx_class == 9 && val_idx + 8 <= mat_end) {
                                        uint32_t v_word1 = *reinterpret_cast<const uint32_t*>(buf + val_idx);
                                        uint32_t v_word2 = *reinterpret_cast<const uint32_t*>(buf + val_idx + 4);
                                        uint32_t v_size = (v_word1 >> 16) != 0 ? (v_word1 >> 16) : v_word2;
                                        size_t v_tag_size = (v_word1 >> 16) != 0 ? 4 : 8;
                                        size_t v_data_start = val_idx + v_tag_size;
                                        if (v_data_start + v_size <= mat_end && v_size >= 8) {
                                            bool has_sig = (((buf[v_data_start] == 0x00 && buf[v_data_start + 1] == 0x01) ||
                                                             (buf[v_data_start] == 0x01 && buf[v_data_start + 1] == 0x00)) &&
                                                            ((buf[v_data_start + 2] == 'I' && buf[v_data_start + 3] == 'M') ||
                                                             (buf[v_data_start + 2] == 'M' && buf[v_data_start + 3] == 'I')));
                                            if (has_sig) {
                                                is_workspace = true;
                                            }
                                        }
                                    }

                                    if (is_workspace) {
                                        if (val_idx + 8 <= mat_end) {
                                            uint32_t v_word1 = *reinterpret_cast<const uint32_t*>(buf + val_idx);
                                            uint32_t v_word2 = *reinterpret_cast<const uint32_t*>(buf + val_idx + 4);

                                            uint32_t v_size = (v_word1 >> 16) != 0 ? (v_word1 >> 16) : v_word2;
                                            size_t v_tag_size = (v_word1 >> 16) != 0 ? 4 : 8;

                                            size_t v_data_start = val_idx + v_tag_size;
                                            if (v_data_start + v_size <= mat_end) {
                                                if (v_size > 8) {
                                                    parse_mat_variables(buf, v_data_start + 8, v_data_start + v_size, found_polygons);
                                                }
                                            }
                                        }
                                    } else if (mx_class == 6 && dims.size() == 2 && dims[1] == 2 && dims[0] >= 1) {
                                        if (val_idx + 8 <= mat_end) {
                                            uint32_t v_word1 = *reinterpret_cast<const uint32_t*>(buf + val_idx);
                                            uint32_t v_word2 = *reinterpret_cast<const uint32_t*>(buf + val_idx + 4);

                                            uint32_t v_type = (v_word1 >> 16) != 0 ? (v_word1 & 0xFFFF) : v_word1;
                                            uint32_t v_size = (v_word1 >> 16) != 0 ? (v_word1 >> 16) : v_word2;
                                            size_t v_tag_size = (v_word1 >> 16) != 0 ? 4 : 8;

                                            size_t v_data_start = val_idx + v_tag_size;
                                            if (v_type == 9 && v_data_start + v_size <= mat_end) { // miDOUBLE
                                                size_t num_doubles = v_size / 8;
                                                int n_points = dims[0];
                                                if (num_doubles >= static_cast<size_t>(n_points * 2)) {
                                                    const double* double_ptr = reinterpret_cast<const double*>(buf + v_data_start);
                                                    std::vector<std::pair<double, double>> poly(n_points);
                                                    for (int k = 0; k < n_points; ++k) {
                                                        poly[k] = {double_ptr[k], double_ptr[k + n_points]};
                                                    }
                                                    found_polygons.push_back(poly);
                                                }
                                            }
                                        }
                                    } else {
                                        parse_mat_variables(buf, val_idx, mat_end, found_polygons);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        idx = data_start + padded_size;
    }
}

inline std::vector<ROI> load_rois_from_mat(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Failed to open MAT file: " << filepath << std::endl;
        return {};
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        std::cerr << "Failed to read MAT file content: " << filepath << std::endl;
        return {};
    }

    std::vector<uint8_t> decompressed = decompress_mat(buffer);
    if (decompressed.empty()) {
        std::cerr << "Decompressing MAT file failed or file has invalid header: " << filepath << std::endl;
        return {};
    }

    std::vector<std::vector<std::pair<double, double>>> found_polygons;
    parse_mat_variables(decompressed.data(), 0, decompressed.size(), found_polygons);

    std::vector<ROI> rois;
    int roi_id = 0;
    for (const auto& poly : found_polygons) {
        rois.push_back({roi_id++, poly});
    }

    return rois;
}

} // namespace MatParser

#endif // MAT_PARSER_HPP
