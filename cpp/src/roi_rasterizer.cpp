#include "bolus_tracking_cpp.hpp"

#include <vector>
#include <utility>
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------
// ROIMaskRasterizer Implementation
// ---------------------------------------------------------

/**
 * @brief Scanline rasterization algorithm to convert ROI polygon points to pixel coordinate mask.
 * @param poly Vector of polygon vertices.
 * @param width Image width.
 * @param height Image height.
 * @return Flat binary pixel mask array.
 */
std::vector<int> ROIMaskRasterizer::get_mask_pixels(const std::vector<std::pair<double, double>>& poly, int width, int height) {
    std::vector<int> mask(width * height, 0);
    int n = poly.size();
    if (n < 3) return mask;
    
    // Find bounding box in y-dimension
    double y_min = poly[0].second;
    double y_max = poly[0].second;
    for (int i = 1; i < n; ++i) {
        if (poly[i].second < y_min) y_min = poly[i].second;
        if (poly[i].second > y_max) y_max = poly[i].second;
    }
    
    int r_start = std::max(0, (int)std::floor(y_min));
    int r_end = std::min(height - 1, (int)std::ceil(y_max));
    
    for (int r = r_start; r <= r_end; ++r) {
        double y = (double)r;
        std::vector<double> intersections;
        for (int i = 0; i < n; ++i) {
            auto p1 = poly[i];
            auto p2 = poly[(i + 1) % n];
            if ((p1.second < y && p2.second >= y) || (p2.second < y && p1.second >= y)) {
                if (p2.second != p1.second) {
                    double x = p1.first + (y - p1.second) * (p2.first - p1.first) / (p2.second - p1.second);
                    intersections.push_back(x);
                }
            }
        }
        std::sort(intersections.begin(), intersections.end());
        for (size_t i = 0; i < intersections.size(); i += 2) {
            if (i + 1 >= intersections.size()) break;
            int x_start = std::max(0, (int)std::ceil(intersections[i]));
            int x_end = std::min(width - 1, (int)std::floor(intersections[i+1]));
            for (int c = x_start; c <= x_end; ++c) {
                mask[r * width + c] = 1;
            }
        }
    }
    return mask;
}

/**
 * @brief Scanline rasterization algorithm to convert ROI polygon points directly into active pixel coordinate indices.
 *        Avoids allocating a full-frame mask array.
 * @param poly Vector of polygon vertices.
 * @param width Image width.
 * @param height Image height.
 * @return Vector of flat pixel coordinate indices.
 */
std::vector<int> ROIMaskRasterizer::get_active_pixels(const std::vector<std::pair<double, double>>& poly, int width, int height) {
    std::vector<int> active_indices;
    int n = poly.size();
    if (n < 3) return active_indices;
    
    // Find bounding box in y-dimension to restrict scanning
    double y_min = poly[0].second;
    double y_max = poly[0].second;
    for (int i = 1; i < n; ++i) {
        if (poly[i].second < y_min) y_min = poly[i].second;
        if (poly[i].second > y_max) y_max = poly[i].second;
    }
    
    int r_start = std::max(0, (int)std::floor(y_min));
    int r_end = std::min(height - 1, (int)std::ceil(y_max));
    
    for (int r = r_start; r <= r_end; ++r) {
        double y = (double)r;
        std::vector<double> intersections;
        for (int i = 0; i < n; ++i) {
            auto p1 = poly[i];
            auto p2 = poly[(i + 1) % n];
            if ((p1.second < y && p2.second >= y) || (p2.second < y && p1.second >= y)) {
                if (p2.second != p1.second) {
                    double x = p1.first + (y - p1.second) * (p2.first - p1.first) / (p2.second - p1.second);
                    intersections.push_back(x);
                }
            }
        }
        std::sort(intersections.begin(), intersections.end());
        for (size_t i = 0; i < intersections.size(); i += 2) {
            if (i + 1 >= intersections.size()) break;
            int x_start = std::max(0, (int)std::ceil(intersections[i]));
            int x_end = std::min(width - 1, (int)std::floor(intersections[i+1]));
            for (int c = x_start; c <= x_end; ++c) {
                active_indices.push_back(r * width + c);
            }
        }
    }
    return active_indices;
}
