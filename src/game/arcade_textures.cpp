// Arcade textures (see cspeed_arcade.h).
//
// The arcade drive uses Atari Games' file system: a home block (0xFEEDF00D) at sector 1 points to the
// index file, which lists three copies of each file's header (0xC0EDBABE: size, then extents as
// sector/count pairs). Directories are files of (3-byte file id, type, name length, name) entries,
// with the root at file id 2. Textures (.fxb) are Glide's Gu3dfInfo as saved from memory: a 24-byte
// header (width, height, small/large LOD, aspect, format), a 1024-byte NCC table or palette, data
// pointer and size, a 20-byte record, then the texels (largest mip first). The record's first word is the
// texture's chroma key, as the game sets it for the 3dfx chip (SetTexSource): 0xFFFFFFFF for none,
// otherwise the colour (0xRRGGBB) left out, or for the palette formats its palette index. The arcade
// textures keep exactly the transparency the arcade gives them: their alpha, and their key colour.
//
// The mapping (assets/arcade_textures.json) gives, for each N64 texture hash, the arcade texture and
// the crop of it that matches, the flip (many N64 textures are stored upside down), and the N64
// texture's size (RT64's, and the part of it holding loaded texels). Each replacement is the crop scaled
// to the loaded part of the N64 texture, at an integer multiple of its size.

#include "cspeed_arcade.h"
#include "cspeed_config.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <thread>
#include <unordered_map>
#include <vector>

#include <json/json.hpp>
#include <libchdr/chd.h>

#include "recompui/recompui.h"
#include "recompui/config.h"
#include "recompui/renderer.h"
#include "util/file.h"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../lib/rt64/src/contrib/stb/stb_image_write.h"

using json = nlohmann::json;

namespace {

constexpr int MAX_SCALE = 8;

struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
    uint8_t *at(int x, int y) { return &rgba[(size_t(y) * width + x) * 4]; }
    const uint8_t *at(int x, int y) const { return &rgba[(size_t(y) * width + x) * 4]; }
};

// Reads sectors from the CHD hard drive image.
class HardDrive {
public:
    ~HardDrive() {
        if (chd) chd_close(chd);
        if (file) fclose(file);
    }

    std::string open(const std::filesystem::path &path) {
#ifdef _WIN32
        file = _wfopen(path.c_str(), L"rb");
#else
        file = fopen(path.c_str(), "rb");
#endif
        if (file == nullptr) return "Could not open the file.";
        chd_error err = chd_open_file(file, CHD_OPEN_READ, nullptr, &chd);
        if (err != CHDERR_NONE) return std::string("Not a valid CHD file: ") + chd_error_string(err);
        hunk_bytes = chd_get_header(chd)->hunkbytes;
        hunk.resize(hunk_bytes);
        return "";
    }

    bool read(uint64_t offset, uint8_t *dst, size_t length) {
        while (length > 0) {
            uint32_t index = uint32_t(offset / hunk_bytes);
            if (int64_t(index) != current_hunk) {
                if (chd_read(chd, index, hunk.data()) != CHDERR_NONE) return false;
                current_hunk = index;
            }
            size_t start = size_t(offset % hunk_bytes);
            size_t count = std::min<size_t>(length, hunk_bytes - start);
            memcpy(dst, hunk.data() + start, count);
            dst += count; offset += count; length -= count;
        }
        return true;
    }

    bool sectors(uint32_t lba, uint32_t count, std::vector<uint8_t> &out) {
        size_t old = out.size();
        out.resize(old + size_t(count) * 512);
        return read(uint64_t(lba) * 512, out.data() + old, size_t(count) * 512);
    }

private:
    FILE *file = nullptr;
    chd_file *chd = nullptr;
    uint32_t hunk_bytes = 0;
    std::vector<uint8_t> hunk;
    int64_t current_hunk = -1;
};

uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }

// The arcade drive's file system.
class ArcadeFileSystem {
public:
    explicit ArcadeFileSystem(HardDrive &drive) : drive(drive) {}

    std::string open() {
        std::vector<uint8_t> home;
        if (!drive.sectors(1, 1, home) || le32(&home[0]) != 0xFEEDF00D) return "This is not the California Speed arcade hard drive.";
        std::vector<uint8_t> index;
        if (!read_file_at(le32(&home[18 * 4]), index)) return "The drive's file index could not be read.";
        for (size_t i = 0; i + 12 <= index.size(); i += 12) {
            ids.push_back({ le32(&index[i]), le32(&index[i + 4]), le32(&index[i + 8]) });
        }
        return "";
    }

    // Reads a file by path (e.g. "monterey/3df/bank3.fxb").
    bool read(const std::string &path, std::vector<uint8_t> &out) {
        uint32_t id = ROOT_ID;
        size_t start = 0;
        while (start < path.size()) {
            size_t end = path.find('/', start);
            std::string name = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
            const auto &entries = directory(id);
            auto it = entries.find(name);
            if (it == entries.end()) return false;
            id = it->second;
            if (end == std::string::npos) break;
            start = end + 1;
        }
        return read_file(id, out);
    }

private:
    static constexpr uint32_t ROOT_ID = 2;
    struct Copies { uint32_t lba[3]; };

    HardDrive &drive;
    std::vector<Copies> ids;
    std::unordered_map<uint32_t, std::map<std::string, uint32_t>> directories;

    bool read_file_at(uint32_t header_lba, std::vector<uint8_t> &out) {
        std::vector<uint8_t> header;
        if (header_lba == 0 || !drive.sectors(header_lba, 1, header)) return false;
        uint32_t magic = le32(&header[0]);
        if (magic != 0xC0EDBABE && magic != 0xF00DFACE) return false;
        uint32_t size = le32(&header[4]);
        out.clear();
        for (size_t i = 24; i + 8 <= 512; i += 8) {
            uint32_t lba = le32(&header[i]), count = le32(&header[i + 4]);
            if (count == 0) break;
            if (!drive.sectors(lba, count, out)) return false;
        }
        if (out.size() < size) return false;
        out.resize(size);
        return true;
    }

    bool read_file(uint32_t id, std::vector<uint8_t> &out) {
        if (id >= ids.size()) return false;
        for (uint32_t lba : ids[id].lba) {
            if (read_file_at(lba, out)) return true;
        }
        return false;
    }

    const std::map<std::string, uint32_t> &directory(uint32_t id) {
        auto it = directories.find(id);
        if (it != directories.end()) return it->second;
        auto &entries = directories[id];
        std::vector<uint8_t> data;
        if (read_file(id, data)) {
            size_t p = 0;
            while (p + 5 <= data.size()) {
                uint32_t file_id = data[p] | data[p + 1] << 8 | data[p + 2] << 16;
                uint8_t length = data[p + 4];
                if (length == 0 || p + 5 + length > data.size()) break;
                std::string name(reinterpret_cast<const char *>(&data[p + 5]), strnlen(reinterpret_cast<const char *>(&data[p + 5]), length));
                entries[name] = file_id;
                p += 5 + length;
            }
        }
        return entries;
    }
};

constexpr size_t SPECK_TEXELS = 4;  // clusters this small, apart from the rest of a keyed texture, are specks

// Decodes a .fxb texture (3dfx Glide texture formats).
bool decode_fxb(const std::vector<uint8_t> &d, Image &out) {
    constexpr size_t TEXELS = 0x434;
    if (d.size() < TEXELS) return false;
    uint32_t w = le32(&d[0]), h = le32(&d[4]), fmt = le32(&d[20]);
    if (w == 0 || h == 0 || w > 1024 || h > 1024) return false;
    size_t bytes = size_t(w) * h * (fmt >= 8 ? 2 : 1);
    std::vector<uint8_t> px(d.begin() + TEXELS, d.begin() + std::min(d.size(), TEXELS + bytes));
    px.resize(bytes, 0);
    out.width = int(w); out.height = int(h);
    out.rgba.assign(size_t(w) * h * 4, 0);

    // NCC table (YIQ formats): 16 Y values, then 4 I and 4 Q (r, g, b) as 9-bit signed values.
    const uint8_t *y = &d[0x18];
    auto iq = [&](int k, int c) { int v = int16_t(d[0x28 + (k * 3 + c) * 2] | d[0x29 + (k * 3 + c) * 2] << 8) & 0x1FF; return v & 0x100 ? v - 0x200 : v; };
    auto palette = [&](int i) { return le32(&d[0x18 + i * 4]); };
    auto clamp8 = [](int v) { return uint8_t(std::clamp(v, 0, 255)); };

    for (size_t k = 0; k < size_t(w) * h; k++) {
        uint8_t *o = &out.rgba[k * 4];
        uint32_t v8 = px[k < bytes ? k : 0], v16 = fmt >= 8 ? (px[k * 2] | px[k * 2 + 1] << 8) : 0;
        switch (fmt) {
        case 1: case 9: { // YIQ_422, AYIQ_8422
            uint32_t t = fmt == 1 ? px[k] : (v16 & 0xFF);
            for (int c = 0; c < 3; c++) o[c] = clamp8(y[t >> 4] + iq((t >> 2) & 3, c) + iq(4 + (t & 3), c));
            o[3] = fmt == 1 ? 255 : uint8_t(v16 >> 8);
            break;
        }
        case 10: o[0] = uint8_t(((v16 >> 11) & 31) * 255 / 31); o[1] = uint8_t(((v16 >> 5) & 63) * 255 / 63); o[2] = uint8_t((v16 & 31) * 255 / 31); o[3] = 255; break;
        case 11: o[0] = uint8_t(((v16 >> 10) & 31) * 255 / 31); o[1] = uint8_t(((v16 >> 5) & 31) * 255 / 31); o[2] = uint8_t((v16 & 31) * 255 / 31); o[3] = v16 & 0x8000 ? 255 : 0; break;
        case 12: o[0] = uint8_t(((v16 >> 8) & 15) * 17); o[1] = uint8_t(((v16 >> 4) & 15) * 17); o[2] = uint8_t((v16 & 15) * 17); o[3] = uint8_t((v16 >> 12) * 17); break;
        case 5: case 14: { // P_8, AP_88
            uint32_t c = palette(fmt == 5 ? px[k] : (v16 & 0xFF));
            o[0] = uint8_t(c >> 16); o[1] = uint8_t(c >> 8); o[2] = uint8_t(c); o[3] = fmt == 5 ? 255 : uint8_t(v16 >> 8);
            break;
        }
        case 13: o[0] = o[1] = o[2] = uint8_t(v16); o[3] = uint8_t(v16 >> 8); break;
        case 3: o[0] = o[1] = o[2] = uint8_t(v8); o[3] = 255; break;
        case 2: o[0] = o[1] = o[2] = 255; o[3] = uint8_t(v8); break;
        case 4: o[0] = o[1] = o[2] = uint8_t((v8 & 15) * 17); o[3] = uint8_t((v8 >> 4) * 17); break;
        case 0: o[0] = uint8_t((v8 >> 5) * 255 / 7); o[1] = uint8_t(((v8 >> 2) & 7) * 255 / 7); o[2] = uint8_t((v8 & 3) * 85); o[3] = 255; break;
        case 8: o[0] = uint8_t(((v16 >> 5) & 7) * 255 / 7); o[1] = uint8_t(((v16 >> 2) & 7) * 255 / 7); o[2] = uint8_t((v16 & 3) * 85); o[3] = uint8_t(v16 >> 8); break;
        default: return false;
        }
    }
    uint32_t key = le32(&d[0x420]);
    if (key != 0xFFFFFFFF) {
        if (fmt == 5 || fmt == 14) key = palette(key & 0xFF);
        uint8_t kr = uint8_t(key >> 16), kg = uint8_t(key >> 8), kb = uint8_t(key);
        for (size_t k = 0; k < out.rgba.size(); k += 4) {
            uint8_t *o = &out.rgba[k];
            if (o[0] == kr && o[1] == kg && o[2] == kb) o[3] = 0;
        }
        // Keying leaves specks of the old background in some textures (tree lines): tiny clusters of texels
        // apart from the art. At the arcade's resolution they hardly showed; scaled up they float next to it.
        std::vector<int> seen(size_t(out.width) * out.height, 0), cluster, stack;
        for (int start = 0; start < out.width * out.height; start++) {
            if (seen[start] || out.rgba[size_t(start) * 4 + 3] == 0) continue;
            cluster.clear(); stack.assign(1, start); seen[start] = 1;
            while (!stack.empty()) {
                int k = stack.back(); stack.pop_back(); cluster.push_back(k);
                int i = k % out.width, j = k / out.width;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        int x = i + dx, y = j + dy;
                        if (x < 0 || y < 0 || x >= out.width || y >= out.height) continue;
                        int n = y * out.width + x;
                        if (!seen[n] && out.rgba[size_t(n) * 4 + 3] != 0) { seen[n] = 1; stack.push_back(n); }
                    }
                }
            }
            if (cluster.size() <= SPECK_TEXELS) {
                for (int k : cluster) out.rgba[size_t(k) * 4 + 3] = 0;
            }
        }
    }
    return true;
}

Image crop(const Image &src, int x, int y, int w, int h, bool flip_x, bool flip_y) {
    Image out;
    out.width = w; out.height = h; out.rgba.resize(size_t(w) * h * 4);
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            int sx = std::clamp(x + (flip_x ? w - 1 - i : i), 0, src.width - 1);
            int sy = std::clamp(y + (flip_y ? h - 1 - j : j), 0, src.height - 1);
            memcpy(out.at(i, j), src.at(sx, sy), 4);
        }
    }
    return out;
}

// Resamples one axis: box filter when shrinking, linear interpolation when enlarging.
Image resample_axis(const Image &src, int new_size, bool horizontal) {
    int old_size = horizontal ? src.width : src.height;
    Image out;
    out.width = horizontal ? new_size : src.width;
    out.height = horizontal ? src.height : new_size;
    out.rgba.resize(size_t(out.width) * out.height * 4);
    int lines = horizontal ? src.height : src.width;
    double ratio = double(old_size) / new_size;
    for (int l = 0; l < lines; l++) {
        for (int n = 0; n < new_size; n++) {
            double acc[4] = {}, total = 0;
            auto add = [&](int s, double weight) {
                s = std::clamp(s, 0, old_size - 1);
                const uint8_t *p = horizontal ? src.at(s, l) : src.at(l, s);
                // Weight colour by alpha so transparent texels do not darken the edges.
                double a = p[3] * weight;
                for (int c = 0; c < 3; c++) acc[c] += p[c] * a;
                acc[3] += a; total += weight;
            };
            if (ratio > 1.0) {
                double begin = n * ratio, end = begin + ratio;
                for (int s = int(begin); s < int(std::ceil(end)); s++) {
                    add(s, std::min(end, s + 1.0) - std::max(begin, double(s)));
                }
            }
            else {
                double pos = (n + 0.5) * ratio - 0.5;
                int s = int(std::floor(pos));
                double f = pos - s;
                add(s, 1 - f); add(s + 1, f);
            }
            uint8_t *o = horizontal ? out.at(n, l) : out.at(l, n);
            for (int c = 0; c < 3; c++) o[c] = acc[3] > 0 ? uint8_t(std::lround(std::clamp(acc[c] / acc[3], 0.0, 255.0))) : 0;
            o[3] = uint8_t(std::lround(std::clamp(acc[3] / total, 0.0, 255.0)));
        }
    }
    return out;
}

Image resize(const Image &src, int w, int h) {
    Image tmp = src.width == w ? src : resample_axis(src, w, true);
    return tmp.height == h ? tmp : resample_axis(tmp, h, false);
}

// Places the replacement content in the loaded part of the N64 texture's area and clamps beyond it.
Image place(const Image &content, int width, int height) {
    Image out;
    out.width = width; out.height = height; out.rgba.resize(size_t(width) * height * 4);
    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            memcpy(out.at(i, j), content.at(std::min(i, content.width - 1), std::min(j, content.height - 1)), 4);
        }
    }
    return out;
}

std::filesystem::path mapping_path() {
    return recompui::file::get_asset_path("arcade_textures.json");
}

std::string read_text(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Identifies the mapping a pack was built from, so a changed mapping rebuilds the pack. PACK_FORMAT changes
// when the way packs are built does (2: the arcade's own chroma keys, cutouts; 3: specks).
constexpr const char *PACK_FORMAT = "3";

std::string mapping_id() {
    std::string text = read_text(mapping_path()) + PACK_FORMAT;
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : text) { h ^= c; h *= 1099511628211ULL; }
    char buf[32];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return buf;
}

std::atomic<bool> generating = false;
std::atomic<bool> hidden = false;

} // namespace

std::filesystem::path cspeed::arcade::pack_directory() {
    return recompui::file::get_app_folder_path() / "arcade_textures";
}

bool cspeed::arcade::pack_available() {
    std::string stamp = read_text(pack_directory() / "mapping_id.txt");
    return !stamp.empty() && stamp == mapping_id();
}

std::string cspeed::arcade::generate_pack(const std::filesystem::path &chd_path, std::function<void(float)> progress) {
    json mapping;
    try {
        mapping = json::parse(read_text(mapping_path()));
    }
    catch (const std::exception &) {
        return "The texture mapping (assets/arcade_textures.json) is missing or invalid.";
    }

    HardDrive drive;
    if (std::string err = drive.open(chd_path); !err.empty()) return err;
    ArcadeFileSystem fs(drive);
    if (std::string err = fs.open(); !err.empty()) return err;

    std::filesystem::path dir = pack_directory();
    std::filesystem::path tmp = dir.string() + ".tmp";
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
    std::filesystem::create_directories(tmp, ec);
    if (ec) return "Could not create " + tmp.string();

    json database;
    database["configuration"] = { { "configurationVersion", 3 }, { "autoPath", "rt64" }, { "defaultOperation", "stream" }, { "defaultShift", "half" }, { "hashVersion", 5 } };
    database["textures"] = json::array();

    std::unordered_map<std::string, Image> decoded;
    const json &textures = mapping["textures"];
    size_t written = 0, missing = 0;
    for (size_t n = 0; n < textures.size(); n++) {
        const json &t = textures[n];
        std::string file = t["file"];
        auto it = decoded.find(file);
        if (it == decoded.end()) {
            std::vector<uint8_t> data;
            Image image;
            if (!fs.read(file, data) || !decode_fxb(data, image)) image = Image{};
            it = decoded.emplace(file, std::move(image)).first;
        }
        const Image &source = it->second;
        if (source.width == 0) { missing++; continue; }

        int x = t["rect"][0], y = t["rect"][1], w = t["rect"][2], h = t["rect"][3];
        std::string flip = t["flip"];
        int tile_w = t["tile"][0], tile_h = t["tile"][1], loaded_w = t["loaded"][0], loaded_h = t["loaded"][1];
        // Keep the arcade texture's detail: scale the N64 size up by a whole factor that covers the crop.
        int scale = std::clamp(int(std::ceil(std::max(double(w) / loaded_w, double(h) / loaded_h))), 1, MAX_SCALE);
        Image cropped = crop(source, x, y, w, h, flip.find('h') != std::string::npos, flip.find('v') != std::string::npos);
        // Transparency that is a cutout (keyed or 1-bit alpha) must show even where the N64 texture was
        // opaque and the game draws it without alpha (RT64 discards those texels, see ReplacementTexture::cutout).
        bool holes = false, partial = false;
        for (size_t k = 3; k < cropped.rgba.size(); k += 4) {
            holes = holes || cropped.rgba[k] == 0;
            partial = partial || (cropped.rgba[k] != 0 && cropped.rgba[k] != 255);
        }
        Image content = resize(cropped, loaded_w * scale, loaded_h * scale);
        Image result = place(content, tile_w * scale, tile_h * scale);

        std::string hash = t["hash"];
        std::filesystem::path out = tmp / (hash + ".png");
        if (!stbi_write_png(out.string().c_str(), result.width, result.height, 4, result.rgba.data(), result.width * 4)) {
            return "Could not write " + out.string();
        }
        json entry = { { "path", hash }, { "hashes", { { "rt64", hash } } } };
        if (holes && !partial) entry["cutout"] = true;
        database["textures"].push_back(entry);
        written++;
        if (progress) progress(float(n + 1) / textures.size());
    }
    if (written == 0) return "None of the textures were found on this drive.";

    std::ofstream(tmp / "rt64.json") << database.dump(1);
    std::ofstream(tmp / "mapping_id.txt") << mapping_id();
    std::filesystem::remove_all(dir, ec);
    std::filesystem::rename(tmp, dir, ec);
    if (ec) return "Could not move the pack into " + dir.string();
    printf("Arcade textures: %zu written, %zu not found on the drive\n", written, missing);
    return "";
}

void cspeed::arcade::apply_pack_setting() {
    bool use = cspeed::arcade_textures_enabled() && !hidden && pack_available();
    recompui::renderer::set_base_texture_pack(use ? pack_directory() : std::filesystem::path());
}

void cspeed::arcade::set_hidden(bool hide) {
    hidden = hide;
    apply_pack_setting();
}

void cspeed::arcade::startup() {
    // Rebuild the pack when the mapping changed since it was generated (e.g. after an update).
    std::filesystem::path path = cspeed::arcade_data_path();
    // Debug aid: CSPEED_ARCADE_CHD=<path> rebuilds the pack from that image at startup.
    const char *debug_chd = getenv("CSPEED_ARCADE_CHD");
    bool force = debug_chd != nullptr && debug_chd[0] != '\0';
    if (force) path = std::filesystem::u8path(debug_chd);
    std::error_code ec;
    if (path.empty() || (pack_available() && !force) || !std::filesystem::exists(path, ec)) {
        apply_pack_setting();
        return;
    }
    generating = true;
    std::thread([path]() {
        std::string err = generate_pack(path);
        generating = false;
        if (!err.empty()) printf("Arcade textures: could not rebuild the pack: %s\n", err.c_str());
        apply_pack_setting();
    }).detach();
}

void cspeed::arcade::select_arcade_data() {
    if (generating) {
        recompui::message_box("The arcade textures are still being prepared.");
        return;
    }
    recompui::file::open_file_dialog([](bool success, const std::filesystem::path &path) {
        if (!success) return;
        cspeed::set_arcade_data_path(path);
        generating = true;
        std::thread([path]() {
            std::string err = generate_pack(path);
            generating = false;
            if (err.empty()) {
                apply_pack_setting();
                recompui::message_box("The arcade textures are ready. They can be turned off with Arcade Textures in the Graphics settings.");
            }
            else {
                recompui::message_box(("The arcade textures could not be prepared.\n" + err).c_str());
            }
        }).detach();
    });
}
