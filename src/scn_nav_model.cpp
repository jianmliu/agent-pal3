#include "pal3/portable/scn_nav_model.h"
#include "pal3/portable/sce_vm.h"
#include "pal4/portable/dds_texture.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <queue>
#include <stdexcept>
#include <unordered_map>

namespace pal3::portable {
namespace {

void WriteU8(std::vector<std::uint8_t>& out, std::uint8_t v) { out.push_back(v); }

void WriteU16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8u) & 0xFFu));
}

void WriteI16(std::vector<std::uint8_t>& out, std::int16_t v) {
    WriteU16(out, static_cast<std::uint16_t>(v));
}

void WriteU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (i * 8u)) & 0xFFu));
    }
}

void WriteI32(std::vector<std::uint8_t>& out, std::int32_t v) {
    WriteU32(out, static_cast<std::uint32_t>(v));
}

void WriteF32(std::vector<std::uint8_t>& out, float v) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(float));
    WriteU32(out, bits);
}

void WriteU32At(std::vector<std::uint8_t>& out, std::size_t off, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) {
        out[off + i] = static_cast<std::uint8_t>((v >> (i * 8u)) & 0xFFu);
    }
}

void WriteFixedString(std::vector<std::uint8_t>& out, std::string_view str, std::size_t fixed_len) {
    const std::size_t n = std::min(str.size(), fixed_len > 0 ? fixed_len - 1 : 0);
    for (std::size_t i = 0; i < n; ++i) {
        out.push_back(static_cast<std::uint8_t>(str[i]));
    }
    for (std::size_t i = n; i < fixed_len; ++i) {
        out.push_back(0u);
    }
}

struct ByteReader {
    std::span<const std::uint8_t> buf;
    std::size_t pos{0};

    void Seek(std::size_t p) {
        if (p > buf.size()) throw std::runtime_error("ByteReader seek past end");
        pos = p;
    }

    std::uint8_t ReadU8() {
        if (pos + 1 > buf.size()) throw std::runtime_error("ByteReader EOF on u8");
        return buf[pos++];
    }

    std::uint16_t ReadU16() {
        if (pos + 2 > buf.size()) throw std::runtime_error("ByteReader EOF on u16");
        const auto v = static_cast<std::uint16_t>(buf[pos]) |
                       (static_cast<std::uint16_t>(buf[pos + 1]) << 8u);
        pos += 2;
        return v;
    }

    std::int16_t ReadI16() { return static_cast<std::int16_t>(ReadU16()); }

    std::uint32_t ReadU32() {
        if (pos + 4 > buf.size()) throw std::runtime_error("ByteReader EOF on u32");
        const auto v = static_cast<std::uint32_t>(buf[pos]) |
                       (static_cast<std::uint32_t>(buf[pos + 1]) << 8u) |
                       (static_cast<std::uint32_t>(buf[pos + 2]) << 16u) |
                       (static_cast<std::uint32_t>(buf[pos + 3]) << 24u);
        pos += 4;
        return v;
    }

    std::int32_t ReadI32() { return static_cast<std::int32_t>(ReadU32()); }

    float ReadF32() {
        const std::uint32_t bits = ReadU32();
        float v = 0.0f;
        std::memcpy(&v, &bits, sizeof(float));
        return v;
    }

    std::string ReadFixedString(std::size_t fixed_len) {
        if (pos + fixed_len > buf.size()) throw std::runtime_error("ByteReader EOF on FixedString");
        const auto* begin = reinterpret_cast<const char*>(buf.data() + pos);
        std::size_t len = 0;
        while (len < fixed_len && begin[len] != '\0') ++len;
        pos += fixed_len;
        return ConvertGbkOrUtf8ToUtf8(std::string_view(begin, len));
    }
};

}  // namespace

// ============================================================================
// 1. .scn Serialization & Parsing (exact offsets matching OpenPAL3 `scn_loader.rs`)
// ============================================================================

std::vector<std::uint8_t> SerializeScnFile(const ScnFile& scn) {
    std::vector<std::uint8_t> out;
    // Exact 150-byte header matching OpenPAL3 `scn_load_from_file`:
    // 0..4 "SCN\0", 4..6 version(1), 6..8 role_num, 8..12 role_offset(150),
    // 12..14 node_num, 14..18 node_offset, 18..50 cpk_name, 50..82 scn_name,
    // 82..114 scn_base_name, 114..118 global_scene_index, 118..122 is_night,
    // 122..126 skybox_id, 126..150 reserved.
    out.insert(out.end(), {'S', 'C', 'N', '\0'});
    WriteU16(out, 1u);
    WriteU16(out, static_cast<std::uint16_t>(scn.roles.size()));
    const std::size_t role_offset_pos = out.size();
    WriteU32(out, 0u);
    WriteU16(out, static_cast<std::uint16_t>(scn.nodes.size()));
    const std::size_t node_offset_pos = out.size();
    WriteU32(out, 0u);

    WriteFixedString(out, scn.cpk_name, 32);
    WriteFixedString(out, scn.scn_name, 32);
    WriteFixedString(out, scn.scn_base_name, 32);
    WriteU32(out, 0u);  // global_scene_index
    WriteU32(out, scn.is_night);
    WriteU32(out, scn.skybox_id);
    out.resize(150, 0u);

    // Roles table (each 456 bytes matching OpenPAL3 `read_scn_role`)
    const std::uint32_t role_offset = static_cast<std::uint32_t>(out.size());
    WriteU32At(out, role_offset_pos, role_offset);
    for (const auto& r : scn.roles) {
        const std::size_t r_start = out.size();
        out.resize(r_start + 456, 0u);
        out[r_start + 0] = r.index;
        out[r_start + 1] = 1u;
        const std::size_t nlen = std::min<std::size_t>(r.name.size(), 63);
        std::memcpy(out.data() + r_start + 2, r.name.data(), nlen);

        auto write_f32_at = [&](std::size_t off, float val) {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &val, 4);
            WriteU32At(out, r_start + off, bits);
        };
        write_f32_at(0x44, r.facing_deg);
        WriteU32At(out, r_start + 0x48, r.nav_layer);
        write_f32_at(0x4C, r.position_x);
        write_f32_at(0x50, r.position_z);
        WriteU32At(out, r_start + 0x5C, r.sce_proc_id);

        const std::size_t alen = std::min<std::size_t>(r.action_name.size(), 15);
        std::memcpy(out.data() + r_start + 0x64, r.action_name.data(), alen);
        WriteU32At(out, r_start + 0x80, r.path_mode);

        const std::uint32_t wp_cnt =
            static_cast<std::uint32_t>(std::min<std::size_t>(r.patrol_path.size(), 16));
        WriteU32At(out, r_start + 0x84, wp_cnt);
        for (std::size_t w = 0; w < wp_cnt; ++w) {
            write_f32_at(0x88 + w * 12 + 0, r.patrol_path[w][0]);
            write_f32_at(0x88 + w * 12 + 4, r.patrol_path[w][1]);
            write_f32_at(0x88 + w * 12 + 8, r.patrol_path[w][2]);
        }
        WriteU32At(out, r_start + 0x150, static_cast<std::uint32_t>(std::max(0.0f, r.move_speed)));
    }

    // Nodes table (each 620 bytes matching OpenPAL3 `read_scn_node`)
    const std::uint32_t node_offset = static_cast<std::uint32_t>(out.size());
    WriteU32At(out, node_offset_pos, node_offset);
    for (const auto& n : scn.nodes) {
        const std::size_t n_start = out.size();
        out.resize(n_start + 620, 0u);
        out[n_start + 0] = n.index;
        out[n_start + 1] = 0u;
        out[n_start + 2] = n.active;
        out[n_start + 3] = n.switch_state;
        const std::size_t nlen = std::min<std::size_t>(n.name.size(), 31);
        std::memcpy(out.data() + n_start + 4, n.name.data(), nlen);
        out[n_start + 0x26] = static_cast<std::uint8_t>(n.node_type & 0xFFu);
        out[n_start + 0x27] = static_cast<std::uint8_t>((n.node_type >> 8u) & 0xFFu);

        auto write_f32_at = [&](std::size_t off, float val) {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &val, 4);
            WriteU32At(out, n_start + off, bits);
        };
        write_f32_at(0x28, n.position[0]);
        write_f32_at(0x2C, n.position[1]);
        write_f32_at(0x30, n.position[2]);
        write_f32_at(0x34, n.rotation_deg);

        WriteU32At(out, n_start + 0x38, static_cast<std::uint32_t>(n.nav_trigger_coord_min[0]));
        WriteU32At(out, n_start + 0x3C, static_cast<std::uint32_t>(n.nav_trigger_coord_min[1]));
        WriteU32At(out, n_start + 0x40, static_cast<std::uint32_t>(n.nav_trigger_coord_max[0]));
        WriteU32At(out, n_start + 0x44, static_cast<std::uint32_t>(n.nav_trigger_coord_max[1]));

        out[n_start + 0x48] = static_cast<std::uint8_t>(n.node_type & 0xFFu);
        out[n_start + 0x49] = static_cast<std::uint8_t>((n.node_type >> 8u) & 0xFFu);
        out[n_start + 0x4A] = static_cast<std::uint8_t>(n.nav_layer & 0xFFu);
        out[n_start + 0x4B] = static_cast<std::uint8_t>((n.nav_layer >> 8u) & 0xFFu);
        WriteU32At(out, n_start + 0x4C, static_cast<std::uint32_t>(n.ladder_nav_coord1[0]));
        WriteU32At(out, n_start + 0x50, static_cast<std::uint32_t>(n.ladder_nav_coord1[1]));
        WriteU32At(out, n_start + 0x54, static_cast<std::uint32_t>(n.ladder_nav_coord2[0]));
        WriteU32At(out, n_start + 0x58, static_cast<std::uint32_t>(n.ladder_nav_coord2[1]));
        WriteU32At(out, n_start + 0x5C, static_cast<std::uint32_t>(n.ladder_switch_layer));
        WriteU32At(out, n_start + 0x80, n.sce_proc_id);

        write_f32_at(0x16C, n.aabb_trigger_coord1[0]);
        write_f32_at(0x170, n.aabb_trigger_coord1[1]);
        write_f32_at(0x174, n.aabb_trigger_coord1[2]);
        write_f32_at(0x178, n.aabb_trigger_coord2[0]);
        write_f32_at(0x17C, n.aabb_trigger_coord2[1]);
        write_f32_at(0x180, n.aabb_trigger_coord2[2]);
    }

    return out;
}

ScnFile ParseScnFile(std::span<const std::uint8_t> bytes) {
    ByteReader r{bytes, 0};
    const std::string magic = r.ReadFixedString(4);
    if (magic != "SCN") {
        throw std::runtime_error("invalid PAL3 .scn magic (expected SCN\\0)");
    }
    const std::uint16_t version = r.ReadU16();
    if (version != 1u) {
        throw std::runtime_error("unsupported PAL3 .scn version");
    }
    const std::uint16_t role_num = r.ReadU16();
    const std::uint32_t role_offset = r.ReadU32();
    const std::uint16_t node_num = r.ReadU16();
    const std::uint32_t node_offset = r.ReadU32();

    ScnFile scn;
    scn.cpk_name = r.ReadFixedString(32);
    scn.scn_name = r.ReadFixedString(32);
    scn.scn_base_name = r.ReadFixedString(32);
    (void)r.ReadU32();  // global_scene_index
    scn.is_night = r.ReadU32();
    scn.skybox_id = r.ReadU32();

    scn.roles.reserve(role_num);
    for (std::uint16_t i = 0; i < role_num; ++i) {
        const std::size_t base = static_cast<std::size_t>(role_offset) + static_cast<std::size_t>(i) * 456u;
        if (base + 456u > bytes.size()) break;
        ScnRole role;
        r.Seek(base + 0);
        role.index = r.ReadU8();
        (void)r.ReadU8();
        role.name = r.ReadFixedString(64);
        (void)r.ReadU16();
        role.facing_deg = r.ReadF32();
        role.nav_layer = r.ReadU32();
        role.position_x = r.ReadF32();
        role.position_z = r.ReadF32();
        (void)r.ReadU32();  // flag_0x54
        (void)r.ReadU32();  // dw58
        role.sce_proc_id = r.ReadU32();
        (void)r.ReadU32();  // dw60
        role.action_name = r.ReadFixedString(16);
        r.Seek(base + 0x80);
        role.path_mode = r.ReadU32();
        const std::uint32_t wp_cnt = std::min<std::uint32_t>(r.ReadU32(), 16u);
        for (std::uint32_t w = 0; w < wp_cnt; ++w) {
            role.patrol_path.push_back({r.ReadF32(), r.ReadF32(), r.ReadF32()});
        }
        r.Seek(base + 0x150);
        const std::uint32_t raw_speed = r.ReadU32();
        role.move_speed = (raw_speed > 0u && raw_speed <= 500u)
                              ? static_cast<float>(raw_speed)
                              : 215.0f;
        scn.roles.push_back(std::move(role));
    }

    // PAL3 node records are 620 bytes (PAL3A is 604 bytes)
    const std::size_t node_stride =
        (node_num > 0 && node_offset + static_cast<std::size_t>(node_num) * 620u > bytes.size() &&
         node_offset + static_cast<std::size_t>(node_num) * 604u <= bytes.size())
            ? 604u
            : 620u;

    scn.nodes.reserve(node_num);
    for (std::uint16_t i = 0; i < node_num; ++i) {
        const std::size_t base = static_cast<std::size_t>(node_offset) + static_cast<std::size_t>(i) * node_stride;
        if (base + 0x184u > bytes.size()) break;
        ScnNode node;
        r.Seek(base + 0);
        node.index = static_cast<std::uint8_t>(r.ReadU16() & 0xFFu);
        const std::uint16_t w2 = r.ReadU16();
        node.active = (w2 == 0u) ? 1u : static_cast<std::uint8_t>(w2 & 0xFFu);
        node.times = 255u;
        node.switch_state = static_cast<std::uint8_t>((w2 >> 8u) & 0xFFu);
        node.name = r.ReadFixedString(32);
        (void)r.ReadU16();  // w24
        const std::uint16_t w26 = r.ReadU16();
        node.position[0] = r.ReadF32();
        node.position[1] = r.ReadF32();
        node.position[2] = r.ReadF32();
        node.rotation_deg = r.ReadF32();
        node.nav_trigger_coord_min[0] = r.ReadI32();
        node.nav_trigger_coord_min[1] = r.ReadI32();
        node.nav_trigger_coord_max[0] = r.ReadI32();
        node.nav_trigger_coord_max[1] = r.ReadI32();
        const std::uint16_t w48 = r.ReadU16();
        node.node_type = (w48 != 0u) ? w48 : w26;
        node.nav_layer = r.ReadU16();
        node.ladder_nav_coord1[0] = r.ReadI32();
        node.ladder_nav_coord1[1] = r.ReadI32();
        node.ladder_nav_coord2[0] = r.ReadI32();
        node.ladder_nav_coord2[1] = r.ReadI32();
        node.ladder_switch_layer = r.ReadI32();
        r.Seek(base + 0x80);
        node.sce_proc_id = r.ReadU32();
        r.Seek(base + 0x16C);
        node.aabb_trigger_coord1[0] = r.ReadF32();
        node.aabb_trigger_coord1[1] = r.ReadF32();
        node.aabb_trigger_coord1[2] = r.ReadF32();
        node.aabb_trigger_coord2[0] = r.ReadF32();
        node.aabb_trigger_coord2[1] = r.ReadF32();
        node.aabb_trigger_coord2[2] = r.ReadF32();
        scn.nodes.push_back(std::move(node));
    }

    return scn;
}

// ============================================================================
// 2. .nav Dual-Layer Grid & A* Pathfinder (`nav_loader.rs`)
// ============================================================================

const NavTilePoint* NavLayer::At(std::int32_t tx, std::int32_t tz) const noexcept {
    if (tx < 0 || tz < 0 || static_cast<std::uint32_t>(tx) >= width ||
        static_cast<std::uint32_t>(tz) >= height) {
        return nullptr;
    }
    const std::size_t idx = static_cast<std::size_t>(tz) * width + static_cast<std::size_t>(tx);
    return (idx < tiles.size()) ? &tiles[idx] : nullptr;
}

NavTilePoint* NavLayer::At(std::int32_t tx, std::int32_t tz) noexcept {
    if (tx < 0 || tz < 0 || static_cast<std::uint32_t>(tx) >= width ||
        static_cast<std::uint32_t>(tz) >= height) {
        return nullptr;
    }
    const std::size_t idx = static_cast<std::size_t>(tz) * width + static_cast<std::size_t>(tx);
    return (idx < tiles.size()) ? &tiles[idx] : nullptr;
}

std::vector<std::uint8_t> SerializeNavFile(const NavFile& nav) {
    if (nav.layers.empty() || nav.layers.size() > 2) {
        throw std::runtime_error("PAL3 .nav requires 1 or 2 navigation layers");
    }
    std::vector<std::uint8_t> out;
    out.insert(out.end(), {'N', 'A', 'V', '\0'});
    WriteU8(out, nav.version);
    WriteU8(out, static_cast<std::uint8_t>(nav.layers.size()));
    const std::size_t off1_pos = out.size();
    WriteU32(out, 0u);  // unknown1_offset (start of NavMap array)
    const std::size_t off2_pos = out.size();
    WriteU32(out, 0u);  // unknown2_offset (start of NavUnknown2 array)

    WriteU32At(out, off1_pos, static_cast<std::uint32_t>(out.size()));
    for (std::size_t li = 0; li < nav.layers.size(); ++li) {
        const auto& layer = nav.layers[li];
        if (nav.version == 2u) {
            for (const auto& portal : layer.portals) {
                WriteI32(out, portal.min_coord[0]);
                WriteI32(out, portal.min_coord[1]);
                WriteI32(out, portal.max_coord[0]);
                WriteI32(out, portal.max_coord[1]);
            }
        }
        WriteF32(out, layer.max_coord[0]);
        WriteF32(out, layer.max_coord[1]);
        WriteF32(out, layer.max_coord[2]);
        WriteF32(out, layer.min_coord[0]);
        WriteF32(out, layer.min_coord[1]);
        WriteF32(out, layer.min_coord[2]);
        WriteU32(out, layer.width);
        WriteU32(out, layer.height);

        const std::size_t expected = static_cast<std::size_t>(layer.width) * layer.height;
        for (std::size_t i = 0; i < expected; ++i) {
            const NavTilePoint pt = (i < layer.tiles.size()) ? layer.tiles[i] : NavTilePoint{};
            WriteF32(out, pt.height);
            WriteU32(out, pt.distance_to_border);
        }
    }

    // Trailing NavUnknown2 section (`count` entries of empty vertex/index lists)
    WriteU32At(out, off2_pos, static_cast<std::uint32_t>(out.size()));
    for (std::size_t li = 0; li < nav.layers.size(); ++li) {
        WriteU16(out, 0u);
        WriteU16(out, 0u);
    }
    return out;
}

NavFile ParseNavFile(std::span<const std::uint8_t> bytes) {
    ByteReader r{bytes, 0};
    const std::string magic = r.ReadFixedString(4);
    if (magic != "NAV") {
        throw std::runtime_error("invalid PAL3 .nav magic (expected NAV\\0)");
    }
    NavFile nav;
    nav.version = r.ReadU8();
    const std::uint8_t count = r.ReadU8();
    const std::uint32_t unknown1_offset = r.ReadU32();
    (void)r.ReadU32();  // unknown2_offset

    if ((nav.version != 1u && nav.version != 2u) || (count != 1u && count != 2u)) {
        throw std::runtime_error("unsupported PAL3 .nav version or layer count");
    }

    auto read_next_layer = [&]() {
        NavLayer layer;
        if (nav.version == 2u) {
            for (auto& portal : layer.portals) {
                portal.min_coord[0] = r.ReadI32();
                portal.min_coord[1] = r.ReadI32();
                portal.max_coord[0] = r.ReadI32();
                portal.max_coord[1] = r.ReadI32();
            }
        }
        layer.max_coord = {r.ReadF32(), r.ReadF32(), r.ReadF32()};
        layer.min_coord = {r.ReadF32(), r.ReadF32(), r.ReadF32()};
        layer.width = r.ReadU32();
        layer.height = r.ReadU32();
        const std::size_t total = static_cast<std::size_t>(layer.width) * layer.height;
        layer.tiles.resize(total);
        for (std::size_t i = 0; i < total; ++i) {
            layer.tiles[i].height = r.ReadF32();
            layer.tiles[i].distance_to_border = r.ReadU32();
        }
        return layer;
    };

    r.Seek(unknown1_offset);
    for (std::uint8_t i = 0; i < count; ++i) {
        nav.layers.push_back(read_next_layer());
    }
    return nav;
}

std::array<std::int32_t, 2> WorldToNavTile(const NavLayer& layer, float world_x, float world_z) {
    const float span_x = std::max(1.0f, layer.max_coord[0] - layer.min_coord[0]);
    const float span_z = std::max(1.0f, layer.max_coord[2] - layer.min_coord[2]);
    const float block_x = span_x / static_cast<float>(layer.width + 1u);
    const float block_z = span_z / static_cast<float>(layer.height + 1u);
    const std::int32_t tx = std::clamp(
        static_cast<std::int32_t>(std::floor((world_x - layer.min_coord[0]) / block_x)),
        0, static_cast<std::int32_t>(layer.width) - 1);
    const std::int32_t tz = std::clamp(
        static_cast<std::int32_t>(std::floor((world_z - layer.min_coord[2]) / block_z)),
        0, static_cast<std::int32_t>(layer.height) - 1);
    return {tx, tz};
}

std::array<float, 3> NavTileToWorld(const NavLayer& layer, std::int32_t tx, std::int32_t tz) {
    const float span_x = std::max(1.0f, layer.max_coord[0] - layer.min_coord[0]);
    const float span_z = std::max(1.0f, layer.max_coord[2] - layer.min_coord[2]);
    const float block_x = span_x / static_cast<float>(layer.width + 1u);
    const float block_z = span_z / static_cast<float>(layer.height + 1u);
    const float wx = layer.min_coord[0] + (static_cast<float>(tx) + 0.5f) * block_x;
    const float wz = layer.min_coord[2] + (static_cast<float>(tz) + 0.5f) * block_z;
    const auto* pt = layer.At(tx, tz);
    const float wy = pt ? pt->height : layer.min_coord[1];
    return {wx, wy, wz};
}

float SampleNavHeight(const NavLayer& layer, float world_x, float world_z) {
    const auto [tx, tz] = WorldToNavTile(layer, world_x, world_z);
    if (const auto* pt = layer.At(tx, tz)) {
        return pt->height;
    }
    return layer.min_coord[1];
}

bool IsNavTileWalkable(const NavLayer& layer, std::int32_t tx, std::int32_t tz) {
    const auto* pt = layer.At(tx, tz);
    return pt != nullptr && pt->distance_to_border > 0u;
}

namespace {

std::optional<std::array<std::int32_t, 2>> SnapToNearestWalkableTile(
    const NavLayer& layer, std::int32_t tx, std::int32_t tz, std::int32_t max_radius = 4) {
    if (IsNavTileWalkable(layer, tx, tz)) {
        return std::array<std::int32_t, 2>{tx, tz};
    }
    for (std::int32_t r = 1; r <= max_radius; ++r) {
        for (std::int32_t dz = -r; dz <= r; ++dz) {
            for (std::int32_t dx = -r; dx <= r; ++dx) {
                if (std::abs(dx) != r && std::abs(dz) != r) continue;
                if (IsNavTileWalkable(layer, tx + dx, tz + dz)) {
                    return std::array<std::int32_t, 2>{tx + dx, tz + dz};
                }
            }
        }
    }
    return std::nullopt;
}

bool HasGridLineOfSight(const NavLayer& layer, std::array<std::int32_t, 2> a,
                        std::array<std::int32_t, 2> b) {
    int x0 = a[0], y0 = a[1];
    const int x1 = b[0], y1 = b[1];
    const int dx = std::abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (true) {
        const auto* pt = layer.At(x0, y0);
        if (!pt || pt->distance_to_border == 0u) return false;
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
    return true;
}

struct SingleLayerPath {
    bool ok{false};
    float cost{0.0f};
    std::vector<std::array<std::int32_t, 2>> tiles;
};

SingleLayerPath RunSingleLayerAStar(const NavLayer& layer,
                                    std::array<std::int32_t, 2> start,
                                    std::array<std::int32_t, 2> goal) {
    const auto snapped_start = SnapToNearestWalkableTile(layer, start[0], start[1], 3);
    const auto snapped_goal = SnapToNearestWalkableTile(layer, goal[0], goal[1], 4);
    if (!snapped_start || !snapped_goal) return {};

    const int w = static_cast<int>(layer.width);
    const int h = static_cast<int>(layer.height);
    const auto pack = [w](int x, int z) { return z * w + x; };

    std::vector<float> g_score(static_cast<std::size_t>(w * h),
                               std::numeric_limits<float>::infinity());
    std::vector<int> parent(static_cast<std::size_t>(w * h), -1);

    struct Node {
        float f;
        int x;
        int z;
        bool operator>(const Node& o) const noexcept { return f > o.f; }
    };
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;

    const int sx = (*snapped_start)[0], sz = (*snapped_start)[1];
    const int gx = (*snapped_goal)[0], gz = (*snapped_goal)[1];
    g_score[static_cast<std::size_t>(pack(sx, sz))] = 0.0f;
    open.push({std::hypot(static_cast<float>(gx - sx), static_cast<float>(gz - sz)), sx, sz});

    constexpr int kDirs[8][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

    bool found = false;
    while (!open.empty()) {
        const auto cur = open.top();
        open.pop();
        if (cur.x == gx && cur.z == gz) {
            found = true;
            break;
        }
        const float cur_g = g_score[static_cast<std::size_t>(pack(cur.x, cur.z))];
        for (const auto& d : kDirs) {
            const int nx = cur.x + d[0];
            const int nz = cur.z + d[1];
            const auto* npt = layer.At(nx, nz);
            if (!npt || npt->distance_to_border == 0u) continue;
            // Prevent diagonal corner cutting across obstacle cells
            if (d[0] != 0 && d[1] != 0) {
                if (!IsNavTileWalkable(layer, cur.x + d[0], cur.z) ||
                    !IsNavTileWalkable(layer, cur.x, cur.z + d[1])) {
                    continue;
                }
            }
            const float step_len = (d[0] != 0 && d[1] != 0) ? 1.41421356f : 1.0f;
            // Weight penalty using `distance_to_border` (1..6) so paths stay clear of walls
            const float clearance_penalty =
                1.0f + 0.5f / static_cast<float>(std::max(1u, npt->distance_to_border));
            const float tentative_g = cur_g + step_len * clearance_penalty;
            const std::size_t n_idx = static_cast<std::size_t>(pack(nx, nz));
            if (tentative_g + 1e-5f < g_score[n_idx]) {
                g_score[n_idx] = tentative_g;
                parent[n_idx] = pack(cur.x, cur.z);
                const float h_val =
                    std::hypot(static_cast<float>(gx - nx), static_cast<float>(gz - nz));
                open.push({tentative_g + h_val, nx, nz});
            }
        }
    }

    if (!found) return {};

    std::vector<std::array<std::int32_t, 2>> raw_tiles;
    for (int cur = pack(gx, gz); cur != -1; cur = parent[static_cast<std::size_t>(cur)]) {
        raw_tiles.push_back({cur % w, cur / w});
    }
    std::reverse(raw_tiles.begin(), raw_tiles.end());

    // String-pulling via Bresenham line-of-sight
    std::vector<std::array<std::int32_t, 2>> smoothed;
    if (!raw_tiles.empty()) {
        smoothed.push_back(raw_tiles.front());
        std::size_t anchor = 0;
        while (anchor + 1 < raw_tiles.size()) {
            std::size_t furthest = anchor + 1;
            for (std::size_t probe = raw_tiles.size() - 1; probe > anchor + 1; --probe) {
                if (HasGridLineOfSight(layer, raw_tiles[anchor], raw_tiles[probe])) {
                    furthest = probe;
                    break;
                }
            }
            smoothed.push_back(raw_tiles[furthest]);
            anchor = furthest;
        }
    }

    return {true, g_score[static_cast<std::size_t>(pack(gx, gz))], std::move(smoothed)};
}

}  // namespace

NavPathResult FindPal3NavPath(const NavFile& nav,
                              std::span<const ScnNode> scn_nodes,
                              std::uint32_t start_layer,
                              float start_x,
                              float start_z,
                              std::uint32_t goal_layer,
                              float goal_x,
                              float goal_z) {
    if (nav.layers.empty()) return {};
    const std::uint32_t s_layer =
        std::min<std::uint32_t>(start_layer, static_cast<std::uint32_t>(nav.layers.size() - 1));
    const std::uint32_t g_layer =
        std::min<std::uint32_t>(goal_layer, static_cast<std::uint32_t>(nav.layers.size() - 1));

    const auto start_tile = WorldToNavTile(nav.layers[s_layer], start_x, start_z);
    const auto goal_tile = WorldToNavTile(nav.layers[g_layer], goal_x, goal_z);

    auto append_waypoints = [&](std::vector<NavWaypoint>& out,
                                std::uint32_t layer_idx,
                                const std::vector<std::array<std::int32_t, 2>>& tiles,
                                bool mark_first_portal) {
        for (std::size_t i = 0; i < tiles.size(); ++i) {
            NavWaypoint wp;
            wp.layer = layer_idx;
            wp.tile = tiles[i];
            wp.world_pos = NavTileToWorld(nav.layers[layer_idx], tiles[i][0], tiles[i][1]);
            wp.via_portal_or_ladder = (i == 0 && mark_first_portal);
            out.push_back(wp);
        }
    };

    // Case 1: Same layer direct path
    if (s_layer == g_layer) {
        const auto res = RunSingleLayerAStar(nav.layers[s_layer], start_tile, goal_tile);
        if (res.ok) {
            NavPathResult path;
            path.reachable = true;
            path.crossed_layers = false;
            path.total_cost = res.cost;
            append_waypoints(path.waypoints, s_layer, res.tiles, false);
            return path;
        }
    }

    // Case 2: Cross-layer path (or same-layer bridge over upper/lower deck) via NavLayerPortal or ScnNode Ladder
    if (nav.layers.size() >= 2) {
        struct CrossPortalCandidate {
            std::array<std::int32_t, 2> entry_tile;
            std::array<std::int32_t, 2> exit_tile;
        };
        std::vector<CrossPortalCandidate> portals;

        // 2a. Collect `NavLayerPortal` rects from `s_layer`
        for (const auto& p : nav.layers[s_layer].portals) {
            if (p.max_coord[0] > p.min_coord[0] && p.max_coord[1] > p.min_coord[1]) {
                const std::int32_t cx = (p.min_coord[0] + p.max_coord[0]) / 2;
                const std::int32_t cz = (p.min_coord[1] + p.max_coord[1]) / 2;
                portals.push_back({{cx, cz}, {cx, cz}});
            }
        }
        // 2b. Collect `ScnNode` ladders / elevators (`node_type == 15 | 40`)
        for (const auto& node : scn_nodes) {
            if (!node.active) continue;
            if (node.node_type == static_cast<std::uint16_t>(ScnNodeType::ladder) ||
                node.node_type == static_cast<std::uint16_t>(ScnNodeType::elevator)) {
                portals.push_back({node.ladder_nav_coord1, node.ladder_nav_coord2});
                portals.push_back({node.ladder_nav_coord2, node.ladder_nav_coord1});
            }
        }

        const std::uint32_t other_layer = (s_layer + 1u) % 2u;
        if (g_layer == other_layer) {
            float best_cost = std::numeric_limits<float>::infinity();
            NavPathResult best_result;
            for (const auto& portal : portals) {
                const auto leg1 = RunSingleLayerAStar(nav.layers[s_layer], start_tile, portal.entry_tile);
                if (!leg1.ok) continue;
                const auto leg2 = RunSingleLayerAStar(nav.layers[g_layer], portal.exit_tile, goal_tile);
                if (!leg2.ok) continue;
                const float total = leg1.cost + leg2.cost + 2.0f;
                if (total < best_cost) {
                    best_cost = total;
                    best_result = {};
                    best_result.reachable = true;
                    best_result.crossed_layers = true;
                    best_result.total_cost = total;
                    append_waypoints(best_result.waypoints, s_layer, leg1.tiles, false);
                    append_waypoints(best_result.waypoints, g_layer, leg2.tiles, true);
                }
            }
            if (best_result.reachable) {
                return best_result;
            }
        }
    }

    return {};
}

pal4::portable::CollisionMesh ConvertNavFileToCollisionMesh(const NavFile& nav,
                                                            std::uint32_t active_layer) {
    pal4::portable::CollisionMesh mesh;
    if (nav.layers.empty()) return mesh;
    const auto& layer = nav.layers[std::min<std::size_t>(active_layer, nav.layers.size() - 1)];

    const float span_x = std::max(1.0f, layer.max_coord[0] - layer.min_coord[0]);
    const float span_z = std::max(1.0f, layer.max_coord[2] - layer.min_coord[2]);
    const float bx = span_x / static_cast<float>(layer.width + 1u);
    const float bz = span_z / static_cast<float>(layer.height + 1u);

    std::uint32_t floor_tri_id = 0;
    std::uint32_t wall_tri_id = 0;

    auto make_col_tri = [](pal4::portable::Point3 p0, pal4::portable::Point3 p1,
                           pal4::portable::Point3 p2, std::uint32_t mat, std::uint32_t tid) {
        pal4::portable::CollisionTriangle t{};
        for (int c = 0; c < 3; ++c) {
            t.points[0][c] = p0[c];
            t.points[1][c] = p1[c];
            t.points[2][c] = p2[c];
        }
        t.material = mat;
        t.triangle = tid;
        return t;
    };

    auto add_wall_quad = [&](pal4::portable::Point3 a0, pal4::portable::Point3 a1, float wall_h) {
        pal4::portable::Point3 b0{a0[0], a0[1] + wall_h, a0[2]};
        pal4::portable::Point3 b1{a1[0], a1[1] + wall_h, a1[2]};
        mesh.wall.push_back(make_col_tri(a0, a1, b1, 1u, wall_tri_id++));
        mesh.wall.push_back(make_col_tri(a0, b1, b0, 1u, wall_tri_id++));
    };

    for (std::int32_t tz = 0; tz < static_cast<std::int32_t>(layer.height); ++tz) {
        for (std::int32_t tx = 0; tx < static_cast<std::int32_t>(layer.width); ++tx) {
            const auto* pt = layer.At(tx, tz);
            if (!pt || pt->distance_to_border == 0u) continue;

            const float x0 = layer.min_coord[0] + static_cast<float>(tx) * bx;
            const float x1 = x0 + bx;
            const float z0 = layer.min_coord[2] + static_cast<float>(tz) * bz;
            const float z1 = z0 + bz;
            const float y = pt->height;

            const pal4::portable::Point3 p00{x0, y, z0};
            const pal4::portable::Point3 p10{x1, y, z0};
            const pal4::portable::Point3 p11{x1, y, z1};
            const pal4::portable::Point3 p01{x0, y, z1};

            mesh.floor.push_back(make_col_tri(p00, p01, p11, 0u, floor_tri_id++));
            mesh.floor.push_back(make_col_tri(p00, p11, p10, 0u, floor_tri_id++));

            // Emit vertical obstacle walls along any neighbor with distance_to_border == 0
            if (!IsNavTileWalkable(layer, tx, tz - 1)) add_wall_quad(p00, p10, 25.0f);
            if (!IsNavTileWalkable(layer, tx + 1, tz)) add_wall_quad(p10, p11, 25.0f);
            if (!IsNavTileWalkable(layer, tx, tz + 1)) add_wall_quad(p11, p01, 25.0f);
            if (!IsNavTileWalkable(layer, tx - 1, tz)) add_wall_quad(p01, p00, 25.0f);
        }
    }

    return mesh;
}

// ============================================================================
// 3. .pol, .cvd, and .mv3 Binary Model Parsers & OpenPAL4 Scene Bridges
// ============================================================================

std::optional<pal4::portable::Texture> DecodePal3Texture(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 18u) return std::nullopt;

    auto downsample_if_needed = [](pal4::portable::Texture tex) -> pal4::portable::Texture {
        while (tex.width > 256 && tex.height > 256 && (tex.width % 2) == 0 && (tex.height % 2) == 0) {
            const unsigned nw = tex.width / 2;
            const unsigned nh = tex.height / 2;
            std::vector<std::uint8_t> dst(static_cast<std::size_t>(nw) * nh * 4u);
            for (unsigned y = 0; y < nh; ++y) {
                for (unsigned x = 0; x < nw; ++x) {
                    const std::size_t i00 = (static_cast<std::size_t>(y * 2) * tex.width + (x * 2)) * 4u;
                    const std::size_t i10 = i00 + 4u;
                    const std::size_t i01 = i00 + static_cast<std::size_t>(tex.width) * 4u;
                    const std::size_t i11 = i01 + 4u;
                    const std::size_t out_i = (static_cast<std::size_t>(y) * nw + x) * 4u;
                    for (unsigned c = 0; c < 4u; ++c) {
                        const unsigned sum = static_cast<unsigned>(tex.rgba[i00 + c]) +
                                             static_cast<unsigned>(tex.rgba[i10 + c]) +
                                             static_cast<unsigned>(tex.rgba[i01 + c]) +
                                             static_cast<unsigned>(tex.rgba[i11 + c]);
                        dst[out_i + c] = static_cast<std::uint8_t>((sum + 2u) >> 2u);
                    }
                }
            }
            tex.width = nw;
            tex.height = nh;
            tex.rgba = std::move(dst);
        }
        return tex;
    };

    // 1. DDS Texture (`DDS `)
    if (bytes.size() >= 128u && std::memcmp(bytes.data(), "DDS ", 4) == 0) {
        try {
            return downsample_if_needed(pal4::portable::DecodeDdsTexture(bytes));
        } catch (...) {
            auto read_u32 = [&](std::size_t off) -> std::uint32_t {
                return static_cast<std::uint32_t>(bytes[off]) |
                       (static_cast<std::uint32_t>(bytes[off + 1]) << 8u) |
                       (static_cast<std::uint32_t>(bytes[off + 2]) << 16u) |
                       (static_cast<std::uint32_t>(bytes[off + 3]) << 24u);
            };
            const unsigned h = read_u32(12);
            const unsigned w = read_u32(16);
            const unsigned pf_flags = read_u32(80);
            const unsigned bpp = read_u32(88);
            const unsigned rmask = read_u32(92);
            if (w == 0 || h == 0 || w > 4096 || h > 4096 || (pf_flags & 0x40u) == 0u) {
                return std::nullopt;
            }
            pal4::portable::Texture img;
            img.width = w;
            img.height = h;
            img.rgba.resize(static_cast<std::size_t>(w) * h * 4u, 255u);
            if (bpp == 24u && bytes.size() >= 128u + static_cast<std::size_t>(w) * h * 3u) {
                for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i) {
                    img.rgba[i * 4 + 0] = bytes[128 + i * 3 + 2];
                    img.rgba[i * 4 + 1] = bytes[128 + i * 3 + 1];
                    img.rgba[i * 4 + 2] = bytes[128 + i * 3 + 0];
                    img.rgba[i * 4 + 3] = 255u;
                }
                return downsample_if_needed(std::move(img));
            }
            if (bpp == 16u && bytes.size() >= 128u + static_cast<std::size_t>(w) * h * 2u) {
                for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i) {
                    const std::uint16_t px = static_cast<std::uint16_t>(bytes[128 + i * 2]) |
                                             (static_cast<std::uint16_t>(bytes[128 + i * 2 + 1]) << 8u);
                    if (rmask == 0xF800u) {
                        img.rgba[i * 4 + 0] = static_cast<std::uint8_t>(((px >> 11u) & 31u) * 255u / 31u);
                        img.rgba[i * 4 + 1] = static_cast<std::uint8_t>(((px >> 5u) & 63u) * 255u / 63u);
                        img.rgba[i * 4 + 2] = static_cast<std::uint8_t>((px & 31u) * 255u / 31u);
                        img.rgba[i * 4 + 3] = 255u;
                    } else if (rmask == 0x0F00u) {
                        img.rgba[i * 4 + 0] = static_cast<std::uint8_t>(((px >> 8u) & 15u) * 17u);
                        img.rgba[i * 4 + 1] = static_cast<std::uint8_t>(((px >> 4u) & 15u) * 17u);
                        img.rgba[i * 4 + 2] = static_cast<std::uint8_t>((px & 15u) * 17u);
                        img.rgba[i * 4 + 3] = static_cast<std::uint8_t>(((px >> 12u) & 15u) * 17u);
                        if (img.rgba[i * 4 + 3] != 255u) img.alpha = true;
                    } else {
                        img.rgba[i * 4 + 0] = static_cast<std::uint8_t>(((px >> 10u) & 31u) * 255u / 31u);
                        img.rgba[i * 4 + 1] = static_cast<std::uint8_t>(((px >> 5u) & 31u) * 255u / 31u);
                        img.rgba[i * 4 + 2] = static_cast<std::uint8_t>((px & 31u) * 255u / 31u);
                        img.rgba[i * 4 + 3] = (px & 0x8000u) ? 255u : 0u;
                        if (img.rgba[i * 4 + 3] != 255u) img.alpha = true;
                    }
                }
                return downsample_if_needed(std::move(img));
            }
            return std::nullopt;
        }
    }

    // 2. TGA Texture (uncompressed TrueColor type 2 or RLE TrueColor type 10, 24/32-bit)
    const std::uint8_t id_len = bytes[0];
    const std::uint8_t cmap_type = bytes[1];
    const std::uint8_t img_type = bytes[2];
    const unsigned w = static_cast<unsigned>(bytes[12]) | (static_cast<unsigned>(bytes[13]) << 8u);
    const unsigned h = static_cast<unsigned>(bytes[14]) | (static_cast<unsigned>(bytes[15]) << 8u);
    const unsigned bpp = bytes[16];
    const std::uint8_t desc = bytes[17];
    if (cmap_type == 0u && (img_type == 2u || img_type == 10u) && (bpp == 24u || bpp == 32u) &&
        w > 0u && w <= 4096u && h > 0u && h <= 4096u) {
        const std::size_t bytes_per_px = bpp / 8u;
        std::size_t pos = 18u + id_len;
        if (pos > bytes.size()) return std::nullopt;

        pal4::portable::Texture img;
        img.width = w;
        img.height = h;
        const std::size_t total_px = static_cast<std::size_t>(w) * h;
        std::vector<std::uint8_t> raw_rgba(total_px * 4u, 255u);

        if (img_type == 2u) {
            if (pos + total_px * bytes_per_px > bytes.size()) return std::nullopt;
            for (std::size_t i = 0; i < total_px; ++i) {
                raw_rgba[i * 4 + 0] = bytes[pos + 2];
                raw_rgba[i * 4 + 1] = bytes[pos + 1];
                raw_rgba[i * 4 + 2] = bytes[pos + 0];
                raw_rgba[i * 4 + 3] = (bytes_per_px == 4u) ? bytes[pos + 3] : 255u;
                if (raw_rgba[i * 4 + 3] != 255u) img.alpha = true;
                pos += bytes_per_px;
            }
        } else {
            std::size_t px_idx = 0;
            while (px_idx < total_px && pos < bytes.size()) {
                const std::uint8_t hdr = bytes[pos++];
                const std::size_t run = static_cast<std::size_t>(hdr & 0x7Fu) + 1u;
                if (hdr & 0x80u) {
                    if (pos + bytes_per_px > bytes.size()) break;
                    const std::uint8_t b = bytes[pos + 0];
                    const std::uint8_t g = bytes[pos + 1];
                    const std::uint8_t r = bytes[pos + 2];
                    const std::uint8_t a = (bytes_per_px == 4u) ? bytes[pos + 3] : 255u;
                    pos += bytes_per_px;
                    if (a != 255u) img.alpha = true;
                    for (std::size_t k = 0; k < run && px_idx < total_px; ++k, ++px_idx) {
                        raw_rgba[px_idx * 4 + 0] = r;
                        raw_rgba[px_idx * 4 + 1] = g;
                        raw_rgba[px_idx * 4 + 2] = b;
                        raw_rgba[px_idx * 4 + 3] = a;
                    }
                } else {
                    for (std::size_t k = 0; k < run && px_idx < total_px; ++k, ++px_idx) {
                        if (pos + bytes_per_px > bytes.size()) break;
                        raw_rgba[px_idx * 4 + 0] = bytes[pos + 2];
                        raw_rgba[px_idx * 4 + 1] = bytes[pos + 1];
                        raw_rgba[px_idx * 4 + 2] = bytes[pos + 0];
                        raw_rgba[px_idx * 4 + 3] = (bytes_per_px == 4u) ? bytes[pos + 3] : 255u;
                        if (raw_rgba[px_idx * 4 + 3] != 255u) img.alpha = true;
                        pos += bytes_per_px;
                    }
                }
            }
        }

        const bool top_down = (desc & 0x20u) != 0u;
        if (top_down) {
            img.rgba = std::move(raw_rgba);
        } else {
            img.rgba.resize(total_px * 4u);
            const std::size_t row_bytes = static_cast<std::size_t>(w) * 4u;
            for (unsigned y = 0; y < h; ++y) {
                std::memcpy(img.rgba.data() + static_cast<std::size_t>(y) * row_bytes,
                            raw_rgba.data() + static_cast<std::size_t>(h - 1u - y) * row_bytes,
                            row_bytes);
            }
        }
        return downsample_if_needed(std::move(img));
    }

    return std::nullopt;
}

namespace {

void EnsureDefaultTexture(pal4::portable::Scene& scene) {
    scene.brightness = 1.0f;
    scene.saturation = {1.0f, 1.0f, 1.0f};
    if (scene.textures.empty()) {
        pal4::portable::Texture fallback;
        fallback.width = 2;
        fallback.height = 2;
        fallback.rgba = {255, 255, 255, 255, 220, 235, 255, 255,
                         220, 235, 255, 255, 255, 255, 255, 255};
        scene.textures.push_back(std::move(fallback));
    }
}

void AppendMeshDrawToScene(pal4::portable::Scene& scene,
                           const std::vector<pal4::portable::Vertex>& vertices,
                           const std::vector<std::array<std::uint16_t, 3>>& triangles,
                           int diffuse_tex_index = 0,
                           const std::vector<std::array<std::uint16_t, 3>>* custom_uv_indices = nullptr,
                           const std::vector<std::array<float, 2>>* custom_texcoords = nullptr) {
    if (vertices.empty() || triangles.empty()) return;
    EnsureDefaultTexture(scene);
    const bool bounds_was_empty = scene.vertices.empty();

    pal4::portable::Draw draw{};
    draw.first = static_cast<std::uint32_t>(scene.vertices.size());
    draw.count = static_cast<std::uint32_t>(triangles.size() * 3u);
    draw.diffuse = diffuse_tex_index;
    draw.lightmap = -1;
    for (int c = 0; c < 4; ++c) draw.color[c] = 1.0f;

    std::array<float, 3> center_sum{0.0f, 0.0f, 0.0f};
    bool first_v = bounds_was_empty;
    for (std::size_t ti = 0; ti < triangles.size(); ++ti) {
        const auto& tri = triangles[ti];
        if (tri[0] >= vertices.size() || tri[1] >= vertices.size() || tri[2] >= vertices.size()) {
            draw.count -= 3u;
            continue;
        }
        const auto& p0 = vertices[tri[0]].position;
        const auto& p1 = vertices[tri[1]].position;
        const auto& p2 = vertices[tri[2]].position;
        const float ex1 = p1[0] - p0[0], ey1 = p1[1] - p0[1], ez1 = p1[2] - p0[2];
        const float ex2 = p2[0] - p0[0], ey2 = p2[1] - p0[1], ez2 = p2[2] - p0[2];
        float nx = ey1 * ez2 - ez1 * ey2;
        float ny = ez1 * ex2 - ex1 * ez2;
        float nz = ex1 * ey2 - ey1 * ex2;
        const float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (nlen > 1e-6f) {
            nx /= nlen;
            ny /= nlen;
            nz /= nlen;
        } else {
            nx = 0.0f;
            ny = 1.0f;
            nz = 0.0f;
        }
        const float ndotl = nx * 0.42f + std::abs(ny) * 0.72f + nz * 0.42f;
        const float shade = std::clamp(0.74f + 0.26f * ndotl, 0.62f, 1.0f);

        for (int k = 0; k < 3; ++k) {
            auto v = vertices[tri[k]];
            if (custom_uv_indices && custom_texcoords && ti < custom_uv_indices->size()) {
                const std::uint16_t uv_idx = (*custom_uv_indices)[ti][k];
                if (uv_idx < custom_texcoords->size()) {
                    v.uv0[0] = (*custom_texcoords)[uv_idx][0];
                    v.uv0[1] = 1.0f - (*custom_texcoords)[uv_idx][1];
                    v.uv1[0] = v.uv0[0];
                    v.uv1[1] = v.uv0[1];
                }
            }
            if (v.color[0] >= 0.99f && v.color[1] >= 0.99f && v.color[2] >= 0.99f) {
                if (diffuse_tex_index > 0) {
                    v.color[0] = shade;
                    v.color[1] = shade;
                    v.color[2] = shade;
                } else {
                    // Warm architectural stone/wood tint when using fallback white texture
                    const float h_tint = std::clamp((v.position[1] + 20.0f) / 80.0f, 0.0f, 1.0f);
                    v.color[0] = shade * (0.68f + 0.18f * h_tint);
                    v.color[1] = shade * (0.66f + 0.16f * h_tint);
                    v.color[2] = shade * (0.62f + 0.14f * h_tint);
                }
            }
            for (int axis = 0; axis < 3; ++axis) {
                if (first_v) {
                    scene.minimum[axis] = v.position[axis];
                    scene.maximum[axis] = v.position[axis];
                } else {
                    scene.minimum[axis] = std::min(scene.minimum[axis], v.position[axis]);
                    scene.maximum[axis] = std::max(scene.maximum[axis], v.position[axis]);
                }
                center_sum[axis] += v.position[axis];
            }
            first_v = false;
            scene.vertices.push_back(v);
        }
    }
    if (draw.count > 0u) {
        const float inv = 1.0f / static_cast<float>(draw.count);
        scene.atomic_centers.push_back({center_sum[0] * inv, center_sum[1] * inv, center_sum[2] * inv});
        scene.draws.push_back(draw);
    }
}

}  // namespace

std::vector<std::uint8_t> SerializePolModel(const PolModel& pol) {
    std::vector<std::uint8_t> out;
    out.insert(out.end(), {'P', 'O', 'L', 'Y'});
    WriteU32(out, pol.version);
    WriteU32(out, static_cast<std::uint32_t>(pol.meshes.size()));
    for (const auto& m : pol.meshes) {
        WriteFixedString(out, m.name, 32);
        WriteFixedString(out, m.texture_name, 64);
        for (float v : m.aabb_min) WriteF32(out, v);
        for (float v : m.aabb_max) WriteF32(out, v);
        WriteU32(out, static_cast<std::uint32_t>(m.vertices.size()));
        for (const auto& vtx : m.vertices) {
            WriteF32(out, vtx.position[0]);
            WriteF32(out, vtx.position[1]);
            WriteF32(out, vtx.position[2]);
            WriteF32(out, vtx.uv0[0]);
            WriteF32(out, vtx.uv0[1]);
            WriteF32(out, vtx.uv1[0]);
            WriteF32(out, vtx.uv1[1]);
            for (int c = 0; c < 4; ++c) WriteF32(out, vtx.color[c]);
        }
        WriteU32(out, static_cast<std::uint32_t>(m.triangles.size()));
        for (const auto& tri : m.triangles) {
            WriteU16(out, tri[0]);
            WriteU16(out, tri[1]);
            WriteU16(out, tri[2]);
        }
    }
    return out;
}

PolModel ParsePolModel(std::span<const std::uint8_t> bytes) {
    // 1. Try real PAL3 .pol format (`OpenPAL3/yaobow/fileformats/src/pol.rs`)
    try {
        ByteReader r{bytes, 0};
        if (r.ReadFixedString(4) != "POLY") {
            throw std::runtime_error("invalid PAL3 .pol magic (expected POLY)");
        }
        PolModel pol;
        pol.version = r.ReadU32();  // some_flag
        const std::uint32_t mesh_cnt = r.ReadU32();
        if (mesh_cnt == 0u || mesh_cnt > 4096u) {
            throw std::runtime_error("unreasonable real .pol mesh_cnt");
        }
        // Skip GeomNodeDesc (mesh_cnt * 52 bytes)
        r.Seek(r.pos + static_cast<std::size_t>(mesh_cnt) * 52u);
        if (pol.version > 100u) {
            const std::uint32_t unknown_cnt = r.ReadU32();
            for (std::uint32_t u = 0; u < unknown_cnt; ++u) {
                r.Seek(r.pos + 32u + 64u + 4u);
                const std::uint32_t str_len = r.ReadU32();
                r.Seek(r.pos + str_len);
            }
        }
        pol.meshes.reserve(mesh_cnt);
        for (std::uint32_t m_idx = 0; m_idx < mesh_cnt; ++m_idx) {
            const std::array<float, 3> aabb_min = {r.ReadF32(), r.ReadF32(), r.ReadF32()};
            const std::array<float, 3> aabb_max = {r.ReadF32(), r.ReadF32(), r.ReadF32()};
            const std::uint32_t vtype = r.ReadU32();
            const std::uint32_t vcnt = r.ReadU32();
            if ((vtype & 0x1u) == 0u || (vtype & 0x10u) == 0u || vcnt > 262144u) {
                throw std::runtime_error("unsupported real .pol vertex_type/vcnt");
            }
            std::vector<pal4::portable::Vertex> shared_verts(vcnt);
            for (std::uint32_t v = 0; v < vcnt; ++v) {
                auto& vtx = shared_verts[v];
                for (int c = 0; c < 3; ++c) vtx.position[c] = r.ReadF32();
                if (vtype & 0x2u) r.Seek(r.pos + 12u);   // normal
                if (vtype & 0x4u) r.Seek(r.pos + 4u);    // unknown4
                if (vtype & 0x8u) r.Seek(r.pos + 4u);    // unknown8
                const float u0 = r.ReadF32();
                const float v0 = r.ReadF32();
                if (vtype & 0x20u) {
                    const float u1 = r.ReadF32();
                    const float v1 = r.ReadF32();
                    // In PAL3 2-texture .pol meshes, tex_coord2 is diffuse UV and tex_coord is lightmap UV
                    vtx.uv0[0] = u1;
                    vtx.uv0[1] = v1;
                    vtx.uv1[0] = u0;
                    vtx.uv1[1] = v0;
                } else {
                    vtx.uv0[0] = u0;
                    vtx.uv0[1] = v0;
                    vtx.uv1[0] = u0;
                    vtx.uv1[1] = v0;
                }
                if (vtype & 0x40u) r.Seek(r.pos + 8u);
                if (vtype & 0x80u) r.Seek(r.pos + 8u);
                if (vtype & 0x100u) r.Seek(r.pos + 16u);
                for (int c = 0; c < 4; ++c) vtx.color[c] = 1.0f;
            }
            const std::uint32_t mat_cnt = r.ReadU32();
            for (std::uint32_t mat = 0; mat < mat_cnt; ++mat) {
                (void)r.ReadU32();  // use_alpha
                r.Seek(r.pos + 68u);  // unknown_68 (16*f32) + unknown_float (f32)
                const std::uint32_t tex_cnt = r.ReadU32();
                std::string diffuse_tname;
                for (std::uint32_t tc = 0; tc < tex_cnt; ++tc) {
                    const std::string tname = r.ReadFixedString(64);
                    if (!tname.empty()) diffuse_tname = tname;
                }
                r.Seek(r.pos + 12u);  // unknown2, unknown3, unknown4
                const std::uint32_t tri_cnt = r.ReadU32();

                PolSubMesh sm;
                sm.name = "mesh_" + std::to_string(m_idx) + "_mat_" + std::to_string(mat);
                sm.texture_name = diffuse_tname;
                sm.aabb_min = aabb_min;
                sm.aabb_max = aabb_max;

                std::unordered_map<std::uint16_t, std::uint16_t> remap;
                sm.triangles.reserve(tri_cnt);
                for (std::uint32_t t = 0; t < tri_cnt; ++t) {
                    const std::uint16_t i0 = r.ReadU16();
                    const std::uint16_t i1 = r.ReadU16();
                    const std::uint16_t i2 = r.ReadU16();
                    if (i0 >= vcnt || i1 >= vcnt || i2 >= vcnt) continue;
                    auto map_idx = [&](std::uint16_t orig) -> std::uint16_t {
                        auto it = remap.find(orig);
                        if (it != remap.end()) return it->second;
                        const auto next = static_cast<std::uint16_t>(sm.vertices.size());
                        sm.vertices.push_back(shared_verts[orig]);
                        remap.emplace(orig, next);
                        return next;
                    };
                    sm.triangles.push_back({map_idx(i0), map_idx(i1), map_idx(i2)});
                }
                if (!sm.triangles.empty()) {
                    pol.meshes.push_back(std::move(sm));
                }
            }
        }
        return pol;
    } catch (...) {
        // Fall through to compact synthetic .pol format
    }

    ByteReader r{bytes, 0};
    if (r.ReadFixedString(4) != "POLY") {
        throw std::runtime_error("invalid PAL3 .pol magic (expected POLY)");
    }
    PolModel pol;
    pol.version = r.ReadU32();
    const std::uint32_t mesh_cnt = r.ReadU32();
    pol.meshes.reserve(mesh_cnt);
    for (std::uint32_t i = 0; i < mesh_cnt; ++i) {
        PolSubMesh m;
        m.name = r.ReadFixedString(32);
        m.texture_name = r.ReadFixedString(64);
        m.aabb_min = {r.ReadF32(), r.ReadF32(), r.ReadF32()};
        m.aabb_max = {r.ReadF32(), r.ReadF32(), r.ReadF32()};
        const std::uint32_t vcnt = r.ReadU32();
        m.vertices.resize(vcnt);
        for (std::uint32_t v = 0; v < vcnt; ++v) {
            for (int c = 0; c < 3; ++c) m.vertices[v].position[c] = r.ReadF32();
            for (int c = 0; c < 2; ++c) m.vertices[v].uv0[c] = r.ReadF32();
            for (int c = 0; c < 2; ++c) m.vertices[v].uv1[c] = r.ReadF32();
            for (int c = 0; c < 4; ++c) m.vertices[v].color[c] = r.ReadF32();
        }
        const std::uint32_t tcnt = r.ReadU32();
        m.triangles.resize(tcnt);
        for (std::uint32_t t = 0; t < tcnt; ++t) {
            m.triangles[t] = {r.ReadU16(), r.ReadU16(), r.ReadU16()};
        }
        pol.meshes.push_back(std::move(m));
    }
    return pol;
}

void NormalizePolModelToStageBounds(PolModel& pol, float target_half_span) {
    float min_x = std::numeric_limits<float>::infinity();
    float min_y = std::numeric_limits<float>::infinity();
    float min_z = std::numeric_limits<float>::infinity();
    float max_x = -std::numeric_limits<float>::infinity();
    float max_y = -std::numeric_limits<float>::infinity();
    float max_z = -std::numeric_limits<float>::infinity();
    std::vector<float> y_samples;

    for (const auto& sm : pol.meshes) {
        for (const auto& v : sm.vertices) {
            if (!std::isfinite(v.position[0]) || !std::isfinite(v.position[1]) || !std::isfinite(v.position[2])) {
                continue;
            }
            if (std::abs(v.position[0]) > 25000.0f || std::abs(v.position[2]) > 25000.0f) continue;
            min_x = std::min(min_x, v.position[0]);
            min_y = std::min(min_y, v.position[1]);
            min_z = std::min(min_z, v.position[2]);
            max_x = std::max(max_x, v.position[0]);
            max_y = std::max(max_y, v.position[1]);
            max_z = std::max(max_z, v.position[2]);
            if (y_samples.size() < 4096) y_samples.push_back(v.position[1]);
        }
    }
    if (!std::isfinite(min_x) || !std::isfinite(max_x)) return;

    const float span_x = std::max(1.0f, max_x - min_x);
    const float span_z = std::max(1.0f, max_z - min_z);
    const float max_horiz = std::max(span_x, span_z);
    const float scale = (2.0f * target_half_span) / max_horiz;
    const float cx = 0.5f * (min_x + max_x);
    const float cz = 0.5f * (min_z + max_z);
    float base_y = min_y;
    if (!y_samples.empty()) {
        std::sort(y_samples.begin(), y_samples.end());
        base_y = y_samples[y_samples.size() / 6];
    }

    for (auto& sm : pol.meshes) {
        for (auto& v : sm.vertices) {
            v.position[0] = (v.position[0] - cx) * scale;
            v.position[1] = (v.position[1] - base_y) * scale;
            v.position[2] = (v.position[2] - cz) * scale;
        }
    }
}

pal4::portable::Scene ConvertPolToPortableScene(const PolModel& pol) {
    pal4::portable::Scene scene;
    EnsureDefaultTexture(scene);
    std::unordered_map<std::string, int> tex_index_by_name;
    for (const auto& sm : pol.meshes) {
        int tex_idx = 0;
        if (sm.decoded_texture && sm.decoded_texture->width > 0 && sm.decoded_texture->height > 0 &&
            !sm.decoded_texture->rgba.empty()) {
            const std::string key = sm.texture_name.empty() ? sm.name : sm.texture_name;
            auto it = tex_index_by_name.find(key);
            if (it != tex_index_by_name.end()) {
                tex_idx = it->second;
            } else {
                tex_idx = static_cast<int>(scene.textures.size());
                scene.textures.push_back(*sm.decoded_texture);
                tex_index_by_name.emplace(key, tex_idx);
            }
        }
        AppendMeshDrawToScene(scene, sm.vertices, sm.triangles, tex_idx);
    }
    return scene;
}

std::vector<std::uint8_t> SerializeCvdModel(const CvdModel& cvd) {
    std::vector<std::uint8_t> out;
    out.insert(out.end(), {'c', 'v', 'd', 'f'});
    WriteF32(out, cvd.animation_duration_sec);
    WriteU32(out, static_cast<std::uint32_t>(cvd.nodes.size()));
    for (const auto& n : cvd.nodes) {
        WriteFixedString(out, n.name, 32);
        WriteF32(out, n.scale);
        WriteU32(out, static_cast<std::uint32_t>(n.position_keys.size()));
        for (const auto& pk : n.position_keys) {
            WriteF32(out, pk.timestamp_sec);
            WriteF32(out, pk.position[0]);
            WriteF32(out, pk.position[1]);
            WriteF32(out, pk.position[2]);
        }
        WriteU32(out, static_cast<std::uint32_t>(n.rotation_keys.size()));
        for (const auto& rk : n.rotation_keys) {
            WriteF32(out, rk.timestamp_sec);
            WriteF32(out, rk.angle_deg);
            WriteF32(out, rk.axis[0]);
            WriteF32(out, rk.axis[1]);
            WriteF32(out, rk.axis[2]);
        }
        WriteU32(out, static_cast<std::uint32_t>(n.vertices.size()));
        for (const auto& vtx : n.vertices) {
            WriteF32(out, vtx.position[0]);
            WriteF32(out, vtx.position[1]);
            WriteF32(out, vtx.position[2]);
            WriteF32(out, vtx.uv0[0]);
            WriteF32(out, vtx.uv0[1]);
            WriteF32(out, vtx.uv1[0]);
            WriteF32(out, vtx.uv1[1]);
            for (int c = 0; c < 4; ++c) WriteF32(out, vtx.color[c]);
        }
        WriteU32(out, static_cast<std::uint32_t>(n.triangles.size()));
        for (const auto& tri : n.triangles) {
            WriteU16(out, tri[0]);
            WriteU16(out, tri[1]);
            WriteU16(out, tri[2]);
        }
    }
    return out;
}

CvdModel ParseCvdModel(std::span<const std::uint8_t> bytes) {
    ByteReader probe{bytes, 0};
    const std::string magic = probe.ReadFixedString(4);
    if (magic != "cvdf" && magic != "cvds") {
        throw std::runtime_error("invalid PAL3 .cvd magic (expected cvdf or cvds)");
    }
    const std::uint32_t raw_first_u32 = probe.ReadU32();
    const bool has_material_extra = (magic == "cvds");

    // In real PAL3 .cvd (`OpenPAL3/yaobow/fileformats/src/pal3/cvd.rs`), offset 4 is `u32 model_count` (1..512),
    // whereas in synthetic .cvd offset 4 is IEEE-754 `f32 animation_duration_sec` (>= 0x3C000000).
    if (raw_first_u32 > 0u && raw_first_u32 <= 512u) {
        try {
            ByteReader r{bytes, 8};
            CvdModel cvd;
            cvd.animation_duration_sec = 1.0f;
            float max_ts = 1.0f;

            auto read_node_recursive = [&](auto& self) -> void {
                const std::uint8_t has_model = r.ReadU8();
                if (has_model > 0u) {
                    CvdNode node;
                    node.name = "cvd_node_" + std::to_string(cvd.nodes.size());

                    // 1. Position keyframes
                    const std::int32_t pos_cnt = r.ReadI32();
                    if (pos_cnt > 0) {
                        const std::uint8_t ver = r.ReadU8();
                        (void)ver;
                        for (std::int32_t k = 0; k < pos_cnt; ++k) {
                            CvdPositionKey pk;
                            pk.timestamp_sec = r.ReadF32();
                            max_ts = std::max(max_ts, pk.timestamp_sec);
                            float u[10]{};
                            for (int j = 0; j < 10; ++j) u[j] = r.ReadF32();
                            pk.position = {u[0], u[1], u[2]};
                            node.position_keys.push_back(pk);
                        }
                    }

                    // 2. Rotation keyframes
                    const std::int32_t rot_cnt = r.ReadI32();
                    if (rot_cnt > 0) {
                        const std::uint8_t ver = r.ReadU8();
                        (void)ver;
                        for (std::int32_t k = 0; k < rot_cnt; ++k) {
                            CvdRotationKey rk;
                            rk.timestamp_sec = r.ReadF32();
                            max_ts = std::max(max_ts, rk.timestamp_sec);
                            float u[10]{};
                            for (int j = 0; j < 10; ++j) u[j] = r.ReadF32();
                            rk.angle_deg = u[3];
                            rk.axis = {u[0], u[1], u[2]};
                            node.rotation_keys.push_back(rk);
                        }
                    }

                    // 3. Scale keyframes
                    const std::int32_t scale_cnt = r.ReadI32();
                    if (scale_cnt > 0) {
                        (void)r.ReadU8();
                        r.Seek(r.pos + static_cast<std::size_t>(scale_cnt) * 60u);
                    }

                    node.scale = r.ReadF32();
                    if (node.scale <= 0.0f) node.scale = 1.0f;

                    // 4. Mesh
                    const std::uint32_t frame_cnt = r.ReadU32();
                    const std::uint32_t vtx_cnt = r.ReadU32();
                    for (std::uint32_t f = 0; f < frame_cnt; ++f) {
                        for (std::uint32_t v = 0; v < vtx_cnt; ++v) {
                            const float u = r.ReadF32();
                            const float tv = r.ReadF32();
                            r.Seek(r.pos + 12u);  // normal
                            const float px = r.ReadF32();
                            const float py = r.ReadF32();
                            const float pz = r.ReadF32();
                            if (f == 0u) {
                                pal4::portable::Vertex vtx{};
                                vtx.position[0] = px;
                                vtx.position[1] = py;
                                vtx.position[2] = pz;
                                vtx.uv0[0] = u;
                                vtx.uv0[1] = tv;
                                vtx.uv1[0] = u;
                                vtx.uv1[1] = tv;
                                for (int c = 0; c < 4; ++c) vtx.color[c] = 1.0f;
                                node.vertices.push_back(vtx);
                            }
                        }
                    }
                    r.Seek(r.pos + static_cast<std::size_t>(frame_cnt) * 4u);  // frame_extra

                    const std::uint32_t mat_cnt = r.ReadU32();
                    for (std::uint32_t m = 0; m < mat_cnt; ++m) {
                        r.Seek(r.pos + 1u + 16u + 4u + 64u);  // u8 + 4*u32 + f32 + 64B tex_name
                        const std::uint32_t tri_cnt = r.ReadU32();
                        for (std::uint32_t t = 0; t < tri_cnt; ++t) {
                            node.triangles.push_back({r.ReadU16(), r.ReadU16(), r.ReadU16()});
                        }
                        if (has_material_extra) {
                            const std::uint32_t extra_cnt = r.ReadU32();
                            r.Seek(r.pos + static_cast<std::size_t>(extra_cnt) * 24u);
                        }
                    }
                    r.Seek(r.pos + 64u);  // 4x4 matrix
                    cvd.nodes.push_back(std::move(node));
                }
                const std::uint32_t child_cnt = r.ReadU32();
                for (std::uint32_t c = 0; c < child_cnt; ++c) {
                    self(self);
                }
            };

            for (std::uint32_t i = 0; i < raw_first_u32; ++i) {
                read_node_recursive(read_node_recursive);
            }
            cvd.animation_duration_sec = max_ts;
            return cvd;
        } catch (...) {
            // Fall through to synthetic .cvd parser
        }
    }

    ByteReader r{bytes, 4};
    CvdModel cvd;
    cvd.animation_duration_sec = r.ReadF32();
    const std::uint32_t ncnt = r.ReadU32();
    cvd.nodes.reserve(ncnt);
    for (std::uint32_t i = 0; i < ncnt; ++i) {
        CvdNode n;
        n.name = r.ReadFixedString(32);
        n.scale = r.ReadF32();
        const std::uint32_t pk_cnt = r.ReadU32();
        n.position_keys.resize(pk_cnt);
        for (std::uint32_t k = 0; k < pk_cnt; ++k) {
            n.position_keys[k].timestamp_sec = r.ReadF32();
            n.position_keys[k].position = {r.ReadF32(), r.ReadF32(), r.ReadF32()};
        }
        const std::uint32_t rk_cnt = r.ReadU32();
        n.rotation_keys.resize(rk_cnt);
        for (std::uint32_t k = 0; k < rk_cnt; ++k) {
            n.rotation_keys[k].timestamp_sec = r.ReadF32();
            n.rotation_keys[k].angle_deg = r.ReadF32();
            n.rotation_keys[k].axis = {r.ReadF32(), r.ReadF32(), r.ReadF32()};
        }
        const std::uint32_t vcnt = r.ReadU32();
        n.vertices.resize(vcnt);
        for (std::uint32_t v = 0; v < vcnt; ++v) {
            for (int c = 0; c < 3; ++c) n.vertices[v].position[c] = r.ReadF32();
            for (int c = 0; c < 2; ++c) n.vertices[v].uv0[c] = r.ReadF32();
            for (int c = 0; c < 2; ++c) n.vertices[v].uv1[c] = r.ReadF32();
            for (int c = 0; c < 4; ++c) n.vertices[v].color[c] = r.ReadF32();
        }
        const std::uint32_t tcnt = r.ReadU32();
        n.triangles.resize(tcnt);
        for (std::uint32_t t = 0; t < tcnt; ++t) {
            n.triangles[t] = {r.ReadU16(), r.ReadU16(), r.ReadU16()};
        }
        cvd.nodes.push_back(std::move(n));
    }
    return cvd;
}

EvaluatedCvdState EvaluateCvdAnimation(const CvdModel& cvd, float time_sec) {
    EvaluatedCvdState state;
    const float clamped = std::clamp(time_sec, 0.0f, std::max(0.001f, cvd.animation_duration_sec));
    state.time_sec = clamped;
    state.finished = (time_sec >= cvd.animation_duration_sec);

    for (const auto& node : cvd.nodes) {
        std::array<float, 3> pos{0.0f, 0.0f, 0.0f};
        if (!node.position_keys.empty()) {
            pos = node.position_keys.back().position;
            for (std::size_t i = 0; i + 1 < node.position_keys.size(); ++i) {
                const auto& k0 = node.position_keys[i];
                const auto& k1 = node.position_keys[i + 1];
                if (clamped >= k0.timestamp_sec && clamped <= k1.timestamp_sec) {
                    const float span = std::max(1e-5f, k1.timestamp_sec - k0.timestamp_sec);
                    const float t = (clamped - k0.timestamp_sec) / span;
                    for (int c = 0; c < 3; ++c) {
                        pos[c] = k0.position[c] + t * (k1.position[c] - k0.position[c]);
                    }
                    break;
                }
            }
        }
        float rot = 0.0f;
        if (!node.rotation_keys.empty()) {
            rot = node.rotation_keys.back().angle_deg;
            for (std::size_t i = 0; i + 1 < node.rotation_keys.size(); ++i) {
                const auto& k0 = node.rotation_keys[i];
                const auto& k1 = node.rotation_keys[i + 1];
                if (clamped >= k0.timestamp_sec && clamped <= k1.timestamp_sec) {
                    const float span = std::max(1e-5f, k1.timestamp_sec - k0.timestamp_sec);
                    const float t = (clamped - k0.timestamp_sec) / span;
                    rot = k0.angle_deg + t * (k1.angle_deg - k0.angle_deg);
                    break;
                }
            }
        }
        state.node_positions.push_back(pos);
        state.node_rotations_deg.push_back(rot);
        state.state_checksum += static_cast<double>(pos[0] + pos[1] + pos[2] + rot);
    }
    return state;
}

pal4::portable::Scene ConvertCvdToPortableScene(const CvdModel& cvd, float time_sec) {
    const auto eval = EvaluateCvdAnimation(cvd, time_sec);
    pal4::portable::Scene scene;
    EnsureDefaultTexture(scene);
    for (std::size_t i = 0; i < cvd.nodes.size(); ++i) {
        const auto& n = cvd.nodes[i];
        const auto offset = eval.node_positions[i];
        std::vector<pal4::portable::Vertex> transformed = n.vertices;
        for (auto& v : transformed) {
            v.position[0] = v.position[0] * n.scale + offset[0];
            v.position[1] = v.position[1] * n.scale + offset[1];
            v.position[2] = v.position[2] * n.scale + offset[2];
        }
        AppendMeshDrawToScene(scene, transformed, n.triangles);
    }
    return scene;
}

std::optional<float> Mv3Model::HoldMarkSec() const noexcept {
    for (const auto& ev : events) {
        if (ev.name == "hold") {
            return static_cast<float>(ev.timestamp_ticks) / 4580.0f;
        }
    }
    return std::nullopt;
}

std::vector<std::uint8_t> SerializeMv3Model(const Mv3Model& mv3) {
    std::vector<std::uint8_t> out;
    out.insert(out.end(), {'M', 'V', '3', '\0'});
    WriteU32(out, mv3.version);
    WriteFixedString(out, mv3.action_name, 16);
    WriteFixedString(out, mv3.texture_name, 64);
    WriteU32(out, mv3.duration_ticks);
    WriteU32(out, static_cast<std::uint32_t>(mv3.events.size()));
    for (const auto& ev : mv3.events) {
        WriteU32(out, ev.timestamp_ticks);
        WriteFixedString(out, ev.name, 16);
    }
    const std::uint32_t vcnt =
        mv3.frames.empty() ? 0u : static_cast<std::uint32_t>(mv3.frames.front().vertex_positions_q6.size());
    WriteU32(out, vcnt);
    WriteU32(out, static_cast<std::uint32_t>(mv3.frames.size()));
    for (const auto& f : mv3.frames) {
        WriteU32(out, f.timestamp_ticks);
        for (std::uint32_t v = 0; v < vcnt; ++v) {
            const auto pt = (v < f.vertex_positions_q6.size())
                                ? f.vertex_positions_q6[v]
                                : std::array<std::int16_t, 3>{0, 0, 0};
            WriteI16(out, pt[0]);
            WriteI16(out, pt[1]);
            WriteI16(out, pt[2]);
        }
    }
    for (std::uint32_t v = 0; v < vcnt; ++v) {
        const auto uv = (v < mv3.texcoords.size()) ? mv3.texcoords[v] : std::array<float, 2>{0.0f, 0.0f};
        WriteF32(out, uv[0]);
        WriteF32(out, uv[1]);
    }
    WriteU32(out, static_cast<std::uint32_t>(mv3.triangles.size()));
    for (const auto& tri : mv3.triangles) {
        WriteU16(out, tri[0]);
        WriteU16(out, tri[1]);
        WriteU16(out, tri[2]);
    }
    return out;
}

Mv3Model ParseMv3Model(std::span<const std::uint8_t> bytes) {
    // 1. Try real PAL3 .mv3 format (`OpenPAL3/yaobow/fileformats/src/mv3.rs`)
    if (bytes.size() >= 28u) {
        try {
            ByteReader r{bytes, 0};
            if (r.ReadFixedString(4) != "MV3") {
                throw std::runtime_error("invalid PAL3 .mv3 magic (expected MV3\\0)");
            }
            const std::uint32_t version = r.ReadU32();
            const std::uint32_t duration = r.ReadU32();
            const std::uint32_t tex_cnt = r.ReadU32();
            const std::uint32_t unk_cnt = r.ReadU32();
            const std::uint32_t model_cnt = r.ReadU32();
            const std::uint32_t action_cnt = r.ReadU32();

            // In synthetic .mv3, bytes 12..24 are inside zero-padded `action_name`, so `model_cnt == 0`
            if (model_cnt > 0u && model_cnt <= 64u && tex_cnt <= 64u && action_cnt <= 128u) {
                Mv3Model mv3;
                mv3.version = version;
                mv3.duration_ticks = duration;
                mv3.events.resize(action_cnt);
                for (std::uint32_t i = 0; i < action_cnt; ++i) {
                    mv3.events[i].timestamp_ticks = r.ReadU32();
                    mv3.events[i].name = r.ReadFixedString(16);
                }
                for (std::uint32_t u = 0; u < unk_cnt; ++u) {
                    r.Seek(r.pos + 64u + 4u);
                    const std::uint32_t u2_cnt = r.ReadU32();
                    r.Seek(r.pos + static_cast<std::size_t>(u2_cnt) * 68u);
                }
                for (std::uint32_t t = 0; t < tex_cnt; ++t) {
                    r.Seek(r.pos + 68u);  // 17 * f32
                    for (int n = 0; n < 4; ++n) {
                        const std::uint32_t slen = r.ReadU32();
                        const std::string tname = r.ReadFixedString(slen);
                        if (mv3.texture_name == "role.tga" && !tname.empty()) {
                            mv3.texture_name = tname;
                        }
                    }
                }
                // Read the first model node
                r.Seek(r.pos + 64u);  // 64B unknown
                const std::uint32_t vtx_per_frame = r.ReadU32();
                r.Seek(r.pos + 24u);  // aabb_min + aabb_max
                const std::uint32_t frame_cnt = r.ReadU32();
                mv3.frames.resize(frame_cnt);
                for (std::uint32_t f = 0; f < frame_cnt; ++f) {
                    mv3.frames[f].timestamp_ticks = r.ReadU32();
                    mv3.frames[f].vertex_positions_q6.resize(vtx_per_frame);
                    for (std::uint32_t v = 0; v < vtx_per_frame; ++v) {
                        const std::int16_t vx = static_cast<std::int16_t>(-r.ReadI16());
                        const std::int16_t vy = r.ReadI16();
                        const std::int16_t vz = static_cast<std::int16_t>(-r.ReadI16());
                        (void)r.ReadU16();  // normal_phi + normal_theta
                        mv3.frames[f].vertex_positions_q6[v] = {vx, vy, vz};
                    }
                }
                const std::uint32_t tc_cnt = r.ReadU32();
                mv3.texcoords.resize(tc_cnt);
                for (std::uint32_t tc = 0; tc < tc_cnt; ++tc) {
                    mv3.texcoords[tc] = {r.ReadF32(), r.ReadF32()};
                }
                const std::uint32_t mesh_cnt = r.ReadU32();
                for (std::uint32_t m = 0; m < mesh_cnt; ++m) {
                    (void)r.ReadU32();  // unknown
                    const std::uint32_t tri_cnt = r.ReadU32();
                    for (std::uint32_t t = 0; t < tri_cnt; ++t) {
                        const std::uint16_t i0 = r.ReadU16();
                        const std::uint16_t i1 = r.ReadU16();
                        const std::uint16_t i2 = r.ReadU16();
                        const std::uint16_t uv0 = r.ReadU16();
                        const std::uint16_t uv1 = r.ReadU16();
                        const std::uint16_t uv2 = r.ReadU16();
                        mv3.triangles.push_back({i0, i1, i2});
                        mv3.triangle_uv_indices.push_back({uv0, uv1, uv2});
                    }
                    const std::uint32_t udata_cnt = r.ReadU32();
                    r.Seek(r.pos + static_cast<std::size_t>(udata_cnt) * 4u);
                }
                return mv3;
            }
        } catch (...) {
            // Fall through to compact synthetic .mv3 parser
        }
    }

    ByteReader r{bytes, 0};
    if (r.ReadFixedString(4) != "MV3") {
        throw std::runtime_error("invalid PAL3 .mv3 magic (expected MV3\\0)");
    }
    Mv3Model mv3;
    mv3.version = r.ReadU32();
    mv3.action_name = r.ReadFixedString(16);
    mv3.texture_name = r.ReadFixedString(64);
    mv3.duration_ticks = r.ReadU32();
    const std::uint32_t ev_cnt = r.ReadU32();
    mv3.events.resize(ev_cnt);
    for (std::uint32_t i = 0; i < ev_cnt; ++i) {
        mv3.events[i].timestamp_ticks = r.ReadU32();
        mv3.events[i].name = r.ReadFixedString(16);
    }
    const std::uint32_t vcnt = r.ReadU32();
    const std::uint32_t fcnt = r.ReadU32();
    mv3.frames.resize(fcnt);
    for (std::uint32_t f = 0; f < fcnt; ++f) {
        mv3.frames[f].timestamp_ticks = r.ReadU32();
        mv3.frames[f].vertex_positions_q6.resize(vcnt);
        for (std::uint32_t v = 0; v < vcnt; ++v) {
            mv3.frames[f].vertex_positions_q6[v] = {r.ReadI16(), r.ReadI16(), r.ReadI16()};
        }
    }
    mv3.texcoords.resize(vcnt);
    for (std::uint32_t v = 0; v < vcnt; ++v) {
        mv3.texcoords[v] = {r.ReadF32(), r.ReadF32()};
    }
    const std::uint32_t tcnt = r.ReadU32();
    mv3.triangles.resize(tcnt);
    for (std::uint32_t t = 0; t < tcnt; ++t) {
        mv3.triangles[t] = {r.ReadU16(), r.ReadU16(), r.ReadU16()};
    }
    return mv3;
}

EvaluatedMv3Pose EvaluateMv3Pose(const Mv3Model& mv3, float time_sec, bool hold_at_mark) {
    EvaluatedMv3Pose pose;
    pose.action_name = mv3.action_name;
    float target_sec = std::max(0.0f, time_sec);
    if (hold_at_mark) {
        if (const auto hold_sec = mv3.HoldMarkSec(); hold_sec && target_sec >= *hold_sec) {
            target_sec = *hold_sec;
            pose.held_at_event = true;
        }
    }
    const float dur = std::max(0.001f, mv3.DurationSec());
    if (!pose.held_at_event && target_sec > dur) {
        target_sec = std::fmod(target_sec, dur);
    }
    pose.time_sec = target_sec;

    if (mv3.frames.empty()) return pose;
    const float target_ticks = target_sec * 4580.0f;

    std::size_t f0 = 0;
    std::size_t f1 = mv3.frames.size() - 1;
    float alpha = 0.0f;
    for (std::size_t i = 0; i + 1 < mv3.frames.size(); ++i) {
        const float t0 = static_cast<float>(mv3.frames[i].timestamp_ticks);
        const float t1 = static_cast<float>(mv3.frames[i + 1].timestamp_ticks);
        if (target_ticks >= t0 && target_ticks <= t1) {
            f0 = i;
            f1 = i + 1;
            alpha = (t1 > t0) ? ((target_ticks - t0) / (t1 - t0)) : 0.0f;
            break;
        }
    }

    constexpr float kMv3Scale = 0.015625f;  // 1/64 fixed-point scale in PAL3 MV3
    const auto& v0 = mv3.frames[f0].vertex_positions_q6;
    const auto& v1 = mv3.frames[f1].vertex_positions_q6;
    const std::size_t vcnt = std::min(v0.size(), v1.size());
    pose.interpolated_positions.resize(vcnt);
    for (std::size_t i = 0; i < vcnt; ++i) {
        for (int c = 0; c < 3; ++c) {
            const float p0 = static_cast<float>(v0[i][c]) * kMv3Scale;
            const float p1 = static_cast<float>(v1[i][c]) * kMv3Scale;
            pose.interpolated_positions[i][c] = p0 + alpha * (p1 - p0);
        }
        pose.pose_checksum += static_cast<double>(
            pose.interpolated_positions[i][0] * 1.1f +
            pose.interpolated_positions[i][1] * 1.3f +
            pose.interpolated_positions[i][2] * 1.7f);
    }
    return pose;
}

pal4::portable::Scene ConvertMv3ToPortableScene(const Mv3Model& mv3,
                                                float time_sec,
                                                bool hold_at_mark) {
    const auto pose = EvaluateMv3Pose(mv3, time_sec, hold_at_mark);
    pal4::portable::Scene scene;
    EnsureDefaultTexture(scene);
    int tex_idx = 0;
    if (mv3.decoded_texture && mv3.decoded_texture->width > 0 && mv3.decoded_texture->height > 0 &&
        !mv3.decoded_texture->rgba.empty()) {
        tex_idx = static_cast<int>(scene.textures.size());
        scene.textures.push_back(*mv3.decoded_texture);
    }
    std::vector<pal4::portable::Vertex> verts(pose.interpolated_positions.size());
    float min_y = std::numeric_limits<float>::infinity();
    float max_y = -std::numeric_limits<float>::infinity();
    float cx = 0.0f;
    float cz = 0.0f;
    for (const auto& p : pose.interpolated_positions) {
        min_y = std::min(min_y, p[1]);
        max_y = std::max(max_y, p[1]);
        cx += p[0];
        cz += p[2];
    }
    if (!pose.interpolated_positions.empty()) {
        cx /= static_cast<float>(pose.interpolated_positions.size());
        cz /= static_cast<float>(pose.interpolated_positions.size());
    }
    const bool is_real_mv3 = (mv3.frames.size() > 3u && (max_y - min_y) > 5.0f);
    const float target_h = (mv3.texture_name.find("109") != std::string::npos) ? 7.5f : 13.5f;
    const float scale = is_real_mv3 ? (target_h / (max_y - min_y)) : 1.0f;
    const float y_off = is_real_mv3 ? min_y : 0.0f;
    const float x_off = is_real_mv3 ? cx : 0.0f;
    const float z_off = is_real_mv3 ? cz : 0.0f;

    for (std::size_t i = 0; i < pose.interpolated_positions.size(); ++i) {
        verts[i].position[0] = (pose.interpolated_positions[i][0] - x_off) * scale;
        verts[i].position[1] = (pose.interpolated_positions[i][1] - y_off) * scale;
        verts[i].position[2] = (pose.interpolated_positions[i][2] - z_off) * scale;
        const auto uv = (i < mv3.texcoords.size()) ? mv3.texcoords[i]
                                                   : std::array<float, 2>{0.0f, 0.0f};
        verts[i].uv0[0] = uv[0];
        verts[i].uv0[1] = uv[1];
        verts[i].uv1[0] = uv[0];
        verts[i].uv1[1] = uv[1];
        for (int c = 0; c < 4; ++c) verts[i].color[c] = 1.0f;
    }
    const auto* custom_uvs =
        (mv3.triangle_uv_indices.size() == mv3.triangles.size()) ? &mv3.triangle_uv_indices : nullptr;
    AppendMeshDrawToScene(scene, verts, mv3.triangles, tex_idx, custom_uvs, &mv3.texcoords);
    return scene;
}

}  // namespace pal3::portable
