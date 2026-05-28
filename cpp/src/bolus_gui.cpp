/**
 * @file bolus_gui.cpp
 * @brief Interactive C++ GUI Triage App for Bolus Tracking Pipeline.
 * 
 * Provides a cross-platform user interface using GLFW, Dear ImGui, and ImPlot.
 * Allows users to inspect fits, crop visualization, drag markers to manually fit,
 * and save results.
 */

#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <limits>
#include <chrono>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include <GLFW/glfw3.h>
#include <tiffio.h>

#include "bolus_tracking_cpp.hpp"
#include "bolus_gui.hpp"
#include "mat_parser.hpp"

// Inline math operators for ImVec2, since ImGui doesn't define them by default in public headers
inline ImVec2 operator+(const ImVec2& lhs, const ImVec2& rhs) { return ImVec2(lhs.x + rhs.x, lhs.y + rhs.y); }
inline ImVec2 operator-(const ImVec2& lhs, const ImVec2& rhs) { return ImVec2(lhs.x - rhs.x, lhs.y - rhs.y); }
inline ImVec2 operator*(const ImVec2& lhs, float rhs) { return ImVec2(lhs.x * rhs, lhs.y * rhs); }
inline ImVec2 operator*(float lhs, const ImVec2& rhs) { return ImVec2(lhs * rhs.x, lhs * rhs.y); }
inline ImVec2 operator/(const ImVec2& lhs, float rhs) { return ImVec2(lhs.x / rhs, lhs.y / rhs); }

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#endif

std::string get_resource_path(const std::string& rel_path) {
    if (std::filesystem::exists(rel_path)) {
        return rel_path;
    }
#if defined(__APPLE__)
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        std::filesystem::path exe_path(path);
        std::filesystem::path bundle_res = exe_path.parent_path() / ".." / "Resources" / rel_path;
        if (std::filesystem::exists(bundle_res)) {
            return bundle_res.string();
        }
    }
#endif
    return rel_path;
}

bool is_valid_ttf(const std::string& path) {
    if (!std::filesystem::exists(path)) {
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    unsigned char magic[4] = {0};
    if (!file.read(reinterpret_cast<char*>(magic), 4)) {
        return false;
    }
    // Check TrueType/OpenType magic: 0x00 0x01 0x00 0x00, 'OTTO', or 'ttcf'
    bool is_ttf = (magic[0] == 0x00 && magic[1] == 0x01 && magic[2] == 0x00 && magic[3] == 0x00);
    bool is_otf = (magic[0] == 'O' && magic[1] == 'T' && magic[2] == 'T' && magic[3] == 'O');
    bool is_ttc = (magic[0] == 't' && magic[1] == 't' && magic[2] == 'c' && magic[3] == 'f');
    return is_ttf || is_otf || is_ttc;
}

struct WAVHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    int32_t overall_size = 0;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt_chunk_marker[4] = {'f', 'm', 't', ' '};
    int32_t length_of_fmt = 16;
    int16_t format_type = 1; // PCM
    int16_t channels = 1; // Mono
    int32_t sample_rate = 44100;
    int32_t byterate = 44100 * 2;
    int16_t block_align = 2;
    int16_t bits_per_sample = 16;
    char data_chunk_header[4] = {'d', 'a', 't', 'a'};
    int32_t data_size = 0;
};

void ensure_thx_sound_exists(const std::string& path) {
    if (std::filesystem::exists(path)) {
        return;
    }
    // Create directory if not exists
    std::filesystem::path p(path);
    if (p.has_parent_path() && !std::filesystem::exists(p.parent_path())) {
        std::filesystem::create_directories(p.parent_path());
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) return;

    const int sample_rate = 44100;
    const float duration = 6.5f;
    const int num_samples = (int)(sample_rate * duration);

    WAVHeader header;
    header.data_size = num_samples * sizeof(int16_t);
    header.overall_size = header.data_size + 36;
    header.byterate = sample_rate * sizeof(int16_t);

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));

    // 12 oscillator voices for custom THX-style chord resolution
    const int num_voices = 12;
    float start_f[num_voices] = {
        220.0f, 240.0f, 260.0f, 280.0f, 300.0f, 320.0f, 340.0f, 360.0f, 380.0f, 400.0f, 420.0f, 440.0f
    };
    float target_f[num_voices] = {
        36.71f,   // D1 (deep bass foundation)
        73.42f,   // D2
        110.00f,  // A2
        146.83f,  // D3
        220.00f,  // A3
        293.66f,  // D4
        369.99f,  // F#4 (bright major third)
        440.00f,  // A4
        587.33f,  // D5
        739.99f,  // F#5
        880.00f,  // A5
        1174.66f  // D6 (high resolution peak)
    };

    std::vector<double> phase(num_voices, 0.0);
    const double dt = 1.0 / sample_rate;
    const double pi = 3.14159265358979323846;

    for (int i = 0; i < num_samples; ++i) {
        float t = i * (float)dt;

        // Sound crescendo volume envelope:
        // - t=0s to 4.5s: rises from 0.0 to 0.85
        // - t=4.5s to 5.7s: stays at peak
        // - t=5.7s to 6.5s: fades out
        float vol = 0.0f;
        if (t < 4.5f) {
            vol = 0.85f * (t / 4.5f);
        } else if (t < 5.7f) {
            vol = 0.85f;
        } else {
            vol = 0.85f * (1.0f - (t - 5.7f) / 0.8f);
            if (vol < 0.0f) vol = 0.0f;
        }

        // Glide factor (smoothstep)
        float ratio = t / 4.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        float glide = ratio * ratio * (3.0f - 2.0f * ratio);

        float mixed_sample = 0.0f;

        for (int v = 0; v < num_voices; ++v) {
            // Frequency glide with analog LFO wobble
            float f_base = start_f[v] + (target_f[v] - start_f[v]) * glide;
            float lfo = 2.0f * sinf((float)(v * 3.14159f + t * 5.5f)) * (1.0f - glide) 
                      + 0.5f * sinf((float)(v * 1.5f + t * 3.5f)) * glide;
            float freq = f_base + lfo;

            // Integrate phase
            phase[v] += 2.0 * pi * freq * dt;

            // Synthesis: Fund + 2nd harmonic that opens up with glide for brightness
            float voice_val = sin(phase[v]) + 0.3f * sin(2.0 * phase[v]) * glide;
            mixed_sample += voice_val;
        }

        mixed_sample /= num_voices;
        mixed_sample *= vol;

        // Clip-limiting
        if (mixed_sample > 1.0f) mixed_sample = 1.0f;
        if (mixed_sample < -1.0f) mixed_sample = -1.0f;

        int16_t out_val = (int16_t)(mixed_sample * 32767.0f);
        file.write(reinterpret_cast<const char*>(&out_val), sizeof(int16_t));
    }
}

void play_sound_cross_platform(const std::string& audio_path) {
#if defined(_WIN32)
    std::string win_cmd = "powershell -WindowStyle Hidden -Command \"Add-Type -AssemblyName PresentationCore; $player = New-Object system.windows.media.mediaplayer; $player.Open('" + audio_path + "'); $player.Play(); Start-Sleep -s 8\" &";
    std::system(win_cmd.c_str());
#elif defined(__APPLE__)
    std::string mac_cmd = "afplay -t 8 \"" + audio_path + "\" &";
    std::system(mac_cmd.c_str());
#else
    std::string lin_cmd = "(aplay -q \"" + audio_path + "\" || paplay \"" + audio_path + "\" || pw-play \"" + audio_path + "\" || play -q \"" + audio_path + "\" || mpg123 -q \"" + audio_path + "\" || ffplay -nodisp -autoexit -loglevel quiet \"" + audio_path + "\" || cvlc --play-and-exit \"" + audio_path + "\") > /dev/null 2>&1 &";
    std::system(lin_cmd.c_str());
#endif
}

std::string to_klingon_piqad(const std::string& input) {
    std::string result = "";
    size_t i = 0;
    while (i < input.length()) {
        if (input[i] == '%') {
            result += '%';
            i++;
            while (i < input.length() && (std::isdigit(input[i]) || input[i] == '.' || input[i] == '-' || input[i] == '+' || input[i] == ' ' || input[i] == '#' || input[i] == 'l' || input[i] == 'h')) {
                result += input[i];
                i++;
            }
            if (i < input.length()) {
                result += input[i];
                i++;
            }
            continue;
        }

        // Case-insensitive multi-char phonemes
        std::string sub3 = "";
        if (i + 2 < input.length()) {
            sub3 = input.substr(i, 3);
            for (char& c : sub3) c = std::tolower(static_cast<unsigned char>(c));
        }
        std::string sub2 = "";
        if (i + 1 < input.length()) {
            sub2 = input.substr(i, 2);
            for (char& c : sub2) c = std::tolower(static_cast<unsigned char>(c));
        }

        if (sub3 == "tlh") {
            result += "\xEF\xA3\xA4"; // U+F8E4
            i += 3;
        } else if (sub2 == "ch") {
            result += "\xEF\xA3\x92"; // U+F8D2
            i += 2;
        } else if (sub2 == "gh") {
            result += "\xEF\xA3\x95"; // U+F8D5
            i += 2;
        } else if (sub2 == "ng") {
            result += "\xEF\xA3\x9C"; // U+F8DC
            i += 2;
        } else {
            char c = input[i];
            if (c == 'a' || c == 'A') result += "\xEF\xA3\x90";      // U+F8D0
            else if (c == 'b' || c == 'B') result += "\xEF\xA3\x91"; // U+F8D1
            else if (c == 'd' || c == 'D') result += "\xEF\xA3\x93"; // U+F8D3
            else if (c == 'e' || c == 'E') result += "\xEF\xA3\x94"; // U+F8D4
            else if (c == 'h' || c == 'H') result += "\xEF\xA3\x96"; // U+F8D6
            else if (c == 'i' || c == 'I') result += "\xEF\xA3\x97"; // U+F8D7
            else if (c == 'j' || c == 'J') result += "\xEF\xA3\x98"; // U+F8D8
            else if (c == 'l' || c == 'L') result += "\xEF\xA3\x99"; // U+F8D9
            else if (c == 'm' || c == 'M') result += "\xEF\xA3\x9A"; // U+F8DA
            else if (c == 'n' || c == 'N') result += "\xEF\xA3\x9B"; // U+F8DB
            else if (c == 'o' || c == 'O') result += "\xEF\xA3\x9D"; // U+F8DD
            else if (c == 'p' || c == 'P') result += "\xEF\xA3\x9E"; // U+F8DE
            else if (c == 'q') result += "\xEF\xA3\x9F";             // U+F8DF
            else if (c == 'Q') result += "\xEF\xA3\xA0";             // U+F8E0
            else if (c == 'r' || c == 'R') result += "\xEF\xA3\xA1"; // U+F8E1
            else if (c == 's' || c == 'S') result += "\xEF\xA3\xA2"; // U+F8E2
            else if (c == 't' || c == 'T') result += "\xEF\xA3\xA3"; // U+F8E3
            else if (c == 'u' || c == 'U') result += "\xEF\xA3\xA5"; // U+F8E5
            else if (c == 'v' || c == 'V') result += "\xEF\xA3\xA6"; // U+F8E6
            else if (c == 'w' || c == 'W') result += "\xEF\xA3\xA7"; // U+F8E7
            else if (c == 'y' || c == 'Y') result += "\xEF\xA3\xA8"; // U+F8E8
            else if (c == '\'') result += "\xEF\xA3\xA9";            // U+F8E9
            else if (c == '0') result += "\xEF\xA3\xB0";             // U+F8F0
            else if (c == '1') result += "\xEF\xA3\xB1";             // U+F8F1
            else if (c == '2') result += "\xEF\xA3\xB2";             // U+F8F2
            else if (c == '3') result += "\xEF\xA3\xB3";             // U+F8F3
            else if (c == '4') result += "\xEF\xA3\xB4";             // U+F8F4
            else if (c == '5') result += "\xEF\xA3\xB5";             // U+F8F5
            else if (c == '6') result += "\xEF\xA3\xB6";             // U+F8F6
            else if (c == '7') result += "\xEF\xA3\xB7";             // U+F8F7
            else if (c == '8') result += "\xEF\xA3\xB8";             // U+F8F8
            else if (c == '9') result += "\xEF\xA3\xB9";             // U+F8F9
            else if (c == ',') result += "\xEF\xA3\xBD";             // U+F8FD
            else if (c == '.') result += "\xEF\xA3\xBE";             // U+F8FE
            // Fallback for non-Klingon English letters
            else if (c == 'c' || c == 'C') result += "\xEF\xA3\xA2"; // map to S (U+F8E2)
            else if (c == 'f' || c == 'F') result += "\xEF\xA3\xA6"; // map to v (U+F8E6)
            else if (c == 'g' || c == 'G') result += "\xEF\xA3\x95"; // map to gh (U+F8D5)
            else if (c == 'k' || c == 'K') result += "\xEF\xA3\x9F"; // map to q (U+F8DF)
            else if (c == 'x' || c == 'X') result += "\xEF\xA3\xA2"; // map to S (U+F8E2)
            else if (c == 'z' || c == 'Z') result += "\xEF\xA3\xA2"; // map to S (U+F8E2)
            else {
                result += c;
            }
            i++;
        }
    }
    return result;
}

std::string find_cjk_font() {
    std::vector<std::string> paths = {
        // macOS
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/STHeiti Light.ttc",
        "/System/Library/Fonts/STHeiti Medium.ttc",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/Library/Fonts/Arial Unicode.ttf",
        // Windows
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\simsun.ttc",
        "C:\\Windows\\Fonts\\msgothic.ttc",
        "C:\\Windows\\Fonts\\msmincho.ttc",
        // Linux
        "/usr/share/fonts/truetype/droid/DroidSansFallback.ttf",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/wqy-microhei/wqy-microhei.ttc",
        "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc"
    };
    for (const auto& p : paths) {
        if (is_valid_ttf(p)) {
            return p;
        }
    }
    return "";
}

std::string find_korean_font() {
    std::vector<std::string> paths = {
        // macOS
        "/System/Library/Fonts/AppleSDGothicNeo.ttc",
        "/System/Library/Fonts/Supplemental/AppleGothic.ttf",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/System/Library/Fonts/STHeiti Light.ttc",
        // Windows
        "C:\\Windows\\Fonts\\malgun.ttf",
        "C:\\Windows\\Fonts\\malgunbd.ttf",
        // Linux
        "/usr/share/fonts/truetype/nanum/NanumBarunGothic.ttf",
        "/usr/share/fonts/truetype/nanum/NanumGothic.ttf",
        "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/droid/DroidSansFallback.ttf"
    };
    for (const auto& p : paths) {
        if (is_valid_ttf(p)) {
            return p;
        }
    }
    return "";
}

std::string find_inuktitut_font() {
    std::vector<std::string> paths = {
        // macOS
        "/System/Library/Fonts/Supplemental/EuphemiaCAS.ttc",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/Library/Fonts/Arial Unicode.ttf",
        // Windows
        "C:\\Windows\\Fonts\\euphemia.ttf",
        "C:\\Windows\\Fonts\\gadugi.ttf"
    };
    for (const auto& p : paths) {
        if (is_valid_ttf(p)) {
            return p;
        }
    }
    return "";
}

std::string find_egyptian_font() {
    std::vector<std::string> paths = {
        // macOS
        "/System/Library/Fonts/Supplemental/NotoSansEgyptianHieroglyphs-Regular.ttf",
        "/System/Library/Fonts/Apple Symbols.ttf",
        "/System/Library/Fonts/Symbol.ttf"
    };
    for (const auto& p : paths) {
        if (is_valid_ttf(p)) {
            return p;
        }
    }
    return "";
}

std::string find_hindi_font() {
    std::vector<std::string> paths = {
        "/System/Library/Fonts/Supplemental/KohinoorDevanagari.ttc",
        "/System/Library/Fonts/KohinoorDevanagari.ttc",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "C:\\Windows\\Fonts\\mangal.ttf"
    };
    for (const auto& p : paths) {
        if (is_valid_ttf(p)) return p;
    }
    return "";
}

std::string find_bengali_font() {
    std::vector<std::string> paths = {
        "/System/Library/Fonts/Supplemental/KohinoorBangla.ttc",
        "/System/Library/Fonts/KohinoorBangla.ttc",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "C:\\Windows\\Fonts\\vrinda.ttf",
        "C:\\Windows\\Fonts\\Shonar.ttf"
    };
    for (const auto& p : paths) {
        if (is_valid_ttf(p)) return p;
    }
    return "";
}

std::string find_tamil_font() {
    std::vector<std::string> paths = {
        "/System/Library/Fonts/Supplemental/KohinoorTamil.ttc",
        "/System/Library/Fonts/KohinoorTamil.ttc",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "C:\\Windows\\Fonts\\latha.ttf"
    };
    for (const auto& p : paths) {
        if (is_valid_ttf(p)) return p;
    }
    return "";
}

std::string find_thai_font() {
    std::vector<std::string> paths = {
        "/System/Library/Fonts/Thonburi.ttc",
        "/System/Library/Fonts/Supplemental/Ayuthaya.ttf",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "C:\\Windows\\Fonts\\tahoma.ttf",
        "C:\\Windows\\Fonts\\leelawad.ttf"
    };
    for (const auto& p : paths) {
        if (is_valid_ttf(p)) return p;
    }
    return "";
}

void merge_asian_fonts(ImGuiIO& io, float size) {
    std::string hindi_font = find_hindi_font();
    if (!hindi_font.empty()) {
        ImFontConfig merge_config;
        merge_config.MergeMode = true;
        merge_config.PixelSnapH = true;
        static const ImWchar HindiRanges[] = { 0x0900, 0x097F, 0 };
        io.Fonts->AddFontFromFileTTF(hindi_font.c_str(), size, &merge_config, HindiRanges);
    }
    std::string bengali_font = find_bengali_font();
    if (!bengali_font.empty()) {
        ImFontConfig merge_config;
        merge_config.MergeMode = true;
        merge_config.PixelSnapH = true;
        static const ImWchar BengaliRanges[] = { 0x0980, 0x09FF, 0 };
        io.Fonts->AddFontFromFileTTF(bengali_font.c_str(), size, &merge_config, BengaliRanges);
    }
    std::string tamil_font = find_tamil_font();
    if (!tamil_font.empty()) {
        ImFontConfig merge_config;
        merge_config.MergeMode = true;
        merge_config.PixelSnapH = true;
        static const ImWchar TamilRanges[] = { 0x0B80, 0x0BFF, 0 };
        io.Fonts->AddFontFromFileTTF(tamil_font.c_str(), size, &merge_config, TamilRanges);
    }
    std::string thai_font = find_thai_font();
    if (!thai_font.empty()) {
        ImFontConfig merge_config;
        merge_config.MergeMode = true;
        merge_config.PixelSnapH = true;
        static const ImWchar ThaiRanges[] = { 0x0E00, 0x0E7F, 0 };
        io.Fonts->AddFontFromFileTTF(thai_font.c_str(), size, &merge_config, ThaiRanges);
    }
}

std::string find_fallback_font(bool bold) {
    std::vector<std::string> paths;
    if (bold) {
        paths = {
            // macOS
            "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
            "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
            "/System/Library/Fonts/Helvetica.ttc",
            // Windows
            "C:\\Windows\\Fonts\\arialbd.ttf",
            // Linux
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
            "/usr/share/fonts/truetype/freefont/FreeSansBold.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"
        };
    } else {
        paths = {
            // macOS
            "/System/Library/Fonts/Supplemental/Arial.ttf",
            "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
            "/System/Library/Fonts/Helvetica.ttc",
            // Windows
            "C:\\Windows\\Fonts\\arial.ttf",
            // Linux
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"
        };
    }
    for (const auto& p : paths) {
        if (is_valid_ttf(p)) {
            return p;
        }
    }
    return "";
}
// ============================================================================
// Helper Utilities
// ============================================================================

/**
 * @brief Parse the results CSV file into a vector of records.
 */
std::vector<CsvRecord> read_results_csv(const std::string& path) {
    std::vector<CsvRecord> records;
    std::ifstream file(path);
    if (!file.is_open()) return records;

    std::string header_line;
    if (!std::getline(file, header_line)) return records;

    std::vector<std::string> headers;
    std::stringstream hss(header_line);
    std::string hcell;
    while (std::getline(hss, hcell, ',')) {
        while (!hcell.empty() && (hcell.back() == '\r' || hcell.back() == '\n' || hcell.back() == ' ')) hcell.pop_back();
        while (!hcell.empty() && hcell.front() == ' ') hcell.erase(hcell.begin());
        headers.push_back(hcell);
    }

    auto get_col_idx = [&](const std::string& name) -> int {
        for (size_t i = 0; i < headers.size(); ++i) {
            if (headers[i] == name) return static_cast<int>(i);
        }
        return -1;
    };

    int idx_roi = get_col_idx("ROI");
    int idx_subj = get_col_idx("SubjNum");
    int idx_exp = get_col_idx("Exp");
    int idx_init_amp = get_col_idx("InitAmp");
    int idx_init_t2p = get_col_idx("InitT2p");
    int idx_init_fwhm = get_col_idx("InitFWHM");
    int idx_init_m = get_col_idx("InitM");
    int idx_init_snr = get_col_idx("InitSNR");
    int idx_init_cnr = get_col_idx("InitCNR");
    int idx_start = get_col_idx("Click1_Start_T");
    int idx_onset = get_col_idx("Click2_Onset_T");
    int idx_peak = get_col_idx("Click3_Peak_T");
    int idx_end = get_col_idx("Click4_End_T");
    int idx_f_amp = get_col_idx("F_Amp");
    int idx_f_t2p = get_col_idx("F_T2p");
    int idx_f_fwhm = get_col_idx("F_FWHM");
    int idx_f_m = get_col_idx("F_M");
    int idx_f_snr = get_col_idx("F_SNR");
    int idx_f_cnr = get_col_idx("F_CNR");
    int idx_auc = get_col_idx("AUC");
    int idx_aucn = get_col_idx("AUCn");
    int idx_ttlb = get_col_idx("TTlb");
    int idx_ttm = get_col_idx("TTm");
    int idx_tthb = get_col_idx("TThb");
    int idx_ont = get_col_idx("OnT");
    int idx_ont_sc = get_col_idx("OnTSc");
    int idx_roi_size = get_col_idx("ROISize");
    int idx_denoise = get_col_idx("Denoise_RMS");
    int idx_raw_sd_base = get_col_idx("raw_sd_base");
    int idx_stall_flag = get_col_idx("Stall_Flag");
    int idx_ves = get_col_idx("VesType");
    int idx_qc = get_col_idx("QC_Flag");
    int idx_source = get_col_idx("Fit_Source");

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::vector<std::string> cells;
        std::string cell;
        while (std::getline(ss, cell, ',')) {
            while (!cell.empty() && (cell.back() == '\r' || cell.back() == '\n')) cell.pop_back();
            cells.push_back(cell);
        }
        while (cells.size() < headers.size()) {
            cells.push_back("");
        }

        CsvRecord rec;
        auto parse_double = [&](int idx) -> double {
            if (idx >= 0 && idx < static_cast<int>(cells.size()) && !cells[idx].empty()) {
                try { return std::stod(cells[idx]); } catch (...) {}
            }
            return std::numeric_limits<double>::quiet_NaN();
        };
        auto parse_int = [&](int idx) -> int {
            if (idx >= 0 && idx < static_cast<int>(cells.size()) && !cells[idx].empty()) {
                try { return std::stoi(cells[idx]); } catch (...) {}
            }
            return 0;
        };
        auto parse_str = [&](int idx) -> std::string {
            if (idx >= 0 && idx < static_cast<int>(cells.size())) {
                return cells[idx];
            }
            return "";
        };

        if (idx_roi >= 0) rec.roi_id = parse_int(idx_roi);
        if (idx_subj >= 0) rec.subj_num = parse_int(idx_subj);
        if (idx_exp >= 0) rec.exp = parse_str(idx_exp);
        rec.init_amp = parse_double(idx_init_amp);
        rec.init_t2p = parse_double(idx_init_t2p);
        rec.init_fwhm = parse_double(idx_init_fwhm);
        rec.init_m = parse_double(idx_init_m);
        rec.init_snr = parse_double(idx_init_snr);
        rec.init_cnr = parse_double(idx_init_cnr);
        rec.click_start = parse_double(idx_start);
        rec.click_onset = parse_double(idx_onset);
        rec.click_peak = parse_double(idx_peak);
        rec.click_end = parse_double(idx_end);

        rec.f_amp = parse_double(idx_f_amp);
        rec.f_t2p = parse_double(idx_f_t2p);
        rec.f_fwhm = parse_double(idx_f_fwhm);
        rec.f_m = parse_double(idx_f_m);
        rec.f_snr = parse_double(idx_f_snr);
        rec.f_cnr = parse_double(idx_f_cnr);

        rec.auc = parse_double(idx_auc);
        rec.aucn = parse_double(idx_aucn);
        rec.ttlb = parse_double(idx_ttlb);
        rec.ttm = parse_double(idx_ttm);
        rec.tthb = parse_double(idx_tthb);
        rec.ont = parse_double(idx_ont);
        rec.ont_sc = parse_double(idx_ont_sc);

        if (idx_roi_size >= 0) rec.roi_size = parse_int(idx_roi_size);
        rec.denoise_rms = parse_double(idx_denoise);
        if (idx_raw_sd_base >= 0) rec.raw_sd_base = parse_double(idx_raw_sd_base);
        if (idx_stall_flag >= 0) rec.stall_flag = parse_int(idx_stall_flag);
        if (idx_ves >= 0) rec.ves_type = parse_str(idx_ves);
        if (idx_qc >= 0) rec.qc_flag = parse_str(idx_qc);
        if (idx_source >= 0) rec.fit_source = parse_str(idx_source);

        records.push_back(rec);
    }
    return records;
}

/**
 * @brief Write the updated records vector back to the results CSV.
 */
void save_results_csv(const std::string& path, const std::vector<CsvRecord>& records) {
    std::ofstream out(path);
    if (!out.is_open()) return;

    out << "ROI,SubjNum,Exp,InitAmp,InitT2p,InitFWHM,InitM,InitSNR,InitCNR,"
           "Click1_Start_T,Click2_Onset_T,Click3_Peak_T,Click4_End_T,"
           "F_Amp,F_T2p,F_FWHM,F_M,F_SNR,F_CNR,AUC,AUCn,TTlb,TTm,TThb,OnT,OnTSc,ROISize,Denoise_RMS,VesType,QC_Flag,Fit_Source,Stall_Flag\n";

    for (const auto& rec : records) {
        auto format_double = [](double v) -> std::string {
            if (std::isnan(v)) return "";
            std::stringstream ss;
            ss << v;
            return ss.str();
        };

        out << rec.roi_id << ","
            << rec.subj_num << ","
            << rec.exp << ","
            << format_double(rec.init_amp) << ","
            << format_double(rec.init_t2p) << ","
            << format_double(rec.init_fwhm) << ","
            << format_double(rec.init_m) << ","
            << format_double(rec.init_snr) << ","
            << format_double(rec.init_cnr) << ","
            << format_double(rec.click_start) << ","
            << format_double(rec.click_onset) << ","
            << format_double(rec.click_peak) << ","
            << format_double(rec.click_end) << ","
            << format_double(rec.f_amp) << ","
            << format_double(rec.f_t2p) << ","
            << format_double(rec.f_fwhm) << ","
            << format_double(rec.f_m) << ","
            << format_double(rec.f_snr) << ","
            << format_double(rec.f_cnr) << ","
            << format_double(rec.auc) << ","
            << format_double(rec.aucn) << ","
            << format_double(rec.ttlb) << ","
            << format_double(rec.ttm) << ","
            << format_double(rec.tthb) << ","
            << format_double(rec.ont) << ","
            << format_double(rec.ont_sc) << ","
            << rec.roi_size << ","
            << format_double(rec.denoise_rms) << ","
            << rec.ves_type << ","
            << rec.qc_flag << ","
            << rec.fit_source << ","
            << rec.stall_flag << "\n";
    }
}

/**
 * @brief Search for the ROI text file in the same directory or subdirectories.
 */
std::string find_rois_txt_file(const std::string& tiff_path) {
    std::filesystem::path tp(tiff_path);
    std::filesystem::path parent = tp.parent_path();
    std::string stem = tp.stem().string();
    
    for (const auto& entry : std::filesystem::recursive_directory_iterator(parent)) {
        if (entry.is_regular_file()) {
            std::string name = entry.path().filename().string();
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            std::string l_stem = stem;
            std::transform(l_stem.begin(), l_stem.end(), l_stem.begin(), ::tolower);
            if (name.find(l_stem) != std::string::npos && name.find("rois") != std::string::npos && name.find(".txt") != std::string::npos) {
                return entry.path().string();
            }
        }
    }
    return "";
}

std::string find_rois_mat_file(const std::string& tiff_path) {
    std::filesystem::path tp(tiff_path);
    std::filesystem::path parent = tp.parent_path();
    std::string stem = tp.stem().string();
    
    for (const auto& entry : std::filesystem::recursive_directory_iterator(parent)) {
        if (entry.is_regular_file()) {
            std::string name = entry.path().filename().string();
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            std::string l_stem = stem;
            std::transform(l_stem.begin(), l_stem.end(), l_stem.begin(), ::tolower);
            if (name.find(l_stem) != std::string::npos && 
                (name.find("mask") != std::string::npos || name.find("maskobj") != std::string::npos) && 
                name.find(".mat") != std::string::npos) {
                return entry.path().string();
            }
        }
    }
    return "";
}

/**
 * @brief Search for the metadata text file in the same directory or subdirectories.
 */
std::string find_meta_txt_file(const std::string& tiff_path) {
    std::filesystem::path tp(tiff_path);
    std::filesystem::path parent = tp.parent_path();
    std::string stem = tp.stem().string();
    
    for (const auto& entry : std::filesystem::recursive_directory_iterator(parent)) {
        if (entry.is_regular_file()) {
            std::string name = entry.path().filename().string();
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            std::string l_stem = stem;
            std::transform(l_stem.begin(), l_stem.end(), l_stem.begin(), ::tolower);
            if (name.find(l_stem) != std::string::npos && name.find(".txt") != std::string::npos && name.find("rois") == std::string::npos) {
                return entry.path().string();
            }
        }
    }
    return "";
}

/**
 * @brief Load all frames of a multi-page TIFF stack.
 */
TiffData load_tiff(const std::string& path) {
    TIFFSetWarningHandler(nullptr);
    TiffData data;
    TIFF* tif = TIFFOpen(path.c_str(), "r");
    if (!tif) return data;
    
    TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &data.width);
    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &data.height);
    
    tsize_t scanline_size = TIFFScanlineSize(tif);
    tdata_t buf = _TIFFmalloc(scanline_size);
    if (!buf) {
        TIFFClose(tif);
        return data;
    }
    
    do {
        std::vector<float> frame(data.width * data.height);
        uint16_t bitspersample = 8;
        uint16_t sampleformat = 1;
        TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bitspersample);
        TIFFGetField(tif, TIFFTAG_SAMPLEFORMAT, &sampleformat);
        
        tsize_t current_scanline_size = TIFFScanlineSize(tif);
        if (current_scanline_size > scanline_size) {
            _TIFFfree(buf);
            scanline_size = current_scanline_size;
            buf = _TIFFmalloc(scanline_size);
            if (!buf) {
                TIFFClose(tif);
                return data;
            }
        }
        
        if (bitspersample == 16) {
            for (uint32_t row = 0; row < data.height; row++) {
                TIFFReadScanline(tif, buf, row);
                uint16_t* row_ptr = reinterpret_cast<uint16_t*>(buf);
                for (uint32_t col = 0; col < data.width; col++) {
                    frame[row * data.width + col] = static_cast<float>(row_ptr[col]);
                }
            }
        } else if (bitspersample == 8) {
            for (uint32_t row = 0; row < data.height; row++) {
                TIFFReadScanline(tif, buf, row);
                uint8_t* row_ptr = reinterpret_cast<uint8_t*>(buf);
                for (uint32_t col = 0; col < data.width; col++) {
                    frame[row * data.width + col] = static_cast<float>(row_ptr[col]);
                }
            }
        } else if (bitspersample == 32 && sampleformat == 3) {
            for (uint32_t row = 0; row < data.height; row++) {
                TIFFReadScanline(tif, buf, row);
                float* row_ptr = reinterpret_cast<float*>(buf);
                for (uint32_t col = 0; col < data.width; col++) {
                    frame[row * data.width + col] = row_ptr[col];
                }
            }
        }
        data.frames.push_back(frame);
    } while (TIFFReadDirectory(tif));
    
    _TIFFfree(buf);
    TIFFClose(tif);
    
    if (!data.frames.empty()) {
        size_t num_pixels = data.width * data.height;
        data.mip.assign(num_pixels, 0.0f);
        for (const auto& frame : data.frames) {
            for (size_t i = 0; i < num_pixels; ++i) {
                data.mip[i] += frame[i];
            }
        }
        for (size_t i = 0; i < num_pixels; ++i) {
            data.mip[i] /= data.frames.size();
        }
    }
    
    return data;
}

/**
 * @brief Load all polygon ROIs from a text file.
 */
std::vector<ROI> load_rois_txt(const std::string& path) {
    std::vector<ROI> rois;
    std::ifstream rois_file(path);
    if (!rois_file.is_open()) return rois;
    
    int n_rois = 0;
    rois_file >> n_rois;
    rois.resize(n_rois);
    for (int i = 0; i < n_rois; ++i) {
        int roi_id, n_pts;
        rois_file >> roi_id >> n_pts;
        rois[i].id = roi_id;
        rois[i].poly.resize(n_pts);
        for (int j = 0; j < n_pts; ++j) {
            rois_file >> rois[i].poly[j].first >> rois[i].poly[j].second;
        }
    }
    return rois;
}

/**
 * @brief Parse camera frame rate from the Fluoview metadata file.
 */
double parse_frame_rate(const std::string& filepath) {
    std::ifstream f(filepath);
    if (!f.is_open()) return 1.0;
    std::string line;
    while (std::getline(f, line)) {
        if (line.find("\"T Dimension\"") != std::string::npos) {
            size_t pos = line.find("\"T Dimension\"");
            pos = line.find("\"", pos + 13);
            if (pos == std::string::npos) continue;
            std::string val = line.substr(pos + 1);
            std::stringstream ss(val);
            double frames = 0, t_start = 0, t_end = 0;
            char comma = 0, dash = 0;
            ss >> frames >> comma >> t_start >> dash >> t_end;
            if (frames > 0 && t_end > t_start) {
                return std::round((frames / (t_end - t_start)) * 100.0) / 100.0;
            }
        }
    }
    return 1.0;
}

// ============================================================================
// File Browser Component
// ============================================================================

FileBrowser::FileBrowser() {
    current_path = std::filesystem::current_path();
    refresh();
}

void FileBrowser::refresh() {
    entries.clear();
    try {
        if (current_path.has_parent_path() && current_path != current_path.root_path()) {
            entries.push_back({"..", true});
        }
        
        for (const auto& entry : std::filesystem::directory_iterator(current_path)) {
            std::string name = entry.path().filename().string();
            if (name.empty() || name.front() == '.') continue;
            entries.push_back({name, entry.is_directory()});
        }
        
        std::sort(entries.begin(), entries.end(), [](const DirEntry& a, const DirEntry& b) {
            if (a.is_dir != b.is_dir) return a.is_dir;
            return a.name < b.name;
        });
    } catch (...) {}
}

void FileBrowser::draw(const char* title) {
    if (!open) return;
    const char* actual_title = tr ? tr->dialog_title.c_str() : title;
    ImGui::OpenPopup(actual_title);
    ImGui::SetNextWindowSize(ImVec2(600, 450), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(actual_title, &open, 0)) {
        ImGui::Text(tr ? (tr->current_folder + ": %s").c_str() : "Current Folder: %s", current_path.string().c_str());
        
        char path_buf[1024];
        strncpy(path_buf, current_path.string().c_str(), sizeof(path_buf));
        if (ImGui::InputText(tr ? tr->path_selector.c_str() : "Path Selector", path_buf, sizeof(path_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
            std::filesystem::path p(path_buf);
            if (std::filesystem::exists(p) && std::filesystem::is_directory(p)) {
                current_path = p;
                refresh();
            }
        }
        
        ImGui::BeginChild("FileListPane", ImVec2(0, 300), true);
        for (const auto& entry : entries) {
            if (entry.is_dir) {
                if (ImGui::Selectable((entry.name + "/").c_str(), false)) {
                    if (entry.name == "..") {
                        current_path = current_path.parent_path();
                    } else {
                        current_path /= entry.name;
                    }
                    refresh();
                    break;
                }
            } else {
                std::string ext = std::filesystem::path(entry.name).extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".csv" || ext == ".tif" || ext == ".tiff") {
                    if (ImGui::Selectable(entry.name.c_str(), selected_file == entry.name)) {
                        selected_file = entry.name;
                    }
                }
            }
        }
        ImGui::EndChild();
        
        if (ImGui::Button(tr ? tr->btn_select_folder.c_str() : "Select Current Folder", ImVec2(180, 0))) {
            selected_file = "";
            open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (!selected_file.empty()) {
            if (ImGui::Button(tr ? tr->btn_open_file.c_str() : "Open Selected File", ImVec2(180, 0))) {
                open = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(tr ? tr->btn_close_dialog.c_str() : "Close Dialog", ImVec2(120, 0))) {
            open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ============================================================================
// Custom range slider for visual crop selection
// ============================================================================
static bool RangeSlider(const char* id_str, double* v_min, double* v_max, double v_min_limit, double v_max_limit, const ImVec2& size, const char* label) {
    ImGui::PushID(id_str);
    
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    
    // Add an invisible button to handle inputs
    ImGui::InvisibleButton("slider_bar", size);
    bool is_active = ImGui::IsItemActive();
    
    float width = size.x;
    float height = size.y;
    float handle_width = 12.0f;
    float track_width = width - handle_width;
    float track_start_x = pos.x + handle_width * 0.5f;
    
    double range_limit = v_max_limit - v_min_limit;
    if (range_limit <= 0.0) range_limit = 1.0;
    
    // Normalize coordinates
    float t_min = (float)((*v_min - v_min_limit) / range_limit);
    float t_max = (float)((*v_max - v_min_limit) / range_limit);
    t_min = std::max(0.0f, std::min(1.0f, t_min));
    t_max = std::max(0.0f, std::min(1.0f, t_max));
    
    float x_min = track_start_x + t_min * track_width;
    float x_max = track_start_x + t_max * track_width;
    
    // Retrieve state from ImGui storage instead of static variables
    ImGuiStorage* storage = ImGui::GetStateStorage();
    ImGuiID dragging_handle_id = ImGui::GetID("##dragging_handle");
    ImGuiID start_t_min_id = ImGui::GetID("##start_t_min");
    ImGuiID start_t_max_id = ImGui::GetID("##start_t_max");
    ImGuiID drag_start_x_id = ImGui::GetID("##drag_start_x");
    
    int dragging_handle = storage->GetInt(dragging_handle_id, 0);
    float start_t_min = storage->GetFloat(start_t_min_id, 0.0f);
    float start_t_max = storage->GetFloat(start_t_max_id, 0.0f);
    float drag_start_x = storage->GetFloat(drag_start_x_id, 0.0f);
    
    if (ImGui::IsItemActivated()) {
        ImVec2 mouse_pos = ImGui::GetIO().MousePos;
        float mx = mouse_pos.x;
        
        // Check distance to min/max handle
        float dist_min = std::abs(mx - x_min);
        float dist_max = std::abs(mx - x_max);
        
        drag_start_x = mx;
        start_t_min = t_min;
        start_t_max = t_max;
        
        if (dist_min < handle_width && dist_min <= dist_max) {
            dragging_handle = 1;
        } else if (dist_max < handle_width) {
            dragging_handle = 2;
        } else if (mx >= x_min && mx <= x_max) {
            dragging_handle = 3;
        } else {
            // Click outside - jump closest handle to mouse
            float click_t = (mx - track_start_x) / track_width;
            click_t = std::max(0.0f, std::min(1.0f, click_t));
            if (std::abs(click_t - t_min) < std::abs(click_t - t_max)) {
                t_min = std::min(click_t, t_max - 0.01f);
                dragging_handle = 1;
            } else {
                t_max = std::max(click_t, t_min + 0.01f);
                dragging_handle = 2;
            }
            *v_min = v_min_limit + t_min * range_limit;
            *v_max = v_min_limit + t_max * range_limit;
            start_t_min = t_min;
            start_t_max = t_max;
        }
        
        storage->SetInt(dragging_handle_id, dragging_handle);
        storage->SetFloat(start_t_min_id, start_t_min);
        storage->SetFloat(start_t_max_id, start_t_max);
        storage->SetFloat(drag_start_x_id, drag_start_x);
    }
    
    bool changed = false;
    if (is_active && dragging_handle != 0) {
        float dx = ImGui::GetIO().MousePos.x - drag_start_x;
        float dt = dx / track_width;
        
        if (dragging_handle == 1) {
            float new_t = start_t_min + dt;
            new_t = std::max(0.0f, std::min(start_t_max - 0.01f, new_t));
            *v_min = v_min_limit + new_t * range_limit;
            changed = true;
        } else if (dragging_handle == 2) {
            float new_t = start_t_max + dt;
            new_t = std::max(start_t_min + 0.01f, std::min(1.0f, new_t));
            *v_max = v_min_limit + new_t * range_limit;
            changed = true;
        } else if (dragging_handle == 3) {
            float new_t_min = start_t_min + dt;
            float new_t_max = start_t_max + dt;
            float len = start_t_max - start_t_min;
            if (new_t_min < 0.0f) {
                new_t_min = 0.0f;
                new_t_max = len;
            } else if (new_t_max > 1.0f) {
                new_t_max = 1.0f;
                new_t_min = 1.0f - len;
            }
            *v_min = v_min_limit + new_t_min * range_limit;
            *v_max = v_min_limit + new_t_max * range_limit;
            changed = true;
        }
    }
    
    if (!is_active && dragging_handle != 0) {
        dragging_handle = 0;
        storage->SetInt(dragging_handle_id, 0);
    }
    
    ImGuiStyle& style = ImGui::GetStyle();
    // Render slider background
    ImU32 bg_color = ImGui::GetColorU32(ImGuiCol_FrameBg);
    ImVec4 slider_grab = style.Colors[ImGuiCol_SliderGrab];
    ImVec4 slider_grab_active = style.Colors[ImGuiCol_SliderGrabActive];
    ImU32 active_track_color = ImGui::GetColorU32(ImVec4(slider_grab.x, slider_grab.y, slider_grab.z, 0.60f));
    ImU32 border_color = ImGui::GetColorU32(ImGuiCol_Border);
    
    // Choose a high-contrast handle color that stands out from the track
    ImU32 handle_color = ImGui::GetColorU32(ImVec4(slider_grab.x * 0.85f + 0.15f, slider_grab.y * 0.85f + 0.15f, slider_grab.z * 0.85f + 0.15f, 1.00f));
    if (is_active && (dragging_handle == 1 || dragging_handle == 2 || dragging_handle == 3)) {
        handle_color = ImGui::GetColorU32(slider_grab_active);
    } else {
        ImVec2 mouse_pos = ImGui::GetIO().MousePos;
        float mx = mouse_pos.x;
        float dist_min = std::abs(mx - x_min);
        float dist_max = std::abs(mx - x_max);
        if (ImGui::IsItemHovered()) {
            if (dist_min < handle_width || dist_max < handle_width) {
                handle_color = ImGui::GetColorU32(ImVec4(slider_grab.x * 0.70f + 0.30f, slider_grab.y * 0.70f + 0.30f, slider_grab.z * 0.70f + 0.30f, 1.00f));
            }
        }
    }
    
    // Draw background track
    draw_list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), bg_color, style.FrameRounding);
    draw_list->AddRect(pos, ImVec2(pos.x + width, pos.y + height), border_color, style.FrameRounding);
    
    // Draw active range track (goes between handle centers)
    ImVec2 active_min(x_min, pos.y);
    ImVec2 active_max(x_max, pos.y + height);
    draw_list->AddRectFilled(active_min, active_max, active_track_color, 0.0f);
    
    // Draw handles (rounded pills protruding by 4px on top and bottom)
    float protrude = 4.0f;
    ImVec2 h1_min(x_min - handle_width * 0.5f, pos.y - protrude);
    ImVec2 h1_max(x_min + handle_width * 0.5f, pos.y + height + protrude);
    draw_list->AddRectFilled(h1_min, h1_max, handle_color, style.GrabRounding);
    draw_list->AddRect(h1_min, h1_max, border_color, style.GrabRounding);
    
    ImVec2 h2_min(x_max - handle_width * 0.5f, pos.y - protrude);
    ImVec2 h2_max(x_max + handle_width * 0.5f, pos.y + height + protrude);
    draw_list->AddRectFilled(h2_min, h2_max, handle_color, style.GrabRounding);
    draw_list->AddRect(h2_min, h2_max, border_color, style.GrabRounding);
 
    // Draw tactile grab ridges inside handles
    auto draw_ridges = [&](float x_center) {
        float cy = pos.y + height * 0.5f;
        for (int offset = -4; offset <= 4; offset += 4) {
            float ry = cy + offset;
            draw_list->AddLine(ImVec2(x_center - 3.0f, ry), ImVec2(x_center + 3.0f, ry), border_color, 1.0f);
        }
    };
    draw_ridges(x_min);
    draw_ridges(x_max);
    
    // Add text label overlay inside the slider
    char label_buf[128];
    snprintf(label_buf, sizeof(label_buf), "%s: %.1fs - %.1fs", label, *v_min, *v_max);
    ImVec2 text_size = ImGui::CalcTextSize(label_buf);
    ImVec2 text_pos(pos.x + (width - text_size.x) * 0.5f, pos.y + (height - text_size.y) * 0.5f);
    draw_list->AddText(text_pos, ImGui::GetColorU32(ImGuiCol_Text), label_buf);
    
    ImGui::PopID();
    return changed;
}

// ============================================================================
// Main Application Class
// ============================================================================

BolusApp::BolusApp() : m_fitter(1e-6, 1023.0, 1e-6, 1e6, 0.5, 1e6), m_denoise_strength_factor(1.0f), m_showing_intro(true), m_intro_start_time(-1.0), m_any_item_active_prev(false) {}

BolusApp::~BolusApp() {
    if (m_mip_texture_id != 0) {
        glDeleteTextures(1, &m_mip_texture_id);
    }
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    if (m_window) {
        glfwDestroyWindow(m_window);
        glfwTerminate();
    }
}

bool BolusApp::init() {
    if (!glfwInit()) return false;
    
#if defined(__APPLE__)
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif
    
    m_window = glfwCreateWindow(1600, 950, "Bolus Tracking GUI - Triage App", NULL, NULL);
    if (!m_window) return false;
    
    glfwMakeContextCurrent(m_window);
    glfwMaximizeWindow(m_window);
    glfwSwapInterval(1);
    
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    
    // Sleek Premium Mid-Century Modern Theme
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 14.0f;
    style.ChildRounding = 12.0f;
    style.FrameRounding = 10.0f;
    style.GrabRounding = 8.0f;
    style.PopupRounding = 12.0f;
    style.ScrollbarRounding = 10.0f;
    style.TabRounding = 8.0f;
        
        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;
        style.FrameBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.ItemSpacing = ImVec2(12.0f, 10.0f);
        style.FramePadding = ImVec2(8.0f, 6.0f);
        style.WindowPadding = ImVec2(16.0f, 16.0f);
        
        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text]                   = ImVec4(0.95f, 0.94f, 0.90f, 1.00f); // Warm cream text
        colors[ImGuiCol_TextDisabled]           = ImVec4(0.60f, 0.58f, 0.55f, 1.00f);
        colors[ImGuiCol_WindowBg]               = ImVec4(0.18f, 0.18f, 0.17f, 1.00f); // Warm charcoal base
        colors[ImGuiCol_ChildBg]                = ImVec4(0.22f, 0.22f, 0.20f, 0.95f); // Panel backdrop (dark wood tone-ish)
        colors[ImGuiCol_PopupBg]                = ImVec4(0.20f, 0.20f, 0.19f, 0.98f);
        colors[ImGuiCol_Border]                 = ImVec4(0.35f, 0.32f, 0.28f, 0.50f); // Muted warm brown/bronze border
        colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg]                = ImVec4(0.26f, 0.25f, 0.23f, 1.00f); // Sandbox input fields
        colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.32f, 0.30f, 0.28f, 1.00f);
        colors[ImGuiCol_FrameBgActive]          = ImVec4(0.38f, 0.35f, 0.32f, 1.00f);
        colors[ImGuiCol_TitleBg]                = ImVec4(0.28f, 0.25f, 0.22f, 1.00f); // Mustard-accented header space
        colors[ImGuiCol_TitleBgActive]          = ImVec4(0.32f, 0.28f, 0.24f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.20f, 0.18f, 0.16f, 1.00f);
        colors[ImGuiCol_MenuBarBg]              = ImVec4(0.22f, 0.22f, 0.20f, 1.00f);
        colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.18f, 0.18f, 0.17f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.40f, 0.38f, 0.34f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.50f, 0.46f, 0.42f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.60f, 0.55f, 0.50f, 1.00f);
        colors[ImGuiCol_CheckMark]              = ImVec4(0.88f, 0.55f, 0.25f, 1.00f); // Burnt orange checks
        colors[ImGuiCol_SliderGrab]             = ImVec4(0.50f, 0.58f, 0.45f, 1.00f); // Sage green grab
        colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.88f, 0.55f, 0.25f, 1.00f); // Burnt orange highlight
        colors[ImGuiCol_Button]                 = ImVec4(0.38f, 0.42f, 0.35f, 1.00f); // Muted avocado / sage button
        colors[ImGuiCol_ButtonHovered]          = ImVec4(0.46f, 0.52f, 0.42f, 1.00f);
        colors[ImGuiCol_ButtonActive]           = ImVec4(0.88f, 0.55f, 0.25f, 1.00f); // Burnt orange active clicks
        colors[ImGuiCol_Header]                 = ImVec4(0.35f, 0.38f, 0.32f, 1.00f); // Header lists
        colors[ImGuiCol_HeaderHovered]          = ImVec4(0.42f, 0.46f, 0.38f, 1.00f);
        colors[ImGuiCol_HeaderActive]           = ImVec4(0.50f, 0.55f, 0.45f, 1.00f);
        colors[ImGuiCol_Separator]              = ImVec4(0.35f, 0.32f, 0.28f, 0.50f);
        colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.88f, 0.55f, 0.25f, 0.78f);
        colors[ImGuiCol_SeparatorActive]        = ImVec4(0.88f, 0.55f, 0.25f, 1.00f);
        colors[ImGuiCol_ResizeGrip]             = ImVec4(0.38f, 0.42f, 0.35f, 0.20f);
        colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.38f, 0.42f, 0.35f, 0.67f);
        colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.88f, 0.55f, 0.25f, 0.95f);
        colors[ImGuiCol_Tab]                    = ImVec4(0.28f, 0.30f, 0.26f, 0.86f);
        colors[ImGuiCol_TabHovered]             = ImVec4(0.38f, 0.42f, 0.35f, 0.80f);
        colors[ImGuiCol_TabActive]              = ImVec4(0.38f, 0.42f, 0.35f, 1.00f);
        colors[ImGuiCol_TabUnfocused]           = ImVec4(0.20f, 0.22f, 0.18f, 0.97f);
        colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.28f, 0.30f, 0.26f, 1.00f);
        colors[ImGuiCol_PlotLines]              = ImVec4(0.85f, 0.80f, 0.70f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered]       = ImVec4(0.88f, 0.55f, 0.25f, 1.00f);
        colors[ImGuiCol_PlotHistogram]          = ImVec4(0.88f, 0.55f, 0.25f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(0.95f, 0.65f, 0.35f, 1.00f);
        colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.26f, 0.26f, 0.24f, 1.00f);
        colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.35f, 0.35f, 0.32f, 1.00f);
        colors[ImGuiCol_TableBorderLight]       = ImVec4(0.28f, 0.28f, 0.26f, 1.00f);
        colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
        colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.88f, 0.55f, 0.25f, 0.35f);
        colors[ImGuiCol_DragDropTarget]         = ImVec4(0.88f, 0.55f, 0.25f, 0.90f);
        colors[ImGuiCol_NavHighlight]           = ImVec4(0.88f, 0.55f, 0.25f, 1.00f);
        colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
        colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.12f, 0.12f, 0.11f, 0.60f);
        
        // Load custom high-quality MCM Outfit fonts
        ImFontConfig font_config;
        font_config.OversampleH = 3;
        font_config.OversampleV = 3;
        font_config.PixelSnapH = true;
        
        static const ImWchar LatinRanges[] = {
            0x0020, 0x00FF, // Basic Latin + Latin-1 Supplement
            0x0100, 0x017F, // Latin Extended-A (Esperanto: ĉ, ĝ, ĥ, ĵ, ŝ, ŭ)
            0x0180, 0x024F, // Latin Extended-B
            0x1E00, 0x1EFF, // Latin Extended Additional (Vietnamese)
            0
        };
        
        std::string font_reg_path = get_resource_path("resources/fonts/Outfit-Medium.ttf");
        std::string font_bold_path = get_resource_path("resources/fonts/Outfit-Bold.ttf");
        
        std::string cjk_font = find_cjk_font();
        std::string korean_font = find_korean_font();
        std::string klingon_font = get_resource_path("resources/fonts/Klingon-pIqaD.ttf");
        
        static const ImWchar FallbackRanges[] = {
            0x0100, 0x017F, // Latin Extended-A (Esperanto)
            0x0180, 0x024F, // Latin Extended-B
            0x1E00, 0x1EFF, // Latin Extended Additional (Vietnamese)
            0x0400, 0x052F, // Cyrillic (Russian, Ukrainian, Serbian, Bulgarian)
            0x0370, 0x03FF, // Greek
            0x1F00, 0x1FFF, // Greek Extended (Ancient Greek)
            0x1400, 0x167F, // Canadian Aboriginal Syllabics (Inuktitut)
            0
        };

        if (is_valid_ttf(font_reg_path)) {
            m_font_regular = io.Fonts->AddFontFromFileTTF(font_reg_path.c_str(), 16.0f, &font_config, LatinRanges);
            
            if (!cjk_font.empty()) {
                ImFontConfig merge_config;
                merge_config.MergeMode = true;
                merge_config.PixelSnapH = true;
                io.Fonts->AddFontFromFileTTF(cjk_font.c_str(), 16.0f, &merge_config, io.Fonts->GetGlyphRangesChineseFull());
                io.Fonts->AddFontFromFileTTF(cjk_font.c_str(), 16.0f, &merge_config, io.Fonts->GetGlyphRangesJapanese());
            }
            if (!korean_font.empty()) {
                ImFontConfig merge_config;
                merge_config.MergeMode = true;
                merge_config.PixelSnapH = true;
                io.Fonts->AddFontFromFileTTF(korean_font.c_str(), 16.0f, &merge_config, io.Fonts->GetGlyphRangesKorean());
            }
            if (is_valid_ttf(klingon_font)) {
                ImFontConfig merge_config;
                merge_config.MergeMode = true;
                merge_config.PixelSnapH = true;
                static const ImWchar KlingonRanges[] = {
                    0xF8D0, 0xF8FF,
                    0
                };
                io.Fonts->AddFontFromFileTTF(klingon_font.c_str(), 16.0f, &merge_config, KlingonRanges);
            }
            
            std::string fallback_font = find_fallback_font(false);
            if (!fallback_font.empty()) {
                ImFontConfig merge_config;
                merge_config.MergeMode = true;
                merge_config.PixelSnapH = true;
                io.Fonts->AddFontFromFileTTF(fallback_font.c_str(), 16.0f, &merge_config, FallbackRanges);
            }
            std::string inuktitut_font = find_inuktitut_font();
            if (!inuktitut_font.empty()) {
                ImFontConfig merge_config;
                merge_config.MergeMode = true;
                merge_config.PixelSnapH = true;
                io.Fonts->AddFontFromFileTTF(inuktitut_font.c_str(), 16.0f, &merge_config, FallbackRanges);
            }
            std::string egyptian_font = find_egyptian_font();
            if (!egyptian_font.empty()) {
                ImFontConfig merge_config;
                merge_config.MergeMode = true;
                merge_config.PixelSnapH = true;
                static const ImWchar EgyptianRanges[] = {
                    0x13000, 0x1342F,
                    0
                };
                io.Fonts->AddFontFromFileTTF(egyptian_font.c_str(), 16.0f, &merge_config, EgyptianRanges);
            }
            merge_asian_fonts(io, 16.0f);
        }
        
        if (is_valid_ttf(font_bold_path)) {
            m_font_bold = io.Fonts->AddFontFromFileTTF(font_bold_path.c_str(), 18.0f, &font_config, LatinRanges);
            
            if (!cjk_font.empty()) {
                ImFontConfig merge_config;
                merge_config.MergeMode = true;
                merge_config.PixelSnapH = true;
                io.Fonts->AddFontFromFileTTF(cjk_font.c_str(), 18.0f, &merge_config, io.Fonts->GetGlyphRangesChineseFull());
                io.Fonts->AddFontFromFileTTF(cjk_font.c_str(), 18.0f, &merge_config, io.Fonts->GetGlyphRangesJapanese());
            }
            if (!korean_font.empty()) {
                ImFontConfig merge_config;
                merge_config.MergeMode = true;
                merge_config.PixelSnapH = true;
                io.Fonts->AddFontFromFileTTF(korean_font.c_str(), 18.0f, &merge_config, io.Fonts->GetGlyphRangesKorean());
            }
            if (is_valid_ttf(klingon_font)) {
                ImFontConfig merge_config;
                merge_config.MergeMode = true;
                merge_config.PixelSnapH = true;
                static const ImWchar KlingonRanges[] = {
                    0xF8D0, 0xF8FF,
                    0
                };
                io.Fonts->AddFontFromFileTTF(klingon_font.c_str(), 18.0f, &merge_config, KlingonRanges);
            }
            
            std::string fallback_font_bold = find_fallback_font(true);
            if (!fallback_font_bold.empty()) {
                ImFontConfig merge_config;
                merge_config.MergeMode = true;
                merge_config.PixelSnapH = true;
                io.Fonts->AddFontFromFileTTF(fallback_font_bold.c_str(), 18.0f, &merge_config, FallbackRanges);
            }
            std::string inuktitut_font = find_inuktitut_font();
            if (!inuktitut_font.empty()) {
                ImFontConfig merge_config;
                merge_config.MergeMode = true;
                merge_config.PixelSnapH = true;
                io.Fonts->AddFontFromFileTTF(inuktitut_font.c_str(), 18.0f, &merge_config, FallbackRanges);
            }
            std::string egyptian_font = find_egyptian_font();
            if (!egyptian_font.empty()) {
                ImFontConfig merge_config;
                merge_config.MergeMode = true;
                merge_config.PixelSnapH = true;
                static const ImWchar EgyptianRanges[] = {
                    0x13000, 0x1342F,
                    0
                };
                io.Fonts->AddFontFromFileTTF(egyptian_font.c_str(), 18.0f, &merge_config, EgyptianRanges);
            }
            merge_asian_fonts(io, 18.0f);
        }
        
        if (!m_font_regular) {
            m_font_regular = io.Fonts->AddFontDefault();
            if (!cjk_font.empty()) {
                ImFontConfig merge_config;
                merge_config.MergeMode = true;
                merge_config.PixelSnapH = true;
                io.Fonts->AddFontFromFileTTF(cjk_font.c_str(), 13.0f, &merge_config, io.Fonts->GetGlyphRangesChineseFull());
                io.Fonts->AddFontFromFileTTF(cjk_font.c_str(), 13.0f, &merge_config, io.Fonts->GetGlyphRangesJapanese());
            }
            if (!korean_font.empty()) {
                ImFontConfig merge_config;
                merge_config.MergeMode = true;
                merge_config.PixelSnapH = true;
                io.Fonts->AddFontFromFileTTF(korean_font.c_str(), 13.0f, &merge_config, io.Fonts->GetGlyphRangesKorean());
            }
            if (is_valid_ttf(klingon_font)) {
                ImFontConfig merge_config;
                merge_config.MergeMode = true;
                merge_config.PixelSnapH = true;
                static const ImWchar KlingonRanges[] = {
                    0xF8D0, 0xF8FF,
                    0
                };
                io.Fonts->AddFontFromFileTTF(klingon_font.c_str(), 13.0f, &merge_config, KlingonRanges);
            }
            
            std::string fallback_font = find_fallback_font(false);
            if (!fallback_font.empty()) {
                ImFontConfig merge_config;
                merge_config.MergeMode = true;
                merge_config.PixelSnapH = true;
                io.Fonts->AddFontFromFileTTF(fallback_font.c_str(), 13.0f, &merge_config, FallbackRanges);
            }
            std::string inuktitut_font = find_inuktitut_font();
            if (!inuktitut_font.empty()) {
                ImFontConfig merge_config;
                merge_config.MergeMode = true;
                merge_config.PixelSnapH = true;
                io.Fonts->AddFontFromFileTTF(inuktitut_font.c_str(), 13.0f, &merge_config, FallbackRanges);
            }
            std::string egyptian_font = find_egyptian_font();
            if (!egyptian_font.empty()) {
                ImFontConfig merge_config;
                merge_config.MergeMode = true;
                merge_config.PixelSnapH = true;
                static const ImWchar EgyptianRanges[] = {
                    0x13000, 0x1342F,
                    0
                };
                io.Fonts->AddFontFromFileTTF(egyptian_font.c_str(), 13.0f, &merge_config, EgyptianRanges);
            }
            merge_asian_fonts(io, 13.0f);
        }
        if (!m_font_bold) {
            m_font_bold = m_font_regular;
        }


        // Initialize translations
        update_locale();
        apply_theme_colors();
        ensure_minion_squeak_exists();
        m_browser.tr = &m_tr;

        ImGui_ImplGlfw_InitForOpenGL(m_window, true);
        ImGui_ImplOpenGL3_Init(glsl_version);
        
        return true;
    }

    /**
     * @brief Run the interactive application event loop.
     */
void BolusApp::run() {
        while (!glfwWindowShouldClose(m_window)) {
            glfwPollEvents();
            
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            
            draw_gui();
            
            ImGui::Render();
            int display_w, display_h;
            glfwGetFramebufferSize(m_window, &display_w, &display_h);
            {
                static int win_print = 0;
                if (win_print++ < 3) {
                    int win_x, win_y, win_w, win_h;
                    glfwGetWindowPos(m_window, &win_x, &win_y);
                    glfwGetWindowSize(m_window, &win_w, &win_h);
                    fprintf(stderr, "GLFW POS: (%d, %d)  SIZE: (%d, %d)  FB: (%d, %d)\n",
                            win_x, win_y, win_w, win_h, display_w, display_h);
                }
            }
            glViewport(0, 0, display_w, display_h);
            glClearColor(0.12f, 0.12f, 0.13f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            
            glfwSwapBuffers(m_window);
        }
        save_gui_state();
    }

bool BolusApp::load_dataset(const std::string& csv_path) {
        m_csv_path = csv_path;
        m_records = read_results_csv(csv_path);
        if (m_records.empty()) {
            std::cerr << "Error: Loaded empty CSV or failed to open: " << csv_path << std::endl;
            return false;
        }

        std::filesystem::path cp(csv_path);
        std::filesystem::path parent = cp.parent_path();
        std::string stem = cp.stem().string();
        
        // Strip _results or _results_cpp from stem to find original name
        size_t res_pos = stem.find("_results");
        if (res_pos != std::string::npos) {
            stem = stem.substr(0, res_pos);
        }
        
        m_tiff_path = (parent / (stem + ".tif")).string();
        if (!std::filesystem::exists(m_tiff_path)) {
            m_tiff_path = (parent / (stem + ".tiff")).string();
        }
        
        m_rois_path = find_rois_txt_file(m_tiff_path);
        if (m_rois_path.empty() || !std::filesystem::exists(m_rois_path)) {
            m_rois_path = find_rois_mat_file(m_tiff_path);
        }
        m_meta_path = find_meta_txt_file(m_tiff_path);
        
        if (!std::filesystem::exists(m_tiff_path)) {
            std::cerr << "TIFF stack not found: " << m_tiff_path << std::endl;
            return false;
        }
        if (m_rois_path.empty() || !std::filesystem::exists(m_rois_path)) {
            std::cerr << "ROI points txt or mat file not found under: " << parent << std::endl;
            return false;
        }
        
        m_fr = parse_frame_rate(m_meta_path);
        m_tiff = load_tiff(m_tiff_path);
        if (m_rois_path.size() >= 4 && m_rois_path.compare(m_rois_path.size() - 4, 4, ".mat") == 0) {
            m_rois = MatParser::load_rois_from_mat(m_rois_path);
        } else {
            m_rois = load_rois_txt(m_rois_path);
        }
        
        if (m_tiff.frames.empty() || m_rois.empty()) {
            std::cerr << "Error: TIFF frame list or ROI list is empty." << std::endl;
            return false;
        }
        
        precompute_all_traces();
        
        if (m_records.size() != m_cache.size()) {
            std::cerr << "Warning: Mismatch between CSV record count (" << m_records.size()
                      << ") and ROI cache count (" << m_cache.size() << "). Aligning to minimum." << std::endl;
            size_t min_sz = std::min(m_records.size(), m_cache.size());
            m_records.resize(min_sz);
            m_cache.resize(min_sz);
        }
        
        // Reconstruct missing interactive markers if NaN
        for (size_t i = 0; i < m_records.size(); ++i) {
            auto& rec = m_records[i];
            const auto& c = m_cache[i];
            if (std::isnan(rec.click_onset) || std::isnan(rec.click_peak) || std::isnan(rec.click_end) || std::isnan(rec.click_start)) {
                if (!c.y_us.empty()) {
                    AutoEstimateResults auto_res = m_fitter.auto_estimate_params(c.y_us, c.t_us, m_fr, m_upsample_factor);
                    if (std::isnan(rec.click_start)) rec.click_start = auto_res.click_start;
                    if (std::isnan(rec.click_onset)) rec.click_onset = auto_res.click_onset;
                    if (std::isnan(rec.click_peak)) rec.click_peak = auto_res.click_peak;
                    if (std::isnan(rec.click_end)) rec.click_end = auto_res.click_end;
                }
            }
            // Recompute fit curve now that we have the reconstructed click_onset
            precompute_fit_plot(i);
        }
        
        // Initialize default GUI ROI states
        m_gui_roi_states.resize(m_records.size());
        for (size_t i = 0; i < m_records.size(); ++i) {
            const auto& rec = m_records[i];
            const auto& c = m_cache[i];
            auto& s = m_gui_roi_states[i];
            s.roi_id = rec.roi_id;
            s.crop_min = (!std::isnan(rec.click_start) && rec.click_start >= 0.0) ? rec.click_start : 0.0;
            s.crop_max = c.t_raw.empty() ? 120.0 : c.t_raw.back();
            s.onset = !std::isnan(rec.click_onset) ? rec.click_onset : s.crop_max * 0.35;
            s.peak = !std::isnan(rec.click_peak) ? rec.click_peak : (!std::isnan(rec.f_t2p) && !std::isnan(rec.click_onset) ? rec.click_onset + rec.f_t2p : s.onset + 4.0);
            s.end = !std::isnan(rec.click_end) ? rec.click_end : s.peak + 6.0;
            s.baseline = !std::isnan(rec.f_m) ? rec.f_m : (!std::isnan(rec.init_m) ? rec.init_m : c.y_denoised.front());
            s.qc_flag = rec.qc_flag;
            s.fit_source = rec.fit_source;
        }

        // Backup the pristine loaded CSV and initial GUI state (before loading user modifications)
        std::vector<CsvRecord> original_loaded_records = m_records;
        std::vector<RoiState> original_loaded_states = m_gui_roi_states;

        // Temporarily clear selection index while pre-calculating pristine auto-fits
        int backup_selected_roi_idx = m_selected_roi_idx;
        m_selected_roi_idx = -1;
        for (size_t i = 0; i < m_records.size(); ++i) {
            run_fit_on_record(i, true);
        }
        m_records_backup = m_records;
        m_gui_roi_states_backup = m_gui_roi_states;

        // Restore originally loaded CSV values
        m_records = original_loaded_records;
        m_gui_roi_states = original_loaded_states;
        m_selected_roi_idx = backup_selected_roi_idx;

        // Try loading gui state
        load_gui_state();
        
        build_triage_queue();
        
        // Select either the loaded last active index, or default to triage queue start
        if (m_selected_roi_idx >= 0 && m_selected_roi_idx < static_cast<int>(m_records.size())) {
            int target_idx = m_selected_roi_idx;
            m_selected_roi_idx = -1; // Bypass saving current modified state
            select_record(target_idx);
        } else if (!m_triage_queue.empty()) {
            m_selected_roi_idx = -1; // Bypass saving current modified state
            select_record(m_triage_queue[0]);
        } else {
            m_selected_roi_idx = -1; // Bypass saving current modified state
            select_record(0);
        }
        
        return true;
    }

void BolusApp::save_gui_state() {
    if (m_csv_path.empty()) return;
        
        // Save current active ROI state before writing
        if (m_selected_roi_idx >= 0 && m_selected_roi_idx < static_cast<int>(m_records.size())) {
            auto& s = m_gui_roi_states[m_selected_roi_idx];
            s.crop_min = m_crop_min;
            s.crop_max = m_crop_max;
            s.onset = m_onset_marker;
            s.peak = m_peak_marker;
            s.end = m_end_marker;
            s.baseline = m_baseline_marker;
            s.qc_flag = m_records[m_selected_roi_idx].qc_flag;
            s.fit_source = m_records[m_selected_roi_idx].fit_source;
        }
        
        std::string state_path = m_csv_path + ".gui_state";
        std::ofstream out(state_path);
        if (!out.is_open()) return;
        
        out << "# Bolus Tracking Studio GUI State File\n";
        out << "LastSelectedRoiIndex=" << m_selected_roi_idx << "\n";
        out << "FilterFlaggedOnly=" << (m_qc_filter_type == 1 ? 1 : 0) << "\n";
        out << "QcFilterType=" << m_qc_filter_type << "\n";
        out << "DenoiseStrengthFactor=" << m_denoise_strength_factor << "\n";
        out << "# ROI,crop_min,crop_max,onset,peak,end,baseline,qc_flag,fit_source\n";
        for (const auto& s : m_gui_roi_states) {
            out << s.roi_id << ","
                << s.crop_min << ","
                << s.crop_max << ","
                << s.onset << ","
                << s.peak << ","
                << s.end << ","
                << s.baseline << ","
                << s.qc_flag << ","
                << s.fit_source << "\n";
        }
        std::cout << "Saved GUI workflow progress to: " << state_path << std::endl;
    }

void BolusApp::load_gui_state() {
    if (m_csv_path.empty()) return;
    std::string state_path = m_csv_path + ".gui_state";
    std::ifstream in(state_path);
    if (!in.is_open()) return;
    
    int loaded_selected_roi_idx = -1;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        size_t eq_pos = line.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = line.substr(0, eq_pos);
            std::string val = line.substr(eq_pos + 1);
            if (key == "LastSelectedRoiIndex") {
                try { loaded_selected_roi_idx = std::stoi(val); } catch (...) {}
            } else if (key == "FilterFlaggedOnly") {
                try {
                    int val_int = std::stoi(val);
                    if (val_int != 0) {
                        m_qc_filter_type = 1;
                    } else {
                        m_qc_filter_type = 0;
                    }
                } catch (...) {}
            } else if (key == "QcFilterType") {
                try { m_qc_filter_type = std::stoi(val); } catch (...) {}
            } else if (key == "DenoiseStrengthFactor") {
                try { m_denoise_strength_factor = std::stof(val); } catch (...) {}
            }
            continue;
        }
        
        // Parse CSV-style ROI state line
        std::stringstream ss(line);
        std::vector<std::string> cells;
        std::string cell;
        while (std::getline(ss, cell, ',')) {
            cells.push_back(cell);
        }
        if (cells.size() < 9) continue;
        
        try {
            int roi_id = std::stoi(cells[0]);
            for (size_t i = 0; i < m_gui_roi_states.size(); ++i) {
                if (m_gui_roi_states[i].roi_id == roi_id) {
                    auto& s = m_gui_roi_states[i];
                    s.crop_min = std::stod(cells[1]);
                    s.crop_max = std::stod(cells[2]);
                    s.onset = std::stod(cells[3]);
                    s.peak = std::stod(cells[4]);
                    s.end = std::stod(cells[5]);
                    s.baseline = std::stod(cells[6]);
                    s.qc_flag = cells[7];
                    s.fit_source = cells[8];
                    
                    // Sync back to CsvRecord
                    auto& rec = m_records[i];
                    rec.click_start = s.crop_min;
                    rec.click_onset = s.onset;
                    rec.click_peak = s.peak;
                    rec.click_end = s.end;
                    rec.qc_flag = s.qc_flag;
                    rec.fit_source = s.fit_source;
                    break;
                }
            }
        } catch (...) {}
    }
    std::cout << "Loaded GUI workflow progress from: " << state_path << std::endl;
    // Re-apply loaded denoising strength factor to all traces and fit curves
    precompute_all_traces();

    // Temporarily clear selection index so run_fit_on_record operates purely on loaded state arrays cleanly
    m_selected_roi_idx = -1;
    for (size_t i = 0; i < m_gui_roi_states.size(); ++i) {
        if (m_gui_roi_states[i].fit_source == "manual") {
            run_fit_on_record(i, false);
        } else if (m_gui_roi_states[i].fit_source == "override") {
            m_records[i].qc_flag = "PASS";
            m_records[i].fit_source = "override";
            m_gui_roi_states[i].qc_flag = "PASS";
            precompute_fit_plot(i);
        } else {
            precompute_fit_plot(i);
        }
    }
    m_selected_roi_idx = loaded_selected_roi_idx;
}

    /**
     * @brief Precompute raw signals, drift, denoised, and upsampled spline arrays for all ROIs.
     */
void BolusApp::precompute_all_traces() {
        m_cache.resize(m_rois.size());
        for (size_t r = 0; r < m_rois.size(); ++r) {
            precompute_single_trace(r);
        }
    }

void BolusApp::precompute_single_trace(size_t r) {
        if (r >= m_rois.size()) return;
        if (m_cache.size() <= r) m_cache.resize(m_rois.size());
        
        const auto& roi = m_rois[r];
        auto& c = m_cache[r];
        c.roi_id = roi.id;
        
        // 1. Rasterize
        std::vector<int> mask = ROIMaskRasterizer::get_mask_pixels(roi.poly, m_tiff.width, m_tiff.height);
        int mask_size = 0;
        for (int v : mask) mask_size += v;
        
        // 2. Average raw MFI
        c.y_raw.resize(m_tiff.frames.size(), 0.0);
        c.t_raw.resize(m_tiff.frames.size(), 0.0);
        for (size_t f = 0; f < m_tiff.frames.size(); ++f) {
            c.t_raw[f] = f / m_fr;
            if (mask_size > 0) {
                double sum = 0.0;
                for (int idx = 0; idx < m_tiff.width * m_tiff.height; ++idx) {
                    if (mask[idx]) sum += m_tiff.frames[f][idx];
                }
                c.y_raw[f] = sum / mask_size;
            }
        }
        
        // 3. Drift estimation
        double sum_t = 0.0, sum_y = 0.0, sum_tt = 0.0, sum_ty = 0.0;
        int count = 0;
        for (size_t i = 0; i < c.t_raw.size(); ++i) {
            if (c.t_raw[i] <= m_drift_win) {
                sum_t += c.t_raw[i];
                sum_y += c.y_raw[i];
                sum_tt += c.t_raw[i] * c.t_raw[i];
                sum_ty += c.t_raw[i] * c.y_raw[i];
                count++;
            }
        }
        c.drift_slope = 0.0;
        if (count > 1) {
            double mean_t = sum_t / count;
            double mean_y = sum_y / count;
            double num = sum_ty - count * mean_t * mean_y;
            double den = sum_tt - count * mean_t * mean_t;
            if (std::abs(den) > 1e-9) c.drift_slope = num / den;
        }
        
        std::vector<double> detrended = c.y_raw;
        for (size_t i = 0; i < detrended.size(); ++i) {
            detrended[i] -= c.drift_slope * c.t_raw[i];
        }
        c.y_raw_detrended = detrended;
        
        // Calculate raw trace CNR
        int n_base = std::min((int)std::round(2.0 * m_fr), (int)std::round(detrended.size() * 0.1));
        n_base = std::max(2, n_base);
        std::vector<double> raw_base_win(detrended.begin(), detrended.begin() + n_base);
        double raw_baseline = SignalProcessor::compute_median(raw_base_win);
        double sum_raw_base = 0.0;
        for (double val : raw_base_win) sum_raw_base += val;
        double mean_raw_base = sum_raw_base / raw_base_win.size();
        double raw_sd_base = SignalProcessor::compute_std(raw_base_win, mean_raw_base);
        
        double raw_max_val = -1e9;
        for (double val : detrended) {
            if (val > raw_max_val) raw_max_val = val;
        }
        double raw_amp = raw_max_val - raw_baseline;
        double raw_cnr = (raw_sd_base > 0.0) ? (raw_amp / raw_sd_base) : 0.0;
        
        double denoise_thresh = 2.0;
        int denoise_half_win = 5;
        if (raw_cnr < 4.0) {
            denoise_thresh = 1.5;
            denoise_half_win = 7;
        } else if (raw_cnr >= 15.0) {
            denoise_thresh = 3.0;
            denoise_half_win = 3;
        } else if (raw_cnr >= 8.0) {
            denoise_thresh = 2.5;
            denoise_half_win = 5;
        }
        
        // Adjust threshold and half-window size based on GUI denoising strength multiplier
        if (m_denoise_strength_factor > 0.01f) {
            denoise_thresh = denoise_thresh / static_cast<double>(m_denoise_strength_factor);
            denoise_half_win = std::max(1, static_cast<int>(std::round(denoise_half_win * m_denoise_strength_factor)));
        }
        
        // 4. Denoise and Spline
        c.y_denoised = SignalProcessor::denoise_trace(detrended, denoise_thresh, denoise_half_win);
        c.t_us.resize(c.t_raw.size() * m_upsample_factor);
        for (size_t i = 0; i < c.t_us.size(); ++i) {
            c.t_us[i] = i / (m_fr * m_upsample_factor);
        }
        
        SplineInterpolator spline;
        spline.build(c.t_raw, c.y_denoised);
        c.y_us.resize(c.t_us.size());
        for (size_t i = 0; i < c.t_us.size(); ++i) {
            c.y_us[i] = spline.eval(c.t_us[i]);
        }
        
        // 5. Baseline SD
        int n_base_us = std::min((int)std::round(2.0 * m_fr * m_upsample_factor), (int)std::round(c.y_us.size() * 0.1));
        n_base_us = std::max(1, n_base_us);
        std::vector<double> base_win(c.y_us.begin(), c.y_us.begin() + n_base_us);
        double mean_base = 0.0;
        for (double x : base_win) mean_base += x;
        mean_base /= base_win.size();
        c.sd_base = SignalProcessor::compute_std(base_win, mean_base);
        if (c.sd_base <= 0.0) c.sd_base = 0.05;
        
        // 6. Precompute Fit Plot Curve (from CSV record parameters)
        precompute_fit_plot(r);
    }

    /**
     * @brief Precompute plot coordinates for the active fit parameters of a specific cache index.
     */
void BolusApp::precompute_fit_plot(size_t cache_idx) {
        auto& c = m_cache[cache_idx];
        const auto& rec = m_records[cache_idx];
        c.t_fit_plot.clear();
        c.y_fit_plot.clear();
        c.t_fit_auto_plot.clear();
        c.y_fit_auto_plot.clear();
        
        if (std::isnan(rec.f_amp) || std::isnan(rec.f_t2p) || std::isnan(rec.f_fwhm) || std::isnan(rec.f_m) || std::isnan(rec.ont)) {
            return;
        }
        
        double alpha = ((rec.f_t2p * rec.f_t2p) / (rec.f_fwhm * rec.f_fwhm)) * 8.0 * std::log(2.0);
        double beta = ((rec.f_fwhm * rec.f_fwhm) / rec.f_t2p) / (8.0 * std::log(2.0));
        
        c.t_fit_plot = c.t_us;
        c.y_fit_plot.resize(c.t_fit_plot.size());
        
        double onset_t = !std::isnan(rec.click_onset) ? rec.click_onset : 0.0;
        double end_t = !std::isnan(rec.click_end) ? rec.click_end : c.t_us.back();
        for (size_t i = 0; i < c.t_fit_plot.size(); ++i) {
            double t = c.t_fit_plot[i];
            if (t > end_t) {
                c.y_fit_plot[i] = std::numeric_limits<double>::quiet_NaN();
                continue;
            }
            double val = rec.f_m;
            if (t >= onset_t) {
                double dt = t - onset_t;
                val = rec.f_m + rec.f_amp * std::pow(dt / rec.f_t2p, alpha) * std::exp(-(dt - rec.f_t2p) / beta);
            }
            c.y_fit_plot[i] = val;
        }

        // Compute original auto fit curve
        const CsvRecord* rec_auto_ptr = nullptr;
        if (cache_idx < m_records_backup.size()) {
            rec_auto_ptr = &m_records_backup[cache_idx];
        } else {
            rec_auto_ptr = &rec;
        }
        
        if (rec_auto_ptr) {
            const auto& rec_auto = *rec_auto_ptr;
            if (!std::isnan(rec_auto.f_amp) && !std::isnan(rec_auto.f_t2p) && !std::isnan(rec_auto.f_fwhm) && !std::isnan(rec_auto.f_m) && !std::isnan(rec_auto.ont)) {
                double alpha_auto = ((rec_auto.f_t2p * rec_auto.f_t2p) / (rec_auto.f_fwhm * rec_auto.f_fwhm)) * 8.0 * std::log(2.0);
                double beta_auto = ((rec_auto.f_fwhm * rec_auto.f_fwhm) / rec_auto.f_t2p) / (8.0 * std::log(2.0));
                
                c.t_fit_auto_plot = c.t_us;
                c.y_fit_auto_plot.resize(c.t_fit_auto_plot.size());
                
                double onset_t_auto = !std::isnan(rec_auto.click_onset) ? rec_auto.click_onset : 0.0;
                double end_t_auto = !std::isnan(rec_auto.click_end) ? rec_auto.click_end : c.t_us.back();
                
                for (size_t i = 0; i < c.t_fit_auto_plot.size(); ++i) {
                    double t = c.t_fit_auto_plot[i];
                    if (t > end_t_auto) {
                        c.y_fit_auto_plot[i] = std::numeric_limits<double>::quiet_NaN();
                        continue;
                    }
                    double val = rec_auto.f_m;
                    if (t >= onset_t_auto) {
                        double dt = t - onset_t_auto;
                        val = rec_auto.f_m + rec_auto.f_amp * std::pow(dt / rec_auto.f_t2p, alpha_auto) * std::exp(-(dt - rec_auto.f_t2p) / beta_auto);
                    }
                    c.y_fit_auto_plot[i] = val;
                }
            }
        }
    }

    /**
     * @brief Update the list indices that need manual review or have failed.
     */
void BolusApp::build_triage_queue() {
        m_triage_queue.clear();
        for (size_t i = 0; i < m_records.size(); ++i) {
            bool matches = false;
            if (m_qc_filter_type == 0) { // All
                matches = true;
            } else if (m_qc_filter_type == 1) { // Flagged (FAIL/WARN/REVIEW)
                matches = (m_records[i].qc_flag == "FAIL" || m_records[i].qc_flag == "WARN" || m_records[i].qc_flag == "REVIEW");
            } else if (m_qc_filter_type == 2) { // FAIL Only
                matches = (m_records[i].qc_flag == "FAIL");
            } else if (m_qc_filter_type == 3) { // WARN Only
                matches = (m_records[i].qc_flag == "WARN");
            } else if (m_qc_filter_type == 4) { // PASS Only
                matches = (m_records[i].qc_flag == "PASS");
            } else if (m_qc_filter_type == 5) { // REVIEW Only
                matches = (m_records[i].qc_flag == "REVIEW");
            }
            if (matches) {
                m_triage_queue.push_back(i);
            }
        }
        
        // Find current position in queue
        m_queue_pos = -1;
        if (m_selected_roi_idx >= 0) {
            for (size_t q = 0; q < m_triage_queue.size(); ++q) {
                if (m_triage_queue[q] == m_selected_roi_idx) {
                    m_queue_pos = q;
                    break;
                }
            }
        }
    }

    /**
     * @brief Select record and populate interactive draggable markers.
     */
void BolusApp::select_record(int idx) {
        if (idx < 0 || idx >= static_cast<int>(m_records.size())) return;
        
        // 1. Save currently active state to m_gui_roi_states and CsvRecord click times
        if (m_selected_roi_idx >= 0 && m_selected_roi_idx < static_cast<int>(m_records.size())) {
            auto& s = m_gui_roi_states[m_selected_roi_idx];
            s.crop_min = m_crop_min;
            s.crop_max = m_crop_max;
            s.onset = m_onset_marker;
            s.peak = m_peak_marker;
            s.end = m_end_marker;
            s.baseline = m_baseline_marker;
            s.qc_flag = m_records[m_selected_roi_idx].qc_flag;
            s.fit_source = m_records[m_selected_roi_idx].fit_source;
            
            auto& old_rec = m_records[m_selected_roi_idx];
            old_rec.click_start = m_crop_min;
            old_rec.click_onset = m_onset_marker;
            old_rec.click_peak = m_peak_marker;
            old_rec.click_end = m_end_marker;
        }

        m_selected_roi_idx = idx;
        
        const auto& rec = m_records[idx];
        const auto& c = m_cache[idx];
        const auto& s = m_gui_roi_states[idx];
        
        // 2. Load markers from m_gui_roi_states
        m_crop_min = s.crop_min;
        m_crop_max = s.crop_max;
        m_onset_marker = s.onset;
        m_peak_marker = s.peak;
        m_end_marker = s.end;
        m_baseline_marker = s.baseline;
        
        // Limit markers inside trace bounds
        double max_t = c.t_raw.back();
        m_onset_marker = std::clamp(m_onset_marker, 0.0, max_t);
        m_peak_marker = std::clamp(m_peak_marker, m_onset_marker + 0.01, max_t);
        m_end_marker = std::clamp(m_end_marker, m_peak_marker + 0.01, max_t);
        
        // Find queue position
        m_queue_pos = -1;
        for (size_t q = 0; q < m_triage_queue.size(); ++q) {
            if (m_triage_queue[q] == idx) {
                m_queue_pos = q;
                break;
            }
        }
        update_mip_texture();
    }

    /**
     * @brief Run constrained non-linear fit using draggable visual marker bounds.
     */
void BolusApp::run_fit_on_current_roi() {
        if (m_selected_roi_idx < 0) return;
        run_fit_on_record(m_selected_roi_idx, false);
}

void BolusApp::run_fit_on_record(int idx, bool is_auto) {
        if (idx < 0 || idx >= static_cast<int>(m_records.size())) return;
        
        auto& rec = m_records[idx];
        auto& c = m_cache[idx];
        auto& s = m_gui_roi_states[idx];
        
        double crop_min = 0.0;
        double crop_max = c.t_raw.empty() ? 120.0 : c.t_raw.back();
        double onset = 0.0;
        double peak = 0.0;
        double end = 0.0;
        double baseline = 0.0;
        
        if (is_auto) {
            if (!c.y_us.empty()) {
                AutoEstimateResults auto_res = m_fitter.auto_estimate_params(c.y_us, c.t_us, m_fr, m_upsample_factor);
                crop_min = auto_res.click_start;
                onset = auto_res.click_onset;
                peak = auto_res.click_peak;
                end = auto_res.click_end;
                baseline = auto_res.init_params[3];
            } else {
                crop_min = 0.0;
                onset = crop_max * 0.35;
                peak = onset + 4.0;
                end = peak + 6.0;
                baseline = 0.0;
            }
        } else {
            if (idx == m_selected_roi_idx) {
                crop_min = m_crop_min;
                crop_max = m_crop_max;
                onset = m_onset_marker;
                peak = m_peak_marker;
                end = m_end_marker;
                baseline = m_baseline_marker;
            } else {
                crop_min = s.crop_min;
                crop_max = s.crop_max;
                onset = s.onset;
                peak = s.peak;
                end = s.end;
                baseline = s.baseline;
            }
        }
        
        // Map visual marker times to the upsampled trace vector
        auto find_nearest_idx = [](const std::vector<double>& vec, double val) -> int {
            auto it = std::lower_bound(vec.begin(), vec.end(), val);
            if (it == vec.end()) return vec.size() - 1;
            if (it == vec.begin()) return 0;
            double d1 = *it - val;
            double d2 = val - *(it - 1);
            return (d1 < d2) ? std::distance(vec.begin(), it) : std::distance(vec.begin(), it - 1);
        };
        
        int start_idx = find_nearest_idx(c.t_us, onset);
        int end_idx = find_nearest_idx(c.t_us, end);
        int peak_idx = find_nearest_idx(c.t_us, peak);
        
        if (end_idx <= start_idx + 5) {
            std::cerr << "Fit Window is too short!" << std::endl;
            return;
        }
        
        // Prepare sub-vectors relative to the onset
        std::vector<double> t_fit(end_idx - start_idx);
        std::vector<double> y_fit(end_idx - start_idx);
        for (int i = start_idx; i < end_idx; ++i) {
            t_fit[i - start_idx] = c.t_us[i] - c.t_us[start_idx];
            y_fit[i - start_idx] = c.y_us[i];
        }
        
        // Formulate guesses
        double guess_amp = c.y_us[peak_idx] - baseline;
        if (guess_amp < 1e-4) guess_amp = 10.0;
        
        double guess_t2p = c.t_us[peak_idx] - c.t_us[start_idx];
        if (guess_t2p < 0.1) guess_t2p = 3.0;
        
        double guess_fwhm = (c.t_us[end_idx] - c.t_us[start_idx]) / 2.0;
        if (guess_fwhm < 0.1) guess_fwhm = 5.0;
        
        std::vector<double> init_params = {guess_amp, guess_t2p, guess_fwhm, baseline};
        
        // Run fit solver
        bool fit_success = false;
        bool pass2_run = false;
        std::vector<double> popt = m_fitter.run_nonlinear_fit(t_fit, y_fit, init_params, c.sd_base, fit_success, pass2_run);
        
        // Update CsvRecord
        rec.click_start = crop_min;
        rec.click_onset = onset;
        rec.click_peak = peak;
        rec.click_end = end;
        
        rec.init_amp = guess_amp;
        rec.init_t2p = guess_t2p;
        rec.init_fwhm = guess_fwhm;
        rec.init_m = baseline;
        rec.init_cnr = guess_amp / c.sd_base;
        rec.init_snr = baseline / c.sd_base;
        
        rec.fit_source = is_auto ? "auto" : "manual";
        
        if (fit_success) {
            // Parity validation check
            if (popt[0] <= 1.0001e-6 || popt[0] >= 1023.0 * 0.9999 ||
                popt[1] <= 1.0001e-6 || popt[2] <= 0.5001) {
                fit_success = false;
                rec.qc_flag = "FAIL";
            } else {
                rec.f_amp = popt[0];
                rec.f_t2p = popt[1];
                rec.f_fwhm = popt[2];
                rec.f_m = popt[3];
                rec.f_cnr = popt[0] / c.sd_base;
                rec.f_snr = popt[3] / c.sd_base;
                
                // Recompute hemodynamic values
                std::vector<double> y_fit_model(t_fit.size());
                double alpha = ((popt[1] * popt[1]) / (popt[2] * popt[2])) * 8.0 * std::log(2.0);
                double beta = ((popt[2] * popt[2]) / popt[1]) / (8.0 * std::log(2.0));
                for (size_t i = 0; i < t_fit.size(); ++i) {
                    double t_val = t_fit[i];
                    double val = popt[3];
                    if (t_val > 0) {
                        val = popt[3] + popt[0] * std::pow(t_val / popt[1], alpha) * std::exp(-(t_val - popt[1]) / beta);
                    }
                    y_fit_model[i] = val;
                }
                
                double sum_y = 0.0;
                for (double val : y_fit_model) sum_y += val;
                rec.auc = sum_y - (y_fit_model.front() + y_fit_model.back()) / 2.0;
                
                double min_y = y_fit_model[0];
                double max_y = y_fit_model[0];
                for (double val : y_fit_model) {
                    if (val < min_y) min_y = val;
                    if (val > max_y) max_y = val;
                }
                double range = max_y - min_y;
                double sum_yn = 0.0;
                for (double val : y_fit_model) {
                    sum_yn += (range > 0.0) ? (val - min_y) / range : 0.0;
                }
                double first_yn = (range > 0.0) ? (y_fit_model.front() - min_y) / range : 0.0;
                double last_yn = (range > 0.0) ? (y_fit_model.back() - min_y) / range : 0.0;
                rec.aucn = sum_yn - (first_yn + last_yn) / 2.0;
                
                std::vector<int> I;
                for (size_t i = 0; i < y_fit_model.size(); ++i) {
                    double val_n = (range > 0.0) ? (y_fit_model[i] - min_y) / range : 0.0;
                    if (val_n < 0.1) I.push_back(i);
                }
                int onset_idx = 0;
                if (!I.empty()) {
                    int last_idx = -1;
                    for (size_t k = 0; k + 1 < I.size(); ++k) {
                        if (I[k+1] - I[k] == 1) last_idx = k;
                    }
                    if (last_idx != -1) onset_idx = I[last_idx] + 1;
                    else onset_idx = I[0];
                }
                rec.ont = (double)onset_idx / (m_fr * m_upsample_factor);
                rec.ttm = std::abs(popt[1] - rec.ont);
                
                double shift = 0.0;
                if (idx >= 0 && idx < static_cast<int>(m_records_backup.size())) {
                    shift = m_records_backup[idx].ont - m_records_backup[idx].ont_sc;
                }
                if (std::isnan(shift) || std::isinf(shift)) shift = 0.0;
                rec.ont_sc = rec.ont - shift;
                
                double sum_sq_resid = 0.0;
                for (size_t i = 0; i < y_fit.size(); ++i) {
                    double diff = y_fit[i] - y_fit_model[i];
                    sum_sq_resid += diff * diff;
                }
                double mse = (y_fit.size() > 4) ? (sum_sq_resid / (y_fit.size() - 4)) : 0.0;
                std::vector<double> se = m_fitter.get_parameter_se(t_fit, popt, mse);
                rec.ttlb = std::abs((popt[1] - 1.96 * se[1]) - rec.ont);
                rec.tthb = std::abs((popt[1] + 1.96 * se[1]) - rec.ont);
                
                // Determine QC Flag
                double actual_max_t2p = (m_fitter.max_t2p >= 1e5 && !t_fit.empty()) ? t_fit.back() : m_fitter.max_t2p;
                double actual_max_fwhm = (m_fitter.max_fwhm >= 1e5 && !t_fit.empty()) ? t_fit.back() : m_fitter.max_fwhm;
                
                rec.qc_flag = BolusFitter::determine_qc_flag(
                    popt[0], popt[1], popt[2], popt[3], rec.f_cnr,
                    m_fitter.min_amp, m_fitter.max_amp, m_fitter.min_t2p, actual_max_t2p,
                    m_fitter.min_fwhm, actual_max_fwhm, fit_success, pass2_run,
                    guess_amp
                );
                
                if (std::isnan(rec.auc) || std::isnan(rec.aucn) || std::isnan(rec.ttlb) || 
                    std::isnan(rec.ttm) || std::isnan(rec.tthb) || std::isnan(rec.ont)) {
                    rec.qc_flag = "FAIL";
                }
                
                rec.ves_type = BolusFitter::suggest_vessel_type(rec.ont, rec.f_t2p, rec.f_fwhm, rec.f_amp, rec.qc_flag);
            }
        }
        
        if (!fit_success) {
            rec.f_amp = std::numeric_limits<double>::quiet_NaN();
            rec.f_t2p = std::numeric_limits<double>::quiet_NaN();
            rec.f_fwhm = std::numeric_limits<double>::quiet_NaN();
            rec.f_m = std::numeric_limits<double>::quiet_NaN();
            rec.f_cnr = std::numeric_limits<double>::quiet_NaN();
            rec.f_snr = std::numeric_limits<double>::quiet_NaN();
            rec.auc = std::numeric_limits<double>::quiet_NaN();
            rec.aucn = std::numeric_limits<double>::quiet_NaN();
            rec.ttlb = std::numeric_limits<double>::quiet_NaN();
            rec.ttm = std::numeric_limits<double>::quiet_NaN();
            rec.tthb = std::numeric_limits<double>::quiet_NaN();
            rec.ont = std::numeric_limits<double>::quiet_NaN();
            rec.qc_flag = "FAIL";
        }
        
        // Sync to m_gui_roi_states
        s.crop_min = crop_min;
        s.crop_max = crop_max;
        s.onset = onset;
        s.peak = peak;
        s.end = end;
        s.baseline = baseline;
        s.qc_flag = rec.qc_flag;
        s.fit_source = rec.fit_source;
        
        // If this is the active GUI record, sync GUI variables as well
        if (idx == m_selected_roi_idx) {
            m_crop_min = crop_min;
            m_crop_max = crop_max;
            m_onset_marker = onset;
            m_peak_marker = peak;
            m_end_marker = end;
            m_baseline_marker = baseline;
        }
        
        precompute_fit_plot(idx);
        build_triage_queue();
        save_active_roi_svg();
}

void BolusApp::save_active_roi_svg() {
        if (m_selected_roi_idx < 0 || m_selected_roi_idx >= static_cast<int>(m_records.size())) return;
        const auto& rec = m_records[m_selected_roi_idx];
        const auto& c = m_cache[m_selected_roi_idx];
        
        FitRecord frec;
        frec.roi_id = rec.roi_id;
        frec.subj_num = rec.subj_num;
        frec.exp = rec.exp;
        frec.init_amp = rec.init_amp;
        frec.init_t2p = rec.init_t2p;
        frec.init_fwhm = rec.init_fwhm;
        frec.init_m = rec.init_m;
        frec.init_snr = rec.init_snr;
        frec.init_cnr = rec.init_cnr;
        frec.click_start = rec.click_start;
        frec.click_onset = rec.click_onset;
        frec.click_peak = rec.click_peak;
        frec.click_end = rec.click_end;
        frec.f_amp = rec.f_amp;
        frec.f_t2p = rec.f_t2p;
        frec.f_fwhm = rec.f_fwhm;
        frec.f_m = rec.f_m;
        frec.f_snr = rec.f_snr;
        frec.f_cnr = rec.f_cnr;
        frec.denoise_rms = rec.denoise_rms;
        frec.raw_sd_base = rec.raw_sd_base;
        frec.stall_flag = rec.stall_flag;
        frec.auc = rec.auc;
        frec.aucn = rec.aucn;
        frec.ttlb = rec.ttlb;
        frec.ttm = rec.ttm;
        frec.tthb = rec.tthb;
        frec.ont = rec.ont;
        frec.ont_sc = rec.ont_sc;
        frec.roi_size = rec.roi_size;
        frec.ves_type = rec.ves_type;
        frec.qc_flag = rec.qc_flag;
        frec.fit_source = rec.fit_source;
        
        bool fit_success = !std::isnan(frec.f_amp) && !std::isnan(frec.f_t2p) && !std::isnan(frec.f_fwhm) && !std::isnan(frec.f_m);
        BolusVisualizer::save_svg_plot(frec.roi_id, m_tiff_path, c.t_raw, c.y_raw, c.y_denoised, c.t_us, c.y_us, frec, fit_success, c.drift_slope);
    }

void BolusApp::update_mip_texture() {
    if (m_mip_texture_id != 0) {
        glDeleteTextures(1, &m_mip_texture_id);
        m_mip_texture_id = 0;
    }
    
    if (m_selected_roi_idx < 0 || m_selected_roi_idx >= static_cast<int>(m_rois.size()) || m_tiff.mip.empty()) {
        return;
    }
    
    const auto& roi = m_rois[m_selected_roi_idx];
    if (roi.poly.empty()) return;
    
    double min_x = std::numeric_limits<double>::max();
    double max_x = -std::numeric_limits<double>::max();
    double min_y = std::numeric_limits<double>::max();
    double max_y = -std::numeric_limits<double>::max();
    
    for (const auto& pt : roi.poly) {
        if (pt.first < min_x) min_x = pt.first;
        if (pt.first > max_x) max_x = pt.first;
        if (pt.second < min_y) min_y = pt.second;
        if (pt.second > max_y) max_y = pt.second;
    }
    
    double fov_pad = 80.0;
    int pad_min_x = static_cast<int>(std::floor(min_x - fov_pad));
    int pad_max_x = static_cast<int>(std::ceil(max_x + fov_pad));
    int pad_min_y = static_cast<int>(std::floor(min_y - fov_pad));
    int pad_max_y = static_cast<int>(std::ceil(max_y + fov_pad));
    
    pad_min_x = std::max(0, std::min(pad_min_x, static_cast<int>(m_tiff.width) - 1));
    pad_max_x = std::max(0, std::min(pad_max_x, static_cast<int>(m_tiff.width) - 1));
    pad_min_y = std::max(0, std::min(pad_min_y, static_cast<int>(m_tiff.height) - 1));
    pad_max_y = std::max(0, std::min(pad_max_y, static_cast<int>(m_tiff.height) - 1));
    
    int crop_w = pad_max_x - pad_min_x + 1;
    int crop_h = pad_max_y - pad_min_y + 1;
    
    if (crop_w <= 0 || crop_h <= 0) return;
    
    m_mip_tex_width = crop_w;
    m_mip_tex_height = crop_h;
    
    float crop_min_val = std::numeric_limits<float>::max();
    float crop_max_val = -std::numeric_limits<float>::max();
    
    for (int y = pad_min_y; y <= pad_max_y; ++y) {
        for (int x = pad_min_x; x <= pad_max_x; ++x) {
            float val = m_tiff.mip[y * m_tiff.width + x];
            if (val < crop_min_val) crop_min_val = val;
            if (val > crop_max_val) crop_max_val = val;
        }
    }
    
    std::vector<uint8_t> rgb_buf(crop_w * crop_h * 3);
    for (int y = pad_min_y; y <= pad_max_y; ++y) {
        for (int x = pad_min_x; x <= pad_max_x; ++x) {
            float val = m_tiff.mip[y * m_tiff.width + x];
            uint8_t norm_val = 0;
            if (crop_max_val > crop_min_val) {
                norm_val = static_cast<uint8_t>(std::clamp((val - crop_min_val) / (crop_max_val - crop_min_val) * 255.0f, 0.0f, 255.0f));
            }
            int dest_idx = ((y - pad_min_y) * crop_w + (x - pad_min_x)) * 3;
            rgb_buf[dest_idx]     = norm_val;
            rgb_buf[dest_idx + 1] = norm_val;
            rgb_buf[dest_idx + 2] = norm_val;
        }
    }
    
    glGenTextures(1, &m_mip_texture_id);
    glBindTexture(GL_TEXTURE_2D, m_mip_texture_id);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, crop_w, crop_h, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb_buf.data());
    
    glBindTexture(GL_TEXTURE_2D, 0);
}

void BolusApp::draw_mip_modal() {
    if (!m_show_mip_modal) return;
    
    ImGui::OpenPopup(m_tr.title_roi_mip.c_str());
    
    ImGui::SetNextWindowSize(ImVec2(450.0f, 500.0f), ImGuiCond_FirstUseEver);
    
    if (ImGui::BeginPopupModal(m_tr.title_roi_mip.c_str(), &m_show_mip_modal, ImGuiWindowFlags_NoScrollbar)) {
        if (m_mip_texture_id == 0) {
            ImGui::Text("%s", m_tr.text_no_mip.c_str());
            if (ImGui::Button(m_tr.btn_close_dialog.c_str())) {
                m_show_mip_modal = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
            return;
        }
        
        float aspect = static_cast<float>(m_mip_tex_width) / static_cast<float>(m_mip_tex_height);
        float avail_w = ImGui::GetContentRegionAvail().x;
        float avail_h = ImGui::GetContentRegionAvail().y - 40.0f;
        
        float display_w = avail_w;
        float display_h = avail_w / aspect;
        if (display_h > avail_h) {
            display_h = avail_h;
            display_w = avail_h * aspect;
        }
        
        float start_x = ImGui::GetCursorScreenPos().x + (avail_w - display_w) * 0.5f;
        float start_y = ImGui::GetCursorScreenPos().y + (avail_h - display_h) * 0.5f;
        
        ImGui::SetCursorScreenPos(ImVec2(start_x, start_y));
        
        ImGui::Image((void*)(intptr_t)m_mip_texture_id, ImVec2(display_w, display_h));
        
        if (m_selected_roi_idx >= 0 && m_selected_roi_idx < static_cast<int>(m_rois.size())) {
            const auto& roi = m_rois[m_selected_roi_idx];
            if (!roi.poly.empty()) {
                double min_x = std::numeric_limits<double>::max();
                double max_x = -std::numeric_limits<double>::max();
                double min_y = std::numeric_limits<double>::max();
                double max_y = -std::numeric_limits<double>::max();
                
                for (const auto& pt : roi.poly) {
                    if (pt.first < min_x) min_x = pt.first;
                    if (pt.first > max_x) max_x = pt.first;
                    if (pt.second < min_y) min_y = pt.second;
                    if (pt.second > max_y) max_y = pt.second;
                }
                
                double fov_pad = 80.0;
                int pad_min_x = static_cast<int>(std::floor(min_x - fov_pad));
                int pad_min_y = static_cast<int>(std::floor(min_y - fov_pad));
                
                pad_min_x = std::max(0, std::min(pad_min_x, static_cast<int>(m_tiff.width) - 1));
                pad_min_y = std::max(0, std::min(pad_min_y, static_cast<int>(m_tiff.height) - 1));
                
                float rel_min_x = static_cast<float>(min_x - pad_min_x);
                float rel_max_x = static_cast<float>(max_x - pad_min_x);
                float rel_min_y = static_cast<float>(min_y - pad_min_y);
                float rel_max_y = static_cast<float>(max_y - pad_min_y);
                
                float scale_x = display_w / static_cast<float>(m_mip_tex_width);
                float scale_y = display_h / static_cast<float>(m_mip_tex_height);
                
                float draw_min_x = start_x + rel_min_x * scale_x;
                float draw_max_x = start_x + rel_max_x * scale_x;
                float draw_min_y = start_y + rel_min_y * scale_y;
                float draw_max_y = start_y + rel_max_y * scale_y;
                
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                draw_list->AddRect(ImVec2(draw_min_x, draw_min_y), ImVec2(draw_max_x, draw_max_y), ImColor(255, 0, 0, 255), 0.0f, 0, 2.0f);
            }
        }
        
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + 20.0f, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y - 35.0f));
        if (ImGui::Button(m_tr.btn_close_dialog.c_str(), ImVec2(avail_w, 25.0f))) {
            m_show_mip_modal = false;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }
}

// ============================================================================
// Pipeline Execution
// ============================================================================

void BolusApp::run_pipeline_on_folder(const std::string& folder) {
    // Reset state
    {
        std::lock_guard<std::mutex> lock(m_pipeline_mutex);
        m_pipeline_running = true;
        m_pipeline_done = false;
        m_pipeline_error = false;
        m_pipeline_status = "Starting pipeline...";
        m_pipeline_result_csv.clear();
        m_pipeline_log_lines.clear();
        m_pipeline_folder = folder;
    }

    // Launch in background thread
    std::thread([this, folder]() {
        try {
            // Build fitter with GUI's current settings
            BolusFitter fitter(m_fitter.min_amp, m_fitter.max_amp,
                              m_fitter.min_t2p, m_fitter.max_t2p,
                              m_fitter.min_fwhm, m_fitter.max_fwhm, false);

            // Step 1: Prepare .mat files if enabled
            if (m_pipeline_prepare_mats) {
                {
                    std::lock_guard<std::mutex> lock(m_pipeline_mutex);
                    m_pipeline_status = "Preparing .mat masks...";
                    m_pipeline_log_lines.push_back("[PREPARE] Converting .mat masks to _rois.txt...");
                }
                BatchProcessor prep(folder, m_drift_win, false, fitter, m_qc_settings, m_stall_settings);
                prep.run_prepare(false, false); // apply mode, don't force overwrite
                {
                    std::lock_guard<std::mutex> lock(m_pipeline_mutex);
                    m_pipeline_log_lines.push_back("[PREPARE] Done.");
                }
            }

            // Step 2: Run preflight
            {
                std::lock_guard<std::mutex> lock(m_pipeline_mutex);
                m_pipeline_status = "Running preflight validation...";
                m_pipeline_log_lines.push_back("[PREFLIGHT] Scanning folder...");
            }
            BatchProcessor batch(folder, m_drift_win, m_pipeline_enable_plots, fitter, m_qc_settings, m_stall_settings);
            bool pf_warn = false, pf_err = false;
            batch.run_preflight_scan(pf_warn, pf_err);
            {
                std::lock_guard<std::mutex> lock(m_pipeline_mutex);
                if (pf_err) {
                    m_pipeline_log_lines.push_back("[PREFLIGHT] ERRORS found — some datasets may be skipped.");
                } else if (pf_warn) {
                    m_pipeline_log_lines.push_back("[PREFLIGHT] Warnings found (pipeline will continue).");
                } else {
                    m_pipeline_log_lines.push_back("[PREFLIGHT] All OK.");
                }
            }

            // Step 3: Run full pipeline
            {
                std::lock_guard<std::mutex> lock(m_pipeline_mutex);
                m_pipeline_status = "Processing datasets...";
                m_pipeline_log_lines.push_back("[PIPELINE] Running batch processing...");
            }
            bool success = batch.run();
            {
                std::lock_guard<std::mutex> lock(m_pipeline_mutex);
                if (success) {
                    m_pipeline_log_lines.push_back("[PIPELINE] Batch processing complete.");
                } else {
                    m_pipeline_log_lines.push_back("[PIPELINE] Batch processing finished with errors.");
                }
            }

            // Step 4: Find the first generated CSV to auto-load
            std::string first_csv;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
                if (entry.is_regular_file()) {
                    std::string fname = entry.path().filename().string();
                    if (fname.find("_results_cpp.csv") != std::string::npos) {
                        first_csv = entry.path().string();
                        break;
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(m_pipeline_mutex);
                m_pipeline_result_csv = first_csv;
                m_pipeline_done = true;
                m_pipeline_running = false;
                m_pipeline_status = success ? "Pipeline complete!" : "Pipeline finished with errors.";
                m_pipeline_error = !success;
                if (!first_csv.empty()) {
                    m_pipeline_log_lines.push_back("[DONE] Results: " + first_csv);
                }
            }
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(m_pipeline_mutex);
            m_pipeline_running = false;
            m_pipeline_done = true;
            m_pipeline_error = true;
            m_pipeline_status = std::string("Error: ") + e.what();
            m_pipeline_log_lines.push_back(std::string("[ERROR] ") + e.what());
        }
    }).detach();
}

void BolusApp::draw_pipeline_modal() {
    if (!m_show_pipeline_modal) return;

    ImGui::OpenPopup("Run Full Pipeline##pipeline_modal");

    ImGui::SetNextWindowSize(ImVec2(620, 560), ImGuiCond_FirstUseEver);

    if (ImGui::BeginPopupModal("Run Full Pipeline##pipeline_modal", &m_show_pipeline_modal,
                                ImGuiWindowFlags_NoCollapse)) {

        // ── Folder selection ──
        ImGui::TextColored(ImVec4(0.88f, 0.55f, 0.25f, 1.0f), "Subject Folder");
        ImGui::Separator();

        ImGui::Text("Path:"); ImGui::SameLine();
        char folder_buf[512] = {0};
        strncpy(folder_buf, m_pipeline_folder.c_str(), sizeof(folder_buf) - 1);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80.0f);
        if (ImGui::InputText("##pipeline_folder", folder_buf, sizeof(folder_buf))) {
            m_pipeline_folder = folder_buf;
        }
        ImGui::SameLine();
        if (ImGui::Button("Browse##pf", ImVec2(72, 0))) {
            m_pipeline_browser.open = true;
        }

        // Draw the folder browser if open
        if (m_pipeline_browser.open) {
            m_pipeline_browser.draw("Select Subject Folder##pipeline_browse");
        } else if (m_pipeline_browser_was_open) {
            // Browser just closed — grab whatever was selected
            if (!m_pipeline_browser.selected_file.empty()) {
                // A file was selected — use its parent directory
                std::filesystem::path sel(m_pipeline_browser.selected_file);
                if (std::filesystem::is_directory(sel)) {
                    m_pipeline_folder = sel.string();
                } else {
                    m_pipeline_folder = sel.parent_path().string();
                }
                m_pipeline_browser.selected_file.clear();
            } else {
                // "Select Current Folder" was clicked — use current_path
                m_pipeline_folder = m_pipeline_browser.current_path.string();
            }
        }
        m_pipeline_browser_was_open = m_pipeline_browser.open;

        ImGui::Spacing();

        // ── Options ──
        ImGui::TextColored(ImVec4(0.88f, 0.55f, 0.25f, 1.0f), "Pipeline Options");
        ImGui::Separator();

        ImGui::Checkbox("Auto-convert .mat masks to _rois.txt", &m_pipeline_prepare_mats);
        ImGui::Checkbox("Generate SVG fit plots", &m_pipeline_enable_plots);

        ImGui::Spacing();

        // Drift window
        ImGui::Text("Drift Window (s):"); ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        float dw = (float)m_drift_win;
        if (ImGui::InputFloat("##drift_win", &dw, 0, 0, "%.1f")) {
            m_drift_win = dw;
        }

        ImGui::Spacing();

        // ── Fitting Bounds (collapsible) ──
        if (ImGui::TreeNode("Fitting Bounds")) {
            float col_w = 120.0f;
            ImGui::Text("Amplitude:"); ImGui::SameLine(100);
            ImGui::SetNextItemWidth(col_w);
            float v;
            v = (float)m_fitter.min_amp;
            if (ImGui::InputFloat("Min##amp", &v, 0, 0, "%.2g")) m_fitter.min_amp = v;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(col_w);
            v = (float)m_fitter.max_amp;
            if (ImGui::InputFloat("Max##amp", &v, 0, 0, "%.1f")) m_fitter.max_amp = v;

            ImGui::Text("T2P (s):"); ImGui::SameLine(100);
            ImGui::SetNextItemWidth(col_w);
            v = (float)m_fitter.min_t2p;
            if (ImGui::InputFloat("Min##t2p", &v, 0, 0, "%.2g")) m_fitter.min_t2p = v;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(col_w);
            v = (float)m_fitter.max_t2p;
            if (ImGui::InputFloat("Max##t2p", &v, 0, 0, "%.2g")) m_fitter.max_t2p = v;

            ImGui::Text("FWHM (s):"); ImGui::SameLine(100);
            ImGui::SetNextItemWidth(col_w);
            v = (float)m_fitter.min_fwhm;
            if (ImGui::InputFloat("Min##fwhm", &v, 0, 0, "%.1f")) m_fitter.min_fwhm = v;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(col_w);
            v = (float)m_fitter.max_fwhm;
            if (ImGui::InputFloat("Max##fwhm", &v, 0, 0, "%.2g")) m_fitter.max_fwhm = v;

            ImGui::TreePop();
        }

        // ── QC Settings (collapsible) ──
        if (ImGui::TreeNode("QC Thresholds")) {
            float col_w = 100.0f;
            ImGui::Text("PASS thresholds:");
            ImGui::SetNextItemWidth(col_w);
            float qv;
            qv = (float)m_qc_settings.cnr_min;
            if (ImGui::InputFloat("CNR min##qc", &qv, 0, 0, "%.1f")) m_qc_settings.cnr_min = qv;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(col_w);
            qv = (float)m_qc_settings.fwhm_max;
            if (ImGui::InputFloat("FWHM max##qc", &qv, 0, 0, "%.1f")) m_qc_settings.fwhm_max = qv;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(col_w);
            qv = (float)m_qc_settings.t2p_max;
            if (ImGui::InputFloat("T2P max##qc", &qv, 0, 0, "%.1f")) m_qc_settings.t2p_max = qv;

            ImGui::Spacing();
            ImGui::Text("FAIL thresholds:");
            ImGui::SetNextItemWidth(col_w);
            qv = (float)m_qc_settings.cnr_fail;
            if (ImGui::InputFloat("CNR fail##qc", &qv, 0, 0, "%.1f")) m_qc_settings.cnr_fail = qv;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(col_w);
            qv = (float)m_qc_settings.fwhm_fail;
            if (ImGui::InputFloat("FWHM fail##qc", &qv, 0, 0, "%.1f")) m_qc_settings.fwhm_fail = qv;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(col_w);
            qv = (float)m_qc_settings.t2p_fail;
            if (ImGui::InputFloat("T2P fail##qc", &qv, 0, 0, "%.1f")) m_qc_settings.t2p_fail = qv;

            ImGui::SetNextItemWidth(col_w);
            qv = (float)m_qc_settings.amp_fail;
            if (ImGui::InputFloat("Amp fail##qc", &qv, 0, 0, "%.1f")) m_qc_settings.amp_fail = qv;

            ImGui::TreePop();
        }

        // ── Stall Detection (collapsible) ──
        if (ImGui::TreeNode("Stall Detection")) {
            float col_w = 100.0f;
            ImGui::Text("Onset heuristics:");
            ImGui::SetNextItemWidth(col_w);
            float sv;
            sv = (float)m_stall_settings.ont_offset;
            if (ImGui::InputFloat("Onset offset##st", &sv, 0, 0, "%.1f")) m_stall_settings.ont_offset = sv;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(col_w);
            sv = (float)m_stall_settings.ont_mult;
            if (ImGui::InputFloat("Onset mult##st", &sv, 0, 0, "%.1f")) m_stall_settings.ont_mult = sv;

            ImGui::Text("T2P heuristics:");
            ImGui::SetNextItemWidth(col_w);
            sv = (float)m_stall_settings.t2p_mult;
            if (ImGui::InputFloat("T2P mult##st", &sv, 0, 0, "%.1f")) m_stall_settings.t2p_mult = sv;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(col_w);
            sv = (float)m_stall_settings.t2p_abs;
            if (ImGui::InputFloat("T2P abs##st", &sv, 0, 0, "%.1f")) m_stall_settings.t2p_abs = sv;

            ImGui::Text("Other:");
            ImGui::SetNextItemWidth(col_w);
            sv = (float)m_stall_settings.sd_base;
            if (ImGui::InputFloat("SD base##st", &sv, 0, 0, "%.1f")) m_stall_settings.sd_base = sv;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(col_w);
            sv = (float)m_stall_settings.step_t2p;
            if (ImGui::InputFloat("Step T2P##st", &sv, 0, 0, "%.1f")) m_stall_settings.step_t2p = sv;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(col_w);
            sv = (float)m_stall_settings.step_fwhm;
            if (ImGui::InputFloat("Step FWHM##st", &sv, 0, 0, "%.1f")) m_stall_settings.step_fwhm = sv;

            ImGui::TreePop();
        }

        ImGui::Spacing();
        ImGui::Separator();

        // ── Action buttons / progress ──
        bool is_running = false;
        bool is_done = false;
        std::string status_text;
        {
            std::lock_guard<std::mutex> lock(m_pipeline_mutex);
            is_running = m_pipeline_running;
            is_done = m_pipeline_done;
            status_text = m_pipeline_status;
        }

        if (is_running) {
            // Animated spinner
            float t = (float)glfwGetTime();
            const char* spinner_chars[] = { "|", "/", "-", "\\" };
            int spinner_idx = (int)(t * 8.0f) % 4;
            ImGui::TextColored(ImVec4(0.88f, 0.55f, 0.25f, 1.0f), "%s %s",
                              spinner_chars[spinner_idx], status_text.c_str());
        } else if (is_done) {
            if (m_pipeline_error) {
                ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "%s", status_text.c_str());
            } else {
                ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), "%s", status_text.c_str());
            }
        }

        // Log output area
        {
            std::lock_guard<std::mutex> lock(m_pipeline_mutex);
            if (!m_pipeline_log_lines.empty()) {
                ImGui::BeginChild("##pipeline_log", ImVec2(0, 100), true);
                for (const auto& line : m_pipeline_log_lines) {
                    ImGui::TextWrapped("%s", line.c_str());
                }
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                    ImGui::SetScrollHereY(1.0f);
                }
                ImGui::EndChild();
            }
        }

        ImGui::Spacing();

        // Buttons
        if (!is_running) {
            bool can_run = !m_pipeline_folder.empty() && std::filesystem::exists(m_pipeline_folder);
            if (!can_run) ImGui::BeginDisabled();
            if (ImGui::Button("Run Pipeline", ImVec2(140, 28))) {
                m_pipeline_done = false;
                run_pipeline_on_folder(m_pipeline_folder);
            }
            if (!can_run) ImGui::EndDisabled();

            ImGui::SameLine();

            // Load results button (only after completion)
            if (is_done && !m_pipeline_error && !m_pipeline_result_csv.empty()) {
                if (ImGui::Button("Load Results", ImVec2(140, 28))) {
                    load_dataset(m_pipeline_result_csv);
                    m_show_pipeline_modal = false;
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(80, 28))) {
                m_show_pipeline_modal = false;
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }
}

void BolusApp::draw_intro_screen(float width, float height) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(width, height));
    ImGui::Begin("IntroWindow", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
    
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    
    // 1. Draw Background (Deep Charcoal #131316)
    ImU32 bg_color = IM_COL32(19, 19, 22, 255);
    draw_list->AddRectFilled(ImVec2(0, 0), ImVec2(width, height), bg_color);
    
    float elapsed = (float)(glfwGetTime() - m_intro_start_time);
    if (elapsed > 6.5f) {
        m_showing_intro = false;
        ImGui::End();
        return;
    }
    
    // Warm retro palette colors
    ImU32 col_cream    = IM_COL32(244, 234, 212, 255); // #F4EAD4
    ImU32 col_mustard  = IM_COL32(230, 173, 69, 255);  // #E6AD45
    ImU32 col_terracotta = IM_COL32(217, 93, 57, 255);  // #D95D39
    ImU32 col_red_dark   = IM_COL32(138, 37, 34, 255);  // #8A2522
    ImU32 col_teal       = IM_COL32(58, 96, 115, 255);  // #3A6073
    ImU32 col_teal_light = IM_COL32(82, 132, 155, 255); // #52849B
    
    // Overall fade out in the last 0.5s of the 6.5s intro
    float global_alpha = 1.0f;
    if (elapsed > 6.0f) {
        global_alpha = (6.5f - elapsed) / 0.5f;
    }
    
    auto fade = [global_alpha](ImU32 color) -> ImU32 {
        ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
        c.w *= global_alpha;
        return ImGui::ColorConvertFloat4ToU32(c);
    };
    
    // --- 2. RETRO GRID BACKGROUND ---
    float grid_y = height * 0.7f;
    int num_grid_lines = 16;
    for (int i = 0; i <= num_grid_lines; ++i) {
        float x_ratio = (float)i / num_grid_lines;
        float x_start = width * x_ratio;
        draw_list->AddLine(ImVec2(x_start, grid_y), ImVec2(x_start, height), fade(IM_COL32(40, 40, 48, 80)), 1.5f);
    }
    int num_horiz = 8;
    for (int i = 0; i < num_horiz; ++i) {
        float ratio = (float)i / (num_horiz - 1);
        float y = grid_y + (height - grid_y) * (ratio * ratio);
        draw_list->AddLine(ImVec2(0, y), ImVec2(width, y), fade(IM_COL32(40, 40, 48, 80)), 1.5f);
    }
    
    // --- 3. THE VESSEL AND CELL ANIMATION ---
    ImVec2 p0(0.0f, height * 0.45f);
    ImVec2 cp1(width * 0.3f, height * 0.3f);
    ImVec2 cp2(width * 0.6f, height * 0.6f);
    ImVec2 p3(width, height * 0.45f);
    
    float thickness = 70.0f;
    int steps = 100;
    std::vector<ImVec2> points(steps + 1);
    for (int i = 0; i <= steps; ++i) {
        float t = (float)i / steps;
        float omt = 1.0f - t;
        points[i] = omt*omt*omt*p0 + 3.0f*omt*omt*t*cp1 + 3.0f*omt*t*t*cp2 + t*t*t*p3;
    }
    
    // Draw vessel background ribbon
    for (int i = 0; i < steps; ++i) {
        ImVec2 pA = points[i];
        ImVec2 pB = points[i+1];
        ImVec2 dir = ImVec2(pB.x - pA.x, pB.y - pA.y);
        float len = sqrtf(dir.x*dir.x + dir.y*dir.y);
        ImVec2 normal(-dir.y / len, dir.x / len);
        
        ImVec2 topA = pA + normal * (thickness * 0.5f);
        ImVec2 botA = pA - normal * (thickness * 0.5f);
        ImVec2 topB = pB + normal * (thickness * 0.5f);
        ImVec2 botB = pB - normal * (thickness * 0.5f);
        
        draw_list->AddQuadFilled(topA, topB, botB, botA, fade(IM_COL32(35, 55, 65, 90)));
    }
    
    // Draw outer borders
    for (int i = 0; i < steps; ++i) {
        ImVec2 pA = points[i];
        ImVec2 pB = points[i+1];
        ImVec2 dir = ImVec2(pB.x - pA.x, pB.y - pA.y);
        float len = sqrtf(dir.x*dir.x + dir.y*dir.y);
        ImVec2 normal(-dir.y / len, dir.x / len);
        
        draw_list->AddLine(pA + normal * (thickness * 0.5f), pB + normal * (thickness * 0.5f), fade(col_cream), 3.0f);
        draw_list->AddLine(pA - normal * (thickness * 0.5f), pB - normal * (thickness * 0.5f), fade(col_mustard), 3.0f);
    }
    
    // Draw static red blood cells inside vessel
    struct CellSeed {
        float x_ratio;
        float y_offset;
        float radius;
        ImU32 color;
    };
    static const CellSeed bcells[] = {
        { 0.15f, -10.0f, 12.0f, IM_COL32(138, 37, 34, 120) },
        { 0.28f,  15.0f, 10.0f, IM_COL32(217, 93, 57, 100) },
        { 0.45f,  -8.0f, 14.0f, IM_COL32(138, 37, 34, 80) },
        { 0.62f,  12.0f, 11.0f, IM_COL32(217, 93, 57, 110) },
        { 0.78f,  -12.0f, 13.0f, IM_COL32(138, 37, 34, 90) },
        { 0.90f,   5.0f, 10.0f, IM_COL32(217, 93, 57, 120) }
    };
    for (const auto& bc : bcells) {
        int idx = (int)(bc.x_ratio * steps);
        if (idx >= 0 && idx <= steps) {
            ImVec2 center = points[idx];
            ImVec2 dir = (idx < steps) ? ImVec2(points[idx+1].x - points[idx].x, points[idx+1].y - points[idx].y) : ImVec2(1, 0);
            float len = sqrtf(dir.x*dir.x + dir.y*dir.y);
            ImVec2 normal(-dir.y / len, dir.x / len);
            
            ImVec2 cell_pos = center + normal * bc.y_offset;
            draw_list->AddCircleFilled(cell_pos, bc.radius, fade(bc.color));
            draw_list->AddCircle(cell_pos, bc.radius, fade(col_cream), 0, 1.5f);
        }
    }
    
    // --- 4. THE MAIN ACTIVE RED BLOOD CELL ---
    float cell_t = 0.0f;
    if (elapsed < 2.0f) {
        cell_t = elapsed / 2.0f;
    } else {
        cell_t = 1.0f;
    }
    
    float bezier_param = cell_t * 0.5f;
    float omt_c = 1.0f - bezier_param;
    ImVec2 cell_pos = omt_c*omt_c*omt_c*p0 + 3.0f*omt_c*omt_c*bezier_param*cp1 + 3.0f*omt_c*bezier_param*bezier_param*cp2 + bezier_param*bezier_param*bezier_param*p3;
    
    // Draw tracking ticks behind cell
    int cell_idx_limit = (int)(bezier_param * steps);
    for (int i = 0; i <= cell_idx_limit; i += 4) {
        ImVec2 pA = points[i];
        ImVec2 pNext = (i < steps) ? points[i+1] : points[i];
        ImVec2 dir = ImVec2(pNext.x - pA.x, pNext.y - pA.y);
        float len = sqrtf(dir.x*dir.x + dir.y*dir.y);
        ImVec2 normal(-dir.y / len, dir.x / len);
        draw_list->AddLine(pA + normal * (thickness * 0.3f), pA - normal * (thickness * 0.3f), fade(col_teal_light), 1.5f);
    }
    
    float cell_radius = 24.0f;
    float rumble_x = 0.0f;
    float rumble_y = 0.0f;
    float pulse_scale = 1.0f;
    float crescendo_intensity = 0.0f;
    
    if (elapsed >= 1.5f && elapsed < 6.5f) {
        if (elapsed < 3.5f) {
            crescendo_intensity = (elapsed - 1.5f) / 2.0f;
        } else {
            crescendo_intensity = 1.0f;
        }
        
        pulse_scale = 1.0f + 0.3f * crescendo_intensity * sinf(elapsed * 25.0f);
        rumble_x = 8.0f * crescendo_intensity * sinf(elapsed * 45.0f);
        rumble_y = 8.0f * crescendo_intensity * cosf(elapsed * 37.0f);
        
        int wave_count = 4;
        for (int w = 0; w < wave_count; ++w) {
            float wave_age = elapsed * 1.5f - (float)w * 0.35f;
            if (wave_age > 0.0f) {
                float wave_r = cell_radius * (1.0f + 8.0f * fmodf(wave_age, 1.0f));
                float wave_alpha = 1.0f - fmodf(wave_age, 1.0f);
                ImU32 wave_col = (w % 2 == 0) ? col_terracotta : col_mustard;
                ImVec4 wc = ImGui::ColorConvertU32ToFloat4(wave_col);
                wc.w *= wave_alpha * crescendo_intensity * global_alpha * 0.7f;
                draw_list->AddCircle(cell_pos + ImVec2(rumble_x, rumble_y), wave_r, ImGui::ColorConvertFloat4ToU32(wc), 0, 2.5f);
            }
        }
    }
    
    ImVec2 active_cell_pos = cell_pos + ImVec2(rumble_x, rumble_y);
    float final_radius = cell_radius * pulse_scale;
    
    draw_list->AddCircleFilled(active_cell_pos, final_radius, fade(col_terracotta));
    draw_list->AddCircleFilled(active_cell_pos - ImVec2(final_radius*0.15f, final_radius*0.15f), final_radius * 0.5f, fade(col_red_dark));
    draw_list->AddCircle(active_cell_pos, final_radius, fade(col_cream), 0, 3.0f);
    
    // --- 5. THE THX-STYLE LOGO ---
    float logo_alpha = 0.0f;
    if (elapsed > 1.2f) {
        logo_alpha = (elapsed - 1.2f) / 1.0f;
        if (logo_alpha > 1.0f) logo_alpha = 1.0f;
    }
    
    if (logo_alpha > 0.0f) {
        float logo_y = height * 0.28f;
        ImVec2 text_pos_center(width * 0.5f + rumble_x, logo_y + rumble_y);
        
        ImGui::PushFont(m_font_bold);
        std::string title_str = "BOLUS KINETICS";
        ImVec2 text_size = ImGui::CalcTextSize(title_str.c_str());
        ImVec2 text_pos = text_pos_center - ImVec2(text_size.x * 0.5f, text_size.y * 0.5f);
        
        // Retro double-offset shadow
        ImVec4 sc1 = ImGui::ColorConvertU32ToFloat4(col_terracotta);
        sc1.w *= logo_alpha * global_alpha;
        draw_list->AddText(m_font_bold, 36.0f, text_pos + ImVec2(5.0f, 5.0f), ImGui::ColorConvertFloat4ToU32(sc1), title_str.c_str());
        
        ImVec4 sc2 = ImGui::ColorConvertU32ToFloat4(col_teal);
        sc2.w *= logo_alpha * global_alpha * 0.8f;
        draw_list->AddText(m_font_bold, 36.0f, text_pos + ImVec2(-4.0f, -4.0f), ImGui::ColorConvertFloat4ToU32(sc2), title_str.c_str());
        
        ImVec4 tc = ImGui::ColorConvertU32ToFloat4(col_cream);
        tc.w *= logo_alpha * global_alpha;
        draw_list->AddText(m_font_bold, 36.0f, text_pos, ImGui::ColorConvertFloat4ToU32(tc), title_str.c_str());
        ImGui::PopFont();
        
        // --- 6. "MADE BY MATT" BADGE ---
        float matt_alpha = 0.0f;
        if (elapsed > 1.8f) {
            matt_alpha = (elapsed - 1.8f) / 0.8f;
            if (matt_alpha > 1.0f) matt_alpha = 1.0f;
        }
        
        if (matt_alpha > 0.0f) {
            float sub_y = height * 0.65f;
            std::string sub_str = "MADE BY MATT";
            
            // Visual crescendo: starts at 30% scale and swells dramatically to 400% scale (bold 28px base font size)
            float scale = 0.30f + 3.70f * matt_alpha;
            float font_size = 28.0f * scale;
            
            ImVec2 sub_size = m_font_bold->CalcTextSizeA(font_size, FLT_MAX, 0.0f, sub_str.c_str());
            ImVec2 badge_pos_center(width * 0.5f, sub_y);
            float pad_x = 28.0f * scale;
            float pad_y = 10.0f * scale;
            float corner_radius = 24.0f * scale;
            
            ImVec2 min_pt = badge_pos_center - ImVec2(sub_size.x * 0.5f + pad_x, sub_size.y * 0.5f + pad_y);
            ImVec2 max_pt = badge_pos_center + ImVec2(sub_size.x * 0.5f + pad_x, sub_size.y * 0.5f + pad_y);
            
            ImVec4 bgc = ImGui::ColorConvertU32ToFloat4(col_teal);
            bgc.w *= matt_alpha * global_alpha * 0.9f;
            draw_list->AddRectFilled(min_pt, max_pt, ImGui::ColorConvertFloat4ToU32(bgc), corner_radius);
            
            ImVec4 bdc = ImGui::ColorConvertU32ToFloat4(col_cream);
            bdc.w *= matt_alpha * global_alpha;
            draw_list->AddRect(min_pt, max_pt, ImGui::ColorConvertFloat4ToU32(bdc), corner_radius, 0, 2.0f * scale);
            
            ImVec4 mc = ImGui::ColorConvertU32ToFloat4(col_cream);
            mc.w *= matt_alpha * global_alpha;
            draw_list->AddText(m_font_bold, font_size, badge_pos_center - ImVec2(sub_size.x * 0.5f, sub_size.y * 0.5f), ImGui::ColorConvertFloat4ToU32(mc), sub_str.c_str());
        }
    }
    
    float skip_alpha = 0.5f;
    if (elapsed > 6.0f) {
        skip_alpha *= (6.5f - elapsed) / 0.5f;
    }
    ImVec4 skc = ImGui::ColorConvertU32ToFloat4(col_cream);
    skc.w *= skip_alpha;
    std::string skip_str = "Press SPACE to Skip";
    ImVec2 skip_size = ImGui::CalcTextSize(skip_str.c_str());
    draw_list->AddText(ImVec2(width - skip_size.x - 20.0f, height - skip_size.y - 20.0f), ImGui::ColorConvertFloat4ToU32(skc), skip_str.c_str());
    
    ImGui::End();
}

    /**
     * @brief Render the graphical panels.
     */
void BolusApp::draw_gui() {
        ImGui::PushFont(m_font_regular);
        
        if (m_showing_intro) {
            if (m_intro_start_time < 0.0) {
                m_intro_start_time = glfwGetTime();
                std::string wav_path = get_resource_path("resources/thx_crescendo.wav");
                ensure_thx_sound_exists(wav_path);
                play_sound_cross_platform(wav_path);
            }
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            draw_intro_screen(viewport->Size.x, viewport->Size.y);
            
            // Skip intro with Space, Enter, or Mouse Click
            if (ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsMouseClicked(0)) {
                m_showing_intro = false;
            }
            ImGui::PopFont();
            return;
        }

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("MainPanel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
        
        // Top Toolbar
        draw_top_bar();
        
        // Main Panels (Left: Sidebar, Right: Plot & Parameter Details)
        ImGui::Separator();
        
        float sidebar_w = 380.0f;
        ImGui::BeginChild("SidebarPane", ImVec2(sidebar_w, 0), true);
        draw_sidebar();
        ImGui::EndChild();
        
        ImGui::SameLine();
        
        ImGui::BeginChild("PlotAndControlsPane", ImVec2(0, 0), false);
        draw_main_area();
        ImGui::EndChild();
        
        ImGui::End();
        
        // Draw file browser modal
        m_browser.draw("Open Folder or File");
        draw_mip_modal();
        draw_pipeline_modal();
        
        static bool last_item_active = false;
        bool any_item_active = ImGui::IsAnyItemActive();
        if (any_item_active && !last_item_active) {
            trigger_minion_squeak();
        }
        last_item_active = any_item_active;
        
        ImGui::PopFont();
    }

void BolusApp::draw_top_bar() {
    ImGui::PushFont(m_font_bold);
    ImGui::TextColored(ImVec4(0.88f, 0.55f, 0.25f, 1.0f), "%s", m_tr.title_app.c_str());
    ImGui::PopFont();
    
    // Align controls to the right dynamically on the same line as the title to eliminate vertical dead space
    float title_w = ImGui::CalcTextSize(m_tr.title_app.c_str()).x;
    float right_margin = 16.0f;
    float buttons_width = 140.0f + 145.0f + 145.0f + 100.0f + 100.0f + 100.0f + 120.0f + 130.0f + ImGui::GetStyle().ItemSpacing.x * 8.0f;
    float start_x = ImGui::GetWindowWidth() - buttons_width - right_margin;
    if (start_x < title_w + 20.0f) {
        start_x = title_w + 20.0f; // Prevent overlapping
    }
    ImGui::SameLine(start_x);
    
    std::string kl_label = to_klingon_piqad("tlhIngan Hol");
    struct LangOption {
        Language lang;
        std::string name;
        bool is_separator;
    };
    
    std::vector<LangOption> options = {
        { LANG_AF, "AF (Suid-Afrika)", false },
        { LANG_BN, "বাংলা (Bengali)", false },
        { LANG_BG, "BG (България)", false },
        { LANG_CA, "CA (Catalunya)", false },
        { LANG_ZH_CN, "简体中文", false },
        { LANG_DA, "DA (Danmark)", false },
        { LANG_NL, "NL (Nederland)", false },
        { LANG_EN, "EN (Canada)", false },
        { LANG_FI, "FI (Suomi)", false },
        { LANG_FR, "FR (Québec)", false },
        { LANG_DE_CH, "DE (Schweiz)", false },
        { LANG_EL, "EL (Ελλάδα)", false },
        { LANG_GL, "Kalaallisut", false },
        { LANG_HT, "Kreyòl (Ayiti)", false },
        { LANG_HI, "हिन्दी (Hindi)", false },
        { LANG_ID, "ID (Indonesia)", false },
        { LANG_IU, "IU (ᐃᓄᒃᑎᑐᑦ)", false },
        { LANG_GA, "Gaeilge", false },
        { LANG_IT, "IT (Italia)", false },
        { LANG_JA, "日本語", false },
        { LANG_KO, "한국어", false },
        { LANG_LA, "LA (Vatican City)", false },
        { LANG_NO, "NO (Norge)", false },
        { LANG_RU, "RU (Россия)", false },
        { LANG_SCOTS, "Scots", false },
        { LANG_SR, "SR (Србија)", false },
        { LANG_ES, "ES (España)", false },
        { LANG_SV, "SV (Sverige)", false },
        { LANG_TL, "TL (Pilipinas)", false },
        { LANG_TA, "தமிழ் (Tamil)", false },
        { LANG_TH, "ไทย (Thai)", false },
        { LANG_TR, "TR (Türkiye)", false },
        { LANG_UK, "UK (Україна)", false },
        { LANG_VI, "VI (Việt Nam)", false },
        
        { LANG_EN, "", true }, // Separator
        
        { LANG_EGY, "𓏞𓏝𓆎𓅓𓏏𓊖", false },
        { LANG_GRC, "Ancient Greek", false },
        { LANG_EO, "Esperanto", false },
        { LANG_GENALPHA, "Gen Alpha English", false },
        { LANG_GENZ, "Gen Z English", false },
        { LANG_KL, kl_label, false },
        { LANG_LEET, "Leet Speak", false },
        { LANG_MINION, "Minion (Bello!)", false },
        { LANG_PIRATE, "Pirate English", false },
        { LANG_SHAKESPEARE, "Shakespearean", false },
        { LANG_YODA, "Yoda Speak", false }
    };

    std::string current_display_name = "EN (Canada)";
    for (const auto& opt : options) {
        if (!opt.is_separator && opt.lang == m_lang) {
            current_display_name = opt.name;
            break;
        }
    }

    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::BeginCombo("##LanguageCombo", current_display_name.c_str())) {
        for (const auto& opt : options) {
            if (opt.is_separator) {
                ImGui::Separator();
            } else {
                bool is_selected = (m_lang == opt.lang);
                if (ImGui::Selectable(opt.name.c_str(), is_selected)) {
                    m_lang = opt.lang;
                    update_locale();
                    trigger_minion_squeak();
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    
    if (ImGui::Button(m_tr.text_load_subject_data.c_str(), ImVec2(145, 24))) {
        m_browser.open = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(m_tr.btn_clear_data.c_str(), ImVec2(145, 24))) {
        clear_subject_data();
    }
    ImGui::SameLine();
    if (ImGui::Button(m_tr.btn_save_state.c_str(), ImVec2(100, 24))) {
        if (!m_csv_path.empty()) {
            // Sync current workspace parameters to the GUI state array before saving
            if (m_selected_roi_idx >= 0 && m_selected_roi_idx < static_cast<int>(m_records.size())) {
                auto& s = m_gui_roi_states[m_selected_roi_idx];
                s.crop_min = m_crop_min;
                s.crop_max = m_crop_max;
                s.onset = m_onset_marker;
                s.peak = m_peak_marker;
                s.end = m_end_marker;
                s.baseline = m_baseline_marker;
                s.qc_flag = m_records[m_selected_roi_idx].qc_flag;
                s.fit_source = m_records[m_selected_roi_idx].fit_source;
            }
            save_gui_state();
            ImGui::OpenPopup(m_tr.modal_save_state_success.c_str());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(m_tr.btn_load_state.c_str(), ImVec2(100, 24))) {
        if (!m_csv_path.empty()) {
            load_gui_state();
            if (m_selected_roi_idx >= 0 && m_selected_roi_idx < static_cast<int>(m_records.size())) {
                int target_idx = m_selected_roi_idx;
                m_selected_roi_idx = -1; // Bypass saving current modified state
                select_record(target_idx);
            }
            ImGui::OpenPopup(m_tr.modal_load_state_success.c_str());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(m_tr.btn_reset_all.c_str(), ImVec2(100, 24))) {
        if (!m_csv_path.empty()) {
            m_reset_attempt_count = 0;
            ImGui::OpenPopup(m_tr.modal_reset_title.c_str());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(m_tr.btn_save_csv.c_str(), ImVec2(120, 24))) {
        if (!m_csv_path.empty()) {
            save_results_csv(m_csv_path, m_records);
            save_gui_state();
            std::string audio_path = get_resource_path("resources/hallelujah.mp3");
            play_sound_cross_platform(audio_path);
            ImGui::OpenPopup(m_tr.modal_save_success.c_str());
        }
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.35f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.65f, 0.42f, 1.0f));
    if (ImGui::Button("Run Full Pipeline", ImVec2(130, 24))) {
        m_show_pipeline_modal = true;
    }
    ImGui::PopStyleColor(2);
        
        if (ImGui::BeginPopupModal(m_tr.modal_save_success.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text(m_tr.text_save_csv_msg.c_str(), m_csv_path.c_str());
            ImGui::Separator();
            if (ImGui::Button(m_tr.btn_ok.c_str(), ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal(m_tr.modal_save_state_success.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text(m_tr.text_save_state_msg.c_str(), m_csv_path.c_str());
            ImGui::Separator();
            if (ImGui::Button(m_tr.btn_ok.c_str(), ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal(m_tr.modal_load_state_success.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text(m_tr.text_load_state_msg.c_str(), m_csv_path.c_str());
            ImGui::Separator();
            if (ImGui::Button(m_tr.btn_ok.c_str(), ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal(m_tr.modal_reset_title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s", m_tr.modal_reset_desc.c_str());
            if (m_reset_attempt_count > 0) {
                ImGui::TextColored(ImVec4(0.88f, 0.45f, 0.18f, 1.0f), "Attempts to confirm: %d / 3", m_reset_attempt_count);
            }
            ImGui::Separator();
            if (ImGui::Button(m_tr.btn_reset_confirm.c_str(), ImVec2(120, 0))) {
                if (m_reset_attempt_count < 3) {
                    m_reset_attempt_count++;
                    ImVec2 display_size = ImGui::GetIO().DisplaySize;
                    float max_x = std::max(50.0f, display_size.x - 300.0f);
                    float max_y = std::max(50.0f, display_size.y - 150.0f);
                    float rx = 50.0f + static_cast<float>(std::rand()) / (static_cast<float>(RAND_MAX) / (max_x - 50.0f));
                    float ry = 50.0f + static_cast<float>(std::rand()) / (static_cast<float>(RAND_MAX) / (max_y - 50.0f));
                    ImGui::SetWindowPos(ImVec2(rx, ry));
                } else {
                    m_reset_attempt_count = 0;
                    int active_idx = m_selected_roi_idx;
                    m_selected_roi_idx = -1; // Bypass saving current modified state
                    m_records = m_records_backup;
                    m_gui_roi_states = m_gui_roi_states_backup;
                    for (size_t i = 0; i < m_gui_roi_states.size(); ++i) {
                        precompute_fit_plot(i);
                    }
                    if (active_idx >= 0 && active_idx < static_cast<int>(m_records.size())) {
                        select_record(active_idx);
                    } else {
                        m_selected_roi_idx = -1;
                    }
                    build_triage_queue();
                    save_gui_state();
                    save_active_roi_svg();
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(m_tr.btn_cancel.c_str(), ImVec2(120, 0))) {
                m_reset_attempt_count = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

void BolusApp::draw_sidebar() {
    ImGui::PushFont(m_font_bold);
    ImGui::Text("%s", m_tr.sidebar_title.c_str());
    ImGui::PopFont();
    ImGui::Separator();
    
    const char* items[] = {
        m_tr.filter_all.c_str(),
        m_tr.filter_flagged.c_str(),
        m_tr.filter_fail.c_str(),
        m_tr.filter_warn.c_str(),
        m_tr.filter_pass.c_str(),
        m_tr.filter_review.c_str()
    };
    ImGui::Text("%s", m_tr.label_filter.c_str());
    ImGui::SameLine();
    ImGui::PushItemWidth(-10.0f);
    if (ImGui::Combo("##QcFilterCombo", &m_qc_filter_type, items, IM_ARRAYSIZE(items))) {
        build_triage_queue();
        if (!m_triage_queue.empty()) {
            bool current_still_valid = false;
            for (int idx : m_triage_queue) {
                if (idx == m_selected_roi_idx) {
                    current_still_valid = true;
                    break;
                }
            }
            if (!current_still_valid) {
                select_record(m_triage_queue[0]);
            }
        }
    }
    ImGui::PopItemWidth();
    {
        ImVec2 combo_min = ImGui::GetItemRectMin();
        ImVec2 combo_max = ImGui::GetItemRectMax();
        static int combo_print = 0;
        if (combo_print++ < 10) {
            fprintf(stderr, "BBOX COMBO: %f, %f to %f, %f\n", combo_min.x, combo_min.y, combo_max.x, combo_max.y);
        }
    }
    
    int manual_count = 0;
    for (const auto& r : m_records) {
        if (r.fit_source != "auto") manual_count++;
    }
    ImGui::Text(m_tr.text_sidebar_counts.c_str(), (int)m_records.size(), (int)m_triage_queue.size(), manual_count);
    ImGui::Separator();
    
    ImGui::BeginChild("ListScrollPane", ImVec2(0, 0), false);
    if (ImGui::BeginTable("SidebarListTable", 3, ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("ROI", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch);
        
        for (int q = 0; q < static_cast<int>(m_triage_queue.size()); ++q) {
            int idx = m_triage_queue[q];
            const auto& rec = m_records[idx];
            
            char label[64];
            if (rec.fit_source != "auto") {
                snprintf(label, sizeof(label), "ROI %d *", rec.roi_id);
            } else {
                snprintf(label, sizeof(label), "ROI %d", rec.roi_id);
            }
            
            bool is_selected = (m_selected_roi_idx == idx);
            
            // Format state color tag
            ImVec4 status_color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
            if (rec.qc_flag == "PASS") status_color = ImVec4(0.55f, 0.62f, 0.45f, 1.0f);
            else if (rec.qc_flag == "WARN") status_color = ImVec4(0.92f, 0.72f, 0.30f, 1.0f);
            else if (rec.qc_flag == "FAIL") status_color = ImVec4(0.80f, 0.32f, 0.22f, 1.0f);
            else if (rec.qc_flag == "REVIEW") status_color = ImVec4(0.37f, 0.54f, 0.54f, 1.0f);
            
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            
            ImGui::PushStyleColor(ImGuiCol_Text, status_color);
            if (ImGui::Selectable(label, is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
                select_record(idx);
            }
            ImGui::PopStyleColor();
            
            ImGui::TableNextColumn();
            std::string disp_flag;
            if (rec.qc_flag == "PASS") disp_flag = m_tr.qc_pass;
            else if (rec.qc_flag == "WARN") disp_flag = m_tr.qc_warn;
            else if (rec.qc_flag == "FAIL") disp_flag = m_tr.qc_fail;
            else if (rec.qc_flag == "REVIEW") disp_flag = m_tr.qc_review;
            else disp_flag = rec.qc_flag;
            
            ImGui::TextColored(status_color, "[%s]", disp_flag.c_str());
            
            ImGui::TableNextColumn();
            std::string disp_source;
            if (rec.fit_source == "auto") disp_source = m_tr.source_auto;
            else if (rec.fit_source == "manual") disp_source = m_tr.source_manual;
            else if (rec.fit_source == "override") disp_source = m_tr.source_override;
            else if (rec.fit_source == "population_prior") disp_source = m_tr.source_prior;
            else disp_source = rec.fit_source;
            
            if (rec.fit_source != "auto") {
                // Highlight manually updated fits in terracotta
                ImGui::TextColored(ImVec4(0.88f, 0.55f, 0.25f, 1.0f), "%s", disp_source.c_str());
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", disp_source.c_str());
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

void BolusApp::draw_main_area() {
    if (m_selected_roi_idx < 0) {
        ImGui::Text("%s", m_tr.text_no_data.c_str());
        
        // Check if file browser selected a file
        if (!m_browser.selected_file.empty()) {
            std::filesystem::path full_p = m_browser.current_path / m_browser.selected_file;
            if (std::filesystem::exists(full_p)) {
                load_dataset(full_p.string());
                m_browser.selected_file = "";
            }
        }
        return;
    }
    
    const auto& rec = m_records[m_selected_roi_idx];
    const auto& c = m_cache[m_selected_roi_idx];
    
    // Render plot header in a beautiful rounded pane
    std::string disp_flag;
    if (rec.qc_flag == "PASS") disp_flag = m_tr.qc_pass;
    else if (rec.qc_flag == "WARN") disp_flag = m_tr.qc_warn;
    else if (rec.qc_flag == "FAIL") disp_flag = m_tr.qc_fail;
    else if (rec.qc_flag == "REVIEW") disp_flag = m_tr.qc_review;
    else disp_flag = rec.qc_flag;

    std::string disp_source;
    if (rec.fit_source == "auto") disp_source = m_tr.source_auto;
    else if (rec.fit_source == "manual") disp_source = m_tr.source_manual;
    else if (rec.fit_source == "override") disp_source = m_tr.source_override;
    else if (rec.fit_source == "population_prior") disp_source = m_tr.source_prior;
    else disp_source = rec.fit_source;
    
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
    ImGui::BeginChild("PlotHeaderPane", ImVec2(0, 48), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    
    float avail_w = ImGui::GetContentRegionAvail().x;
    float nav_btn_w = m_lang == LANG_FR ? 100.0f : 90.0f;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    char queue_text[64];
    snprintf(queue_text, sizeof(queue_text), "%d / %d", m_queue_pos + 1, (int)m_triage_queue.size());
    float text_w = ImGui::CalcTextSize(queue_text).x;
    float total_buttons_w = nav_btn_w * 2.0f + text_w + spacing * 2.0f;
    
    if (ImGui::BeginTable("PlotHeaderTable", 2, ImGuiTableFlags_None)) {
        ImGui::TableSetupColumn("HeaderText", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("HeaderNav", ImGuiTableColumnFlags_WidthFixed, total_buttons_w + 10.0f);
        
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
        ImGui::Indent(4.0f);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 10.0f);
        ImGui::Text(m_tr.text_plot_status_header.c_str(), rec.roi_id, rec.roi_size, disp_flag.c_str(), disp_source.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Unindent(4.0f);
        
        ImGui::TableNextColumn();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
        if (ImGui::Button(m_tr.btn_prev_short.c_str(), ImVec2(nav_btn_w, 22))) {
            if (m_queue_pos > 0) {
                select_record(m_triage_queue[m_queue_pos - 1]);
            }
        }
        ImGui::SameLine();
        ImGui::Text("%s", queue_text);
        ImGui::SameLine();
        if (ImGui::Button(m_tr.btn_next_short.c_str(), ImVec2(nav_btn_w, 22))) {
            if (m_queue_pos >= 0 && m_queue_pos + 1 < static_cast<int>(m_triage_queue.size())) {
                select_record(m_triage_queue[m_queue_pos + 1]);
            }
        }
        ImGui::PopStyleVar();
        
        ImGui::EndTable();
    }
    ImGui::EndChild();
        
        // Calculate Y limits with a 10% buffer based on visible traces in the crop range
        double visible_y_min = std::numeric_limits<double>::max();
        double visible_y_max = -std::numeric_limits<double>::max();
        
        for (size_t i = 0; i < c.t_raw.size(); ++i) {
            double t = c.t_raw[i];
            if (t >= m_crop_min && t <= m_crop_max) {
                if (c.y_raw_detrended[i] < visible_y_min) visible_y_min = c.y_raw_detrended[i];
                if (c.y_raw_detrended[i] > visible_y_max) visible_y_max = c.y_raw_detrended[i];
                
                if (c.y_denoised[i] < visible_y_min) visible_y_min = c.y_denoised[i];
                if (c.y_denoised[i] > visible_y_max) visible_y_max = c.y_denoised[i];
            }
        }
        
        if (!c.y_fit_plot.empty()) {
            for (size_t i = 0; i < c.t_fit_plot.size(); ++i) {
                double t = c.t_fit_plot[i];
                if (t >= m_crop_min && t <= m_crop_max) {
                    if (c.y_fit_plot[i] < visible_y_min) visible_y_min = c.y_fit_plot[i];
                    if (c.y_fit_plot[i] > visible_y_max) visible_y_max = c.y_fit_plot[i];
                }
            }
        }
        if (rec.fit_source != "auto" && !c.y_fit_auto_plot.empty()) {
            for (size_t i = 0; i < c.t_fit_auto_plot.size(); ++i) {
                double t = c.t_fit_auto_plot[i];
                if (t >= m_crop_min && t <= m_crop_max) {
                    if (c.y_fit_auto_plot[i] < visible_y_min) visible_y_min = c.y_fit_auto_plot[i];
                    if (c.y_fit_auto_plot[i] > visible_y_max) visible_y_max = c.y_fit_auto_plot[i];
                }
            }
        }
        
        if (visible_y_min > visible_y_max) {
            visible_y_min = 0.0;
            visible_y_max = 100.0;
        }
        
        double y_range = visible_y_max - visible_y_min;
        if (y_range <= 0.0) y_range = 1.0;
        
        double y_limit_min = visible_y_min - 0.15 * y_range;
        double y_limit_max = visible_y_max + 0.15 * y_range;
        
        // Sanitization to prevent crashes in ImPlot assertions (e.g. on NaNs, Inf, or empty ranges)
        if (std::isnan(m_crop_min) || std::isinf(m_crop_min)) m_crop_min = 0.0;
        if (std::isnan(m_crop_max) || std::isinf(m_crop_max)) m_crop_max = c.t_raw.empty() ? 120.0 : c.t_raw.back();
        if (std::isnan(m_crop_max) || std::isinf(m_crop_max)) m_crop_max = 120.0;
        if (m_crop_min >= m_crop_max) m_crop_max = m_crop_min + 1.0;

        if (std::isnan(y_limit_min) || std::isinf(y_limit_min)) y_limit_min = 0.0;
        if (std::isnan(y_limit_max) || std::isinf(y_limit_max)) y_limit_max = 100.0;
        if (y_limit_min >= y_limit_max) y_limit_max = y_limit_min + 1.0;

        // Draggable markers sanitization
        if (std::isnan(m_onset_marker) || std::isinf(m_onset_marker)) m_onset_marker = m_crop_min + 0.35 * (m_crop_max - m_crop_min);
        if (std::isnan(m_peak_marker) || std::isinf(m_peak_marker)) m_peak_marker = m_onset_marker + 4.0;
        if (std::isnan(m_end_marker) || std::isinf(m_end_marker)) m_end_marker = m_peak_marker + 6.0;
        if (std::isnan(m_baseline_marker) || std::isinf(m_baseline_marker)) m_baseline_marker = (y_limit_min + y_limit_max) / 2.0;

        // Ensure order constraints even on NaNs/Infs
        m_onset_marker = std::clamp(m_onset_marker, m_crop_min, m_crop_max);
        m_peak_marker = std::clamp(m_peak_marker, m_onset_marker + 0.01, m_crop_max);
        m_end_marker = std::clamp(m_end_marker, m_peak_marker + 0.01, m_crop_max);

        // Draggable Baseline visual crop limits setup
        ImPlot::SetNextAxesLimits(m_crop_min, m_crop_max, y_limit_min, y_limit_max, ImGuiCond_Always);
        
        ImVec2 plot_pos(0.0f, 0.0f);
        ImVec2 plot_size(0.0f, 0.0f);
        
        // Calculate dynamic plot height based on available window height to fit everything else
        float avail_h = ImGui::GetContentRegionAvail().y;
        float plot_h = avail_h - 410.0f; // Reserved for ParamsPane and RangeSlider/Header
        if (plot_h < 240.0f) plot_h = 240.0f;
        if (plot_h > 900.0f) plot_h = 900.0f;

        if (ImPlot::BeginPlot(m_tr.plot_title.c_str(), ImVec2(-1, plot_h))) {
            ImPlot::SetupAxes(m_tr.plot_x_axis.c_str(), m_tr.plot_y_axis.c_str());
            
            // Limit the current axis limits to show cropped visual window on the fly
            ImPlot::SetupAxisLimits(ImAxis_X1, m_crop_min, m_crop_max, ImGuiCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, y_limit_min, y_limit_max, ImGuiCond_Always);

            // Query plot area screen coordinates and dimensions after setup to avoid locking setup early
            plot_pos = ImPlot::GetPlotPos();
            plot_size = ImPlot::GetPlotSize();
            
            // Draw visual curves
            ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 1.5f);
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.85f, 0.78f, 0.62f, 0.70f)); // Warm gold/brass
            ImPlot::PlotLine(m_tr.plot_raw.c_str(), c.t_raw.data(), c.y_raw_detrended.data(), c.t_raw.size());
            ImPlot::PopStyleColor();
            
            ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.37f, 0.64f, 0.64f, 1.0f)); // Muted Sage/Teal
            ImPlot::PlotLine(m_tr.plot_denoised.c_str(), c.t_raw.data(), c.y_denoised.data(), c.t_raw.size());
            ImPlot::PopStyleColor();
            
            if (!c.y_fit_plot.empty()) {
                if (rec.fit_source != "auto" && !c.y_fit_auto_plot.empty()) {
                    ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.27f, 0.51f, 0.71f, 0.5f)); // Steel blue, semi-transparent
                    ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 1.5f);
                    ImPlot::PlotLine(m_tr.label_auto_fit.c_str(), c.t_fit_auto_plot.data(), c.y_fit_auto_plot.data(), c.t_fit_auto_plot.size());
                    ImPlot::PopStyleVar();
                    ImPlot::PopStyleColor();
                }
                
                ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0.88f, 0.45f, 0.18f, 1.0f)); // Terracotta orange
                ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2.5f);
                ImPlot::PlotLine(m_tr.plot_fit.c_str(), c.t_fit_plot.data(), c.y_fit_plot.data(), c.t_fit_plot.size());
                ImPlot::PopStyleVar();
                ImPlot::PopStyleColor();
            }
            ImPlot::PopStyleVar();
            
            // Draggable Lines
            ImPlot::DragLineX(101, &m_onset_marker, ImVec4(0.55f, 0.62f, 0.45f, 1.0f), 2.0f); // Green Onset
            ImPlot::DragLineX(102, &m_peak_marker, ImVec4(0.92f, 0.72f, 0.30f, 1.0f), 2.0f);  // Yellow Peak
            ImPlot::DragLineX(103, &m_end_marker, ImVec4(0.80f, 0.32f, 0.22f, 1.0f), 2.0f);   // Red End
            
            ImPlot::DragLineY(104, &m_baseline_marker, ImVec4(0.68f, 0.48f, 0.68f, 1.0f), 2.0f); // Purple Baseline
            
            // Enforce ordering and bounds constraints on markers immediately
            double max_t = c.t_raw.empty() ? 120.0 : c.t_raw.back();
            if (std::isnan(max_t) || std::isinf(max_t)) max_t = 120.0;
            const double min_gap = 0.1; // 100 ms minimum gap
            m_onset_marker = std::clamp(m_onset_marker, 0.0, max_t - 2.0 * min_gap);
            m_peak_marker = std::clamp(m_peak_marker, m_onset_marker + min_gap, max_t - min_gap);
            m_end_marker = std::clamp(m_end_marker, m_peak_marker + min_gap, max_t);
            
            // Annotate Draggable Lines with formatted numeric values
            ImPlot::TagX(m_onset_marker, ImVec4(0.55f, 0.62f, 0.45f, 1.0f), m_tr.tag_onset.c_str(), m_onset_marker);
            ImPlot::TagX(m_peak_marker, ImVec4(0.92f, 0.72f, 0.30f, 1.0f), m_tr.tag_peak.c_str(), m_peak_marker);
            ImPlot::TagX(m_end_marker, ImVec4(0.80f, 0.32f, 0.22f, 1.0f), m_tr.tag_end.c_str(), m_end_marker);
            {
                ImVec2 o_px = ImPlot::PlotToPixels(m_onset_marker, 0);
                ImVec2 p_px = ImPlot::PlotToPixels(m_peak_marker, 0);
                ImVec2 e_px = ImPlot::PlotToPixels(m_end_marker, 0);
                fprintf(stderr, "MARKER PIXELS: Onset=%f, Peak=%f, End=%f\n", o_px.x, p_px.x, e_px.x);
            }
            ImPlot::TagY(m_baseline_marker, ImVec4(0.68f, 0.48f, 0.68f, 1.0f), m_tr.tag_base.c_str(), m_baseline_marker);
            
            ImPlot::EndPlot();
        }
        
        // Render unified RangeSlider right below the plot, aligned exactly with the plot width
        {
            if (plot_size.x > 0.0f) {
                ImVec2 cursor_pos = ImGui::GetCursorPos();
                ImGui::SetCursorPosX(plot_pos.x - ImGui::GetWindowPos().x);
                double limit_max = c.t_raw.empty() ? 120.0 : c.t_raw.back();
                RangeSlider("PlotCropSlider", &m_crop_min, &m_crop_max, 0.0, limit_max, ImVec2(plot_size.x, 24.0f), m_tr.text_visual_crop_range.c_str());
                ImGui::SetCursorPosX(cursor_pos.x);
            } else {
                float avail_w = ImGui::GetContentRegionAvail().x;
                double limit_max = c.t_raw.empty() ? 120.0 : c.t_raw.back();
                RangeSlider("PlotCropSlider", &m_crop_min, &m_crop_max, 0.0, limit_max, ImVec2(avail_w, 24.0f), m_tr.text_visual_crop_range.c_str());
            }
            ImVec2 slider_min = ImGui::GetItemRectMin();
            ImVec2 slider_max = ImGui::GetItemRectMax();
            fprintf(stderr, "BBOX SLIDER: %f, %f to %f, %f\n", slider_min.x, slider_min.y, slider_max.x, slider_max.y);
        }
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        
        // Parameters & Controls Panel
        ImGui::BeginChild("ParamsPane", ImVec2(0, 0), true);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 2.0f));
        
        // Manual fitting and cropping with dynamic proportions
        float controls_total_w = ImGui::GetContentRegionAvail().x;
        ImGui::Columns(3, "ControlsGrid", false);
        ImGui::SetColumnWidth(0, controls_total_w * 0.52f);
        ImGui::SetColumnWidth(1, controls_total_w * 0.26f);
        // Column 2 takes the remainder
        
        ImGui::PushFont(m_font_bold);
        ImGui::TextColored(ImVec4(0.88f, 0.55f, 0.25f, 1.0f), "%s", m_tr.section_markers.c_str());
        ImGui::PopFont();
        double min_t = 0.0;
        double max_t = c.t_raw.back();
        double base_min = 0.0;
        double base_max = 1000.0;
        
        if (ImGui::BeginTable("SlidersGridTable", 2, ImGuiTableFlags_None)) {
            float col_w = ImGui::GetContentRegionAvail().x * 0.5f;
            ImGui::TableSetupColumn("Col1", ImGuiTableColumnFlags_WidthFixed, col_w);
            ImGui::TableSetupColumn("Col2", ImGuiTableColumnFlags_WidthFixed, col_w);
            
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushItemWidth(col_w * 0.52f);
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.55f, 0.62f, 0.45f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.65f, 0.72f, 0.55f, 1.0f));
            ImGui::SliderScalar(m_tr.label_onset.c_str(), ImGuiDataType_Double, &m_onset_marker, &min_t, &max_t, "%.1f");
            ImGui::PopStyleColor(2);
            ImGui::PopItemWidth();
            
            ImGui::TableNextColumn();
            ImGui::PushItemWidth(col_w * 0.52f);
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.92f, 0.72f, 0.30f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.00f, 0.82f, 0.40f, 1.0f));
            ImGui::SliderScalar(m_tr.label_peak.c_str(), ImGuiDataType_Double, &m_peak_marker, &m_onset_marker, &max_t, "%.1f");
            ImGui::PopStyleColor(2);
            ImGui::PopItemWidth();
            
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushItemWidth(col_w * 0.52f);
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.80f, 0.32f, 0.22f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.90f, 0.42f, 0.32f, 1.0f));
            ImGui::SliderScalar(m_tr.label_end.c_str(), ImGuiDataType_Double, &m_end_marker, &m_peak_marker, &max_t, "%.1f");
            ImGui::PopStyleColor(2);
            ImGui::PopItemWidth();
            
            ImGui::TableNextColumn();
            ImGui::PushItemWidth(col_w * 0.52f);
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.68f, 0.48f, 0.68f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.78f, 0.58f, 0.78f, 1.0f));
            ImGui::SliderScalar(m_tr.label_baseline.c_str(), ImGuiDataType_Double, &m_baseline_marker, &base_min, &base_max, "%.1f");
            ImGui::PopStyleColor(2);
            ImGui::PopItemWidth();
            
            ImGui::EndTable();
        }
        
        ImGui::NextColumn();
        
        ImGui::PushFont(m_font_bold);
        ImGui::TextColored(ImVec4(0.88f, 0.55f, 0.25f, 1.0f), "%s", m_tr.section_crop.c_str());
        ImGui::PopFont();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", m_tr.text_slider_desc.c_str());
        }
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.38f, 0.42f, 0.35f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.46f, 0.52f, 0.42f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.88f, 0.55f, 0.25f, 1.00f));

        if (ImGui::Button(m_tr.btn_reset_crop.c_str(), ImVec2(180, 26))) {
            m_crop_min = 0.0;
            m_crop_max = c.t_raw.back();
        }
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        if (ImGui::Button(m_tr.btn_crop_bounds.c_str(), ImVec2(180, 26))) {
            m_crop_min = std::max(0.0, m_onset_marker - 5.0);
            m_crop_max = std::min(c.t_raw.back(), m_end_marker + 10.0);
        }
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        if (ImGui::Button(m_tr.btn_view_roi_mip.c_str(), ImVec2(180, 26))) {
            update_mip_texture();
            m_show_mip_modal = true;
        }

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
        
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::PushFont(m_font_bold);
        ImGui::TextColored(ImVec4(0.88f, 0.55f, 0.25f, 1.0f), "%s", m_tr.section_denoise.c_str());
        ImGui::PopFont();
        
        ImGui::PushItemWidth(180.0f);
        if (ImGui::SliderFloat(m_tr.label_denoise_strength.c_str(), &m_denoise_strength_factor, 0.5f, 3.0f, "%.2fx")) {
            if (m_selected_roi_idx >= 0 && m_selected_roi_idx < static_cast<int>(m_records.size())) {
                precompute_single_trace(m_selected_roi_idx);
                select_record(m_selected_roi_idx);
            }
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            precompute_all_traces();
        }
        ImGui::PopItemWidth();
        
        ImGui::NextColumn();
        
        ImGui::PushFont(m_font_bold);
        ImGui::TextColored(ImVec4(0.88f, 0.55f, 0.25f, 1.0f), "%s", m_tr.section_actions.c_str());
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        
        float btn_w = ImGui::GetContentRegionAvail().x - 8.0f;
        if (ImGui::Button(m_tr.btn_refit.c_str(), ImVec2(btn_w, 26))) {
            run_fit_on_current_roi();
        }
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        if (ImGui::Button(m_tr.btn_override.c_str(), ImVec2(btn_w, 26))) {
            m_records[m_selected_roi_idx].qc_flag = "PASS";
            m_records[m_selected_roi_idx].fit_source = "override";
            if (m_selected_roi_idx >= 0 && m_selected_roi_idx < static_cast<int>(m_gui_roi_states.size())) {
                m_gui_roi_states[m_selected_roi_idx].qc_flag = "PASS";
                m_gui_roi_states[m_selected_roi_idx].fit_source = "override";
            }
            build_triage_queue();
            save_active_roi_svg();
        }
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        if (ImGui::Button(m_tr.btn_revert.c_str(), ImVec2(btn_w, 26))) {
            if (m_selected_roi_idx >= 0 && m_selected_roi_idx < static_cast<int>(m_records_backup.size())) {
                int idx = m_selected_roi_idx;
                m_records[idx] = m_records_backup[idx];
                m_gui_roi_states[idx] = m_gui_roi_states_backup[idx];
                m_selected_roi_idx = -1; // Bypass saving current modified state
                select_record(idx);
                precompute_fit_plot(idx);
                build_triage_queue();
                save_active_roi_svg();
            }
        }
        
        ImGui::Columns(1);
        ImGui::Separator();
        
        // Active fit parameters table comparison
        ImGui::PushFont(m_font_bold);
        ImGui::TextColored(ImVec4(0.88f, 0.55f, 0.25f, 1.0f), "%s", m_tr.section_params.c_str());
        ImGui::PopFont();
        if (ImGui::BeginTable("ParamsTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn(m_tr.col_variable.c_str());
            ImGui::TableSetupColumn(m_tr.col_amplitude.c_str());
            ImGui::TableSetupColumn(m_tr.col_t2p.c_str());
            ImGui::TableSetupColumn(m_tr.col_fwhm.c_str());
            ImGui::TableSetupColumn(m_tr.col_baseline.c_str());
            ImGui::TableSetupColumn(m_tr.col_cnr.c_str());
            ImGui::TableSetupColumn(m_tr.col_onset.c_str());
            ImGui::TableHeadersRow();
            
            auto display_val = [this](double val) {
                if (std::isnan(val)) ImGui::Text("%s", m_tr.text_na.c_str());
                else ImGui::Text("%.4f", val);
            };
            
            // Fitted row
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%s", m_tr.label_fitted.c_str());
            ImGui::TableNextColumn(); display_val(rec.f_amp);
            ImGui::TableNextColumn(); display_val(rec.f_t2p);
            ImGui::TableNextColumn(); display_val(rec.f_fwhm);
            ImGui::TableNextColumn(); display_val(rec.f_m);
            ImGui::TableNextColumn(); display_val(rec.f_cnr);
            ImGui::TableNextColumn(); display_val(rec.ont);
            
            // Pre-guess row
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%s", m_tr.label_estimated_init.c_str());
            ImGui::TableNextColumn(); display_val(rec.init_amp);
            ImGui::TableNextColumn(); display_val(rec.init_t2p);
            ImGui::TableNextColumn(); display_val(rec.init_fwhm);
            ImGui::TableNextColumn(); display_val(rec.init_m);
            ImGui::TableNextColumn(); display_val(rec.init_cnr);
            ImGui::TableNextColumn(); display_val(rec.click_onset);
            
            ImGui::EndTable();
        }
        
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        
        // Kinetics & Classification table
        ImGui::PushFont(m_font_bold);
        ImGui::TextColored(ImVec4(0.88f, 0.55f, 0.25f, 1.0f), "%s", m_tr.text_kinetics_title.c_str());
        ImGui::PopFont();
        if (ImGui::BeginTable("KineticsTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn(m_tr.col_auc.c_str());
            ImGui::TableSetupColumn(m_tr.col_aucn.c_str());
            ImGui::TableSetupColumn(m_tr.col_onset_scan.c_str());
            ImGui::TableSetupColumn(m_tr.col_tt_lower.c_str());
            ImGui::TableSetupColumn(m_tr.col_tt_peak.c_str());
            ImGui::TableSetupColumn(m_tr.col_tt_upper.c_str());
            ImGui::TableSetupColumn(m_tr.col_vessel_type.c_str());
            ImGui::TableHeadersRow();
            
            auto display_val = [this](double val) {
                if (std::isnan(val)) ImGui::Text("%s", m_tr.text_na.c_str());
                else ImGui::Text("%.4f", val);
            };
            
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); display_val(rec.auc);
            ImGui::TableNextColumn(); display_val(rec.aucn);
            ImGui::TableNextColumn(); display_val(rec.ont_sc);
            ImGui::TableNextColumn(); display_val(rec.ttlb);
            ImGui::TableNextColumn(); display_val(rec.ttm);
            ImGui::TableNextColumn(); display_val(rec.tthb);
            
            ImGui::TableNextColumn();
            std::string ves_label;
            if (rec.ves_type == "A") ves_label = m_tr.ves_artery;
            else if (rec.ves_type == "V") ves_label = m_tr.ves_vein;
            else if (rec.ves_type == "C") ves_label = m_tr.ves_capillary;
            else ves_label = m_tr.ves_unknown;
            ImGui::Text("%s", ves_label.c_str());
            
            ImGui::EndTable();
        }
        
        ImGui::Columns(1);
        ImGui::PopStyleVar(2);
        ImGui::EndChild();
    }

void BolusApp::clear_subject_data() {
    m_csv_path.clear();
    m_tiff_path.clear();
    m_rois_path.clear();
    m_meta_path.clear();
    m_records.clear();
    m_records_backup.clear();
    m_cache.clear();
    m_gui_roi_states.clear();
    m_gui_roi_states_backup.clear();
    m_selected_roi_idx = -1;
    m_triage_queue.clear();
    m_queue_pos = -1;
    m_rois.clear();
    m_tiff = TiffData();
    m_denoise_strength_factor = 1.0f;
}

void BolusApp::apply_theme_colors() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;
    
    if (m_lang == LANG_MINION) {
        // Minion Theme: Denim Blue and Minion Yellow
        colors[ImGuiCol_Text]                   = ImVec4(0.95f, 0.95f, 0.90f, 1.00f);
        colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.55f, 0.65f, 1.00f);
        colors[ImGuiCol_WindowBg]               = ImVec4(0.9843f, 0.8510f, 0.1098f, 1.00f); // Minion Yellow far background
        colors[ImGuiCol_ChildBg]                = ImVec4(0.08f, 0.17f, 0.38f, 0.95f);       // Denim Blue child panels
        colors[ImGuiCol_PopupBg]                = ImVec4(0.08f, 0.17f, 0.38f, 0.98f);
        colors[ImGuiCol_Border]                 = ImVec4(0.9843f, 0.8510f, 0.1098f, 0.80f); // Vibrant Yellow border
        colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg]                = ImVec4(0.05f, 0.10f, 0.22f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.08f, 0.16f, 0.34f, 1.00f);
        colors[ImGuiCol_FrameBgActive]          = ImVec4(0.12f, 0.22f, 0.44f, 1.00f);
        colors[ImGuiCol_TitleBg]                = ImVec4(0.08f, 0.17f, 0.38f, 1.00f);
        colors[ImGuiCol_TitleBgActive]          = ImVec4(0.08f, 0.17f, 0.38f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.05f, 0.10f, 0.22f, 1.00f);
        colors[ImGuiCol_MenuBarBg]              = ImVec4(0.08f, 0.17f, 0.38f, 1.00f);
        colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.05f, 0.10f, 0.22f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.80f, 0.60f, 0.00f, 1.00f);       // Gold scrollbar
        colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.90f, 0.70f, 0.10f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.9843f, 0.8510f, 0.1098f, 1.00f);
        colors[ImGuiCol_CheckMark]              = ImVec4(0.9843f, 0.8510f, 0.1098f, 1.00f);
        colors[ImGuiCol_SliderGrab]             = ImVec4(0.9843f, 0.8510f, 0.1098f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]      = ImVec4(1.00f, 0.90f, 0.25f, 1.00f);
        colors[ImGuiCol_Button]                 = ImVec4(0.80f, 0.60f, 0.00f, 1.00f);       // Rich Gold buttons
        colors[ImGuiCol_ButtonHovered]          = ImVec4(0.90f, 0.70f, 0.10f, 1.00f);
        colors[ImGuiCol_ButtonActive]           = ImVec4(1.00f, 0.85f, 0.20f, 1.00f);
        colors[ImGuiCol_Header]                 = ImVec4(0.08f, 0.17f, 0.38f, 1.00f);
        colors[ImGuiCol_HeaderHovered]          = ImVec4(0.80f, 0.60f, 0.00f, 0.80f);
        colors[ImGuiCol_HeaderActive]           = ImVec4(0.80f, 0.60f, 0.00f, 1.00f);
        colors[ImGuiCol_Separator]              = ImVec4(0.9843f, 0.8510f, 0.1098f, 0.50f);
        colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.9843f, 0.8510f, 0.1098f, 0.80f);
        colors[ImGuiCol_SeparatorActive]        = ImVec4(0.9843f, 0.8510f, 0.1098f, 1.00f);
        colors[ImGuiCol_ResizeGrip]             = ImVec4(0.80f, 0.60f, 0.00f, 0.20f);
        colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.80f, 0.60f, 0.00f, 0.67f);
        colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.9843f, 0.8510f, 0.1098f, 0.95f);
        colors[ImGuiCol_Tab]                    = ImVec4(0.08f, 0.17f, 0.38f, 0.86f);
        colors[ImGuiCol_TabHovered]             = ImVec4(0.80f, 0.60f, 0.00f, 0.80f);
        colors[ImGuiCol_TabActive]              = ImVec4(0.80f, 0.60f, 0.00f, 1.00f);
        colors[ImGuiCol_TabUnfocused]           = ImVec4(0.05f, 0.10f, 0.22f, 0.97f);
        colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.08f, 0.17f, 0.38f, 1.00f);
        colors[ImGuiCol_PlotLines]              = ImVec4(0.95f, 0.95f, 0.90f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered]       = ImVec4(0.9843f, 0.8510f, 0.1098f, 1.00f);
        colors[ImGuiCol_PlotHistogram]          = ImVec4(0.9843f, 0.8510f, 0.1098f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(1.00f, 0.90f, 0.25f, 1.00f);
        colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.08f, 0.17f, 0.38f, 1.00f);
        colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.9843f, 0.8510f, 0.1098f, 0.35f);
        colors[ImGuiCol_TableBorderLight]       = ImVec4(0.08f, 0.17f, 0.38f, 0.35f);
        colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
        colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.9843f, 0.8510f, 0.1098f, 0.35f);
        colors[ImGuiCol_DragDropTarget]         = ImVec4(0.9843f, 0.8510f, 0.1098f, 0.90f);
        colors[ImGuiCol_NavHighlight]           = ImVec4(0.9843f, 0.8510f, 0.1098f, 1.00f);
        colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
        colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.05f, 0.10f, 0.22f, 0.60f);
    } else {
        // Restore Premium Mid-Century Modern Theme
        colors[ImGuiCol_Text]                   = ImVec4(0.95f, 0.94f, 0.90f, 1.00f);
        colors[ImGuiCol_TextDisabled]           = ImVec4(0.60f, 0.58f, 0.55f, 1.00f);
        colors[ImGuiCol_WindowBg]               = ImVec4(0.18f, 0.18f, 0.17f, 1.00f);
        colors[ImGuiCol_ChildBg]                = ImVec4(0.22f, 0.22f, 0.20f, 0.95f);
        colors[ImGuiCol_PopupBg]                = ImVec4(0.20f, 0.20f, 0.19f, 0.98f);
        colors[ImGuiCol_Border]                 = ImVec4(0.35f, 0.32f, 0.28f, 0.50f);
        colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg]                = ImVec4(0.26f, 0.25f, 0.23f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.32f, 0.30f, 0.28f, 1.00f);
        colors[ImGuiCol_FrameBgActive]          = ImVec4(0.38f, 0.35f, 0.32f, 1.00f);
        colors[ImGuiCol_TitleBg]                = ImVec4(0.28f, 0.25f, 0.22f, 1.00f);
        colors[ImGuiCol_TitleBgActive]          = ImVec4(0.32f, 0.28f, 0.24f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.20f, 0.18f, 0.16f, 1.00f);
        colors[ImGuiCol_MenuBarBg]              = ImVec4(0.22f, 0.22f, 0.20f, 1.00f);
        colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.18f, 0.18f, 0.17f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.40f, 0.38f, 0.34f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.50f, 0.46f, 0.42f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.60f, 0.55f, 0.50f, 1.00f);
        colors[ImGuiCol_CheckMark]              = ImVec4(0.88f, 0.55f, 0.25f, 1.00f);
        colors[ImGuiCol_SliderGrab]             = ImVec4(0.50f, 0.58f, 0.45f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.88f, 0.55f, 0.25f, 1.00f);
        colors[ImGuiCol_Button]                 = ImVec4(0.38f, 0.42f, 0.35f, 1.00f);
        colors[ImGuiCol_ButtonHovered]          = ImVec4(0.46f, 0.52f, 0.42f, 1.00f);
        colors[ImGuiCol_ButtonActive]           = ImVec4(0.88f, 0.55f, 0.25f, 1.00f);
        colors[ImGuiCol_Header]                 = ImVec4(0.35f, 0.38f, 0.32f, 1.00f);
        colors[ImGuiCol_HeaderHovered]          = ImVec4(0.42f, 0.46f, 0.38f, 1.00f);
        colors[ImGuiCol_HeaderActive]           = ImVec4(0.50f, 0.55f, 0.45f, 1.00f);
        colors[ImGuiCol_Separator]              = ImVec4(0.35f, 0.32f, 0.28f, 0.50f);
        colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.88f, 0.55f, 0.25f, 0.78f);
        colors[ImGuiCol_SeparatorActive]        = ImVec4(0.88f, 0.55f, 0.25f, 1.00f);
        colors[ImGuiCol_ResizeGrip]             = ImVec4(0.38f, 0.42f, 0.35f, 0.20f);
        colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.38f, 0.42f, 0.35f, 0.67f);
        colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.88f, 0.55f, 0.25f, 0.95f);
        colors[ImGuiCol_Tab]                    = ImVec4(0.28f, 0.30f, 0.26f, 0.86f);
        colors[ImGuiCol_TabHovered]             = ImVec4(0.38f, 0.42f, 0.35f, 0.80f);
        colors[ImGuiCol_TabActive]              = ImVec4(0.38f, 0.42f, 0.35f, 1.00f);
        colors[ImGuiCol_TabUnfocused]           = ImVec4(0.20f, 0.22f, 0.18f, 0.97f);
        colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.28f, 0.30f, 0.26f, 1.00f);
        colors[ImGuiCol_PlotLines]              = ImVec4(0.85f, 0.80f, 0.70f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered]       = ImVec4(0.88f, 0.55f, 0.25f, 1.00f);
        colors[ImGuiCol_PlotHistogram]          = ImVec4(0.88f, 0.55f, 0.25f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(0.95f, 0.65f, 0.35f, 1.00f);
        colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.26f, 0.26f, 0.24f, 1.00f);
        colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.35f, 0.35f, 0.32f, 1.00f);
        colors[ImGuiCol_TableBorderLight]       = ImVec4(0.28f, 0.28f, 0.26f, 1.00f);
        colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
        colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.88f, 0.55f, 0.25f, 0.35f);
        colors[ImGuiCol_DragDropTarget]         = ImVec4(0.88f, 0.55f, 0.25f, 0.90f);
        colors[ImGuiCol_NavHighlight]           = ImVec4(0.88f, 0.55f, 0.25f, 1.00f);
        colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
        colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.12f, 0.12f, 0.11f, 0.60f);
    }
}

void BolusApp::ensure_minion_squeak_exists() {
    std::string path = get_resource_path("resources/minion_squeak.wav");
    std::filesystem::path p(path);
    if (std::filesystem::exists(p)) {
        return;
    }
    if (!p.parent_path().empty()) {
        std::filesystem::create_directories(p.parent_path());
    }
    
    std::ofstream out(p, std::ios::binary);
    if (!out) return;
    
    struct WavHeader {
        char riff_header[4] = {'R', 'I', 'F', 'F'};
        int32_t wav_size;
        char wave_header[4] = {'W', 'A', 'V', 'E'};
        char fmt_header[4] = {'f', 'm', 't', ' '};
        int32_t fmt_chunk_size = 16;
        int16_t audio_format = 1;
        int16_t num_channels = 1;
        int32_t sample_rate = 44100;
        int32_t byte_rate = 44100 * 2;
        int16_t sample_alignment = 2;
        int16_t bit_depth = 16;
        char data_header[4] = {'d', 'a', 't', 'a'};
        int32_t data_size;
    } header;
    
    double duration = 0.18;
    int sample_rate = 44100;
    int total_samples = static_cast<int>(duration * sample_rate);
    int data_size = total_samples * 2;
    
    header.data_size = data_size;
    header.wav_size = 36 + data_size;
    
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    
    double phase = 0.0;
    const double pi_val = 3.14159265358979323846;
    for (int i = 0; i < total_samples; ++i) {
        double t = static_cast<double>(i) / sample_rate;
        double f = 750.0 + 650.0 * (t / 0.18) + 80.0 * std::sin(2.0 * pi_val * 25.0 * t);
        phase += 2.0 * pi_val * f / sample_rate;
        double val = std::sin(phase);
        
        double env = 1.0;
        if (t < 0.02) {
            env = t / 0.02;
        } else if (t > 0.14) {
            env = (0.18 - t) / 0.04;
            if (env < 0.0) env = 0.0;
        }
        
        double amp = 0.5 * env;
        int16_t sample = static_cast<int16_t>(val * amp * 32767.0);
        out.write(reinterpret_cast<const char*>(&sample), sizeof(sample));
    }
}

void BolusApp::trigger_minion_squeak() {
    if (m_lang == LANG_MINION) {
        play_sound_cross_platform(get_resource_path("resources/minion_squeak.wav"));
    }
}

// ============================================================================
// Application Entry Point
// ============================================================================

int main(int argc, char** argv) {
    TIFFSetWarningHandler(nullptr);
    BolusApp app;
    if (!app.init()) {
        std::cerr << "Failed to initialize Bolus GUI App!" << std::endl;
        return -1;
    }
    
    // Automatically load dataset if passed on command line
    if (argc > 1) {
        std::filesystem::path path(argv[1]);
        if (std::filesystem::exists(path)) {
            std::string abs_path = std::filesystem::absolute(path).string();
            app.load_dataset(abs_path);
        }
    }
    
    app.run();
    return 0;
}
