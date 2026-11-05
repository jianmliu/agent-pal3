#include "pal3/portable/scn_nav_model.h"

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
        return std::string(begin, len);
    }
};

}  // namespace

// ============================================================================
// 1. .scn Serialization & Parsing (exact offsets matching OpenPAL3 `scn_loader.rs`)
// ============================================================================

std::vector<std::uint8_t> SerializeScnFile(const ScnFile& scn) {
    std::vector<std::uint8_t> out;
    // Magic "SCN\0" + version(u16=1) + pad(u16=0)
    out.insert(out.end(), {'S', 'C', 'N', '\0'});
    WriteU16(out, 1u);
    WriteU16(out, 0u);
    WriteU16(out, static_cast<std::uint16_t>(scn.roles.size()));
    WriteU16(out, 0u);
    const std::size_t role_offset_pos = out.size();
    WriteU32(out, 0u);
    WriteU16(out, static_cast<std::uint16_t>(scn.nodes.size()));
    WriteU16(out, 0u);
    const std::size_t node_offset_pos = out.size();
    WriteU32(out, 0u);

    WriteFixedString(out, scn.cpk_name, 32);
    WriteFixedString(out, scn.scn_name, 32);
    WriteFixedString(out, scn.scn_base_name, 32);
    WriteU32(out, scn.is_night);
    WriteU32(out, scn.skybox_id);

    // Roles table (each 820 bytes matching OpenPAL3 `read_scn_roles`)
    const std::uint32_t role_offset = static_cast<std::uint32_t>(out.size());
    WriteU32At(out, role_offset_pos, role_offset);
    for (const auto& r : scn.roles) {
        const std::size_t r_start = out.size();
        out.resize(r_start + 820, 0u);
        out[r_start + 0] = r.index;
        const std::size_t nlen = std::min<std::size_t>(r.name.size(), 63);
        std::memcpy(out.data() + r_start + 4, r.name.data(), nlen);

        auto write_f32_at = [&](std::size_t off, float val) {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &val, 4);
            WriteU32At(out, r_start + off, bits);
        };
        write_f32_at(0x44, r.facing_deg);
        WriteU32At(out, r_start + 0x48, r.nav_layer);
        write_f32_at(0x4C, r.position_x);
        write_f32_at(0x54, r.position_z);
        WriteU32At(out, r_start + 0x5C, r.sce_proc_id);

        const std::size_t alen = std::min<std::size_t>(r.action_name.size(), 15);
        std::memcpy(out.data() + r_start + 0x70, r.action_name.data(), alen);
        WriteU32At(out, r_start + 0x80, r.path_mode);

        const std::uint32_t wp_cnt =
            static_cast<std::uint32_t>(std::min<std::size_t>(r.patrol_path.size(), 16));
        WriteU32At(out, r_start + 0x84, wp_cnt);
        for (std::size_t w = 0; w < wp_cnt; ++w) {
            write_f32_at(0x88 + w * 12 + 0, r.patrol_path[w][0]);
            write_f32_at(0x88 + w * 12 + 4, r.patrol_path[w][1]);
            write_f32_at(0x88 + w * 12 + 8, r.patrol_path[w][2]);
        }
        write_f32_at(0x150, r.move_speed);
    }

    // Nodes table (each 620 bytes matching OpenPAL3 `read_scn_nodes`)
    const std::uint32_t node_offset = static_cast<std::uint32_t>(out.size());
    WriteU32At(out, node_offset_pos, node_offset);
    for (const auto& n : scn.nodes) {
        const std::size_t n_start = out.size();
        out.resize(n_start + 620, 0u);
        out[n_start + 0] = n.index;
        out[n_start + 1] = n.active;
        out[n_start + 2] = n.times;
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

        out[n_start + 0x4E] = static_cast<std::uint8_t>(n.node_type & 0xFFu);
        out[n_start + 0x4F] = static_cast<std::uint8_t>((n.node_type >> 8u) & 0xFFu);
        WriteU32At(out, n_start + 0xAC, n.nav_layer);
        WriteU32At(out, n_start + 0xB0, static_cast<std::uint32_t>(n.ladder_nav_coord1[0]));
        WriteU32At(out, n_start + 0xB4, static_cast<std::uint32_t>(n.ladder_nav_coord1[1]));
        WriteU32At(out, n_start + 0xB8, static_cast<std::uint32_t>(n.ladder_nav_coord2[0]));
        WriteU32At(out, n_start + 0xBC, static_cast<std::uint32_t>(n.ladder_nav_coord2[1]));
        WriteU32At(out, n_start + 0xC0, static_cast<std::uint32_t>(n.ladder_switch_layer));
        WriteU32At(out, n_start + 0xFC, n.sce_proc_id);

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
    (void)r.ReadU16();
    if (version != 1u) {
        throw std::runtime_error("unsupported PAL3 .scn version");
    }
    const std::uint16_t role_num = r.ReadU16();
    (void)r.ReadU16();
    const std::uint32_t role_offset = r.ReadU32();
    const std::uint16_t node_num = r.ReadU16();
    (void)r.ReadU16();
    const std::uint32_t node_offset = r.ReadU32();

    ScnFile scn;
    scn.cpk_name = r.ReadFixedString(32);
    scn.scn_name = r.ReadFixedString(32);
    scn.scn_base_name = r.ReadFixedString(32);
    scn.is_night = r.ReadU32();
    scn.skybox_id = r.ReadU32();

    scn.roles.reserve(role_num);
    for (std::uint16_t i = 0; i < role_num; ++i) {
        const std::size_t base = static_cast<std::size_t>(role_offset) + static_cast<std::size_t>(i) * 820u;
        ScnRole role;
        r.Seek(base + 0);
        role.index = r.ReadU8();
        r.Seek(base + 4);
        role.name = r.ReadFixedString(64);
        r.Seek(base + 0x44);
        role.facing_deg = r.ReadF32();
        role.nav_layer = r.ReadU32();
        role.position_x = r.ReadF32();
        r.Seek(base + 0x54);
        role.position_z = r.ReadF32();
        r.Seek(base + 0x5C);
        role.sce_proc_id = r.ReadU32();
        r.Seek(base + 0x70);
        role.action_name = r.ReadFixedString(16);
        r.Seek(base + 0x80);
        role.path_mode = r.ReadU32();
        const std::uint32_t wp_cnt = std::min<std::uint32_t>(r.ReadU32(), 16u);
        for (std::uint32_t w = 0; w < wp_cnt; ++w) {
            role.patrol_path.push_back({r.ReadF32(), r.ReadF32(), r.ReadF32()});
        }
        r.Seek(base + 0x150);
        role.move_speed = r.ReadF32();
        if (role.move_speed <= 0.0f) role.move_speed = 215.0f;
        scn.roles.push_back(std::move(role));
    }

    scn.nodes.reserve(node_num);
    for (std::uint16_t i = 0; i < node_num; ++i) {
        const std::size_t base = static_cast<std::size_t>(node_offset) + static_cast<std::size_t>(i) * 620u;
        ScnNode node;
        r.Seek(base + 0);
        node.index = r.ReadU8();
        node.active = r.ReadU8();
        node.times = r.ReadU8();
        node.switch_state = r.ReadU8();
        node.name = r.ReadFixedString(32);
        r.Seek(base + 0x26);
        const std::uint16_t w26 = r.ReadU16();
        node.position[0] = r.ReadF32();
        node.position[1] = r.ReadF32();
        node.position[2] = r.ReadF32();
        node.rotation_deg = r.ReadF32();
        node.nav_trigger_coord_min[0] = r.ReadI32();
        node.nav_trigger_coord_min[1] = r.ReadI32();
        node.nav_trigger_coord_max[0] = r.ReadI32();
        node.nav_trigger_coord_max[1] = r.ReadI32();
        r.Seek(base + 0x4E);
        const std::uint16_t w4e = r.ReadU16();
        node.node_type = (w4e != 0u) ? w4e : w26;
        r.Seek(base + 0xAC);
        node.nav_layer = r.ReadU32();
        node.ladder_nav_coord1[0] = r.ReadI32();
        node.ladder_nav_coord1[1] = r.ReadI32();
        node.ladder_nav_coord2[0] = r.ReadI32();
        node.ladder_nav_coord2[1] = r.ReadI32();
        node.ladder_switch_layer = r.ReadI32();
        r.Seek(base + 0xFC);
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
    WriteU16(out, 0u);
    const std::size_t off1_pos = out.size();
    WriteU32(out, 0u);
    const std::size_t off2_pos = out.size();
    WriteU32(out, 0u);

    for (std::size_t li = 0; li < nav.layers.size(); ++li) {
        const auto& layer = nav.layers[li];
        const std::uint32_t layer_off = static_cast<std::uint32_t>(out.size());
        WriteU32At(out, (li == 0) ? off1_pos : off2_pos, layer_off);

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
    (void)r.ReadU16();
    const std::uint32_t offset1 = r.ReadU32();
    const std::uint32_t offset2 = r.ReadU32();

    if ((nav.version != 1u && nav.version != 2u) || (count != 1u && count != 2u)) {
        throw std::runtime_error("unsupported PAL3 .nav version or layer count");
    }

    auto read_layer = [&](std::uint32_t offset) {
        r.Seek(offset);
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

    nav.layers.push_back(read_layer(offset1));
    if (count == 2u) {
        nav.layers.push_back(read_layer(offset2));
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
                           const std::vector<std::array<std::uint16_t, 3>>& triangles) {
    if (vertices.empty() || triangles.empty()) return;
    EnsureDefaultTexture(scene);
    const bool bounds_was_empty = scene.vertices.empty();

    pal4::portable::Draw draw{};
    draw.first = static_cast<std::uint32_t>(scene.vertices.size());
    draw.count = static_cast<std::uint32_t>(triangles.size() * 3u);
    draw.diffuse = 0;
    draw.lightmap = -1;
    for (int c = 0; c < 4; ++c) draw.color[c] = 1.0f;

    std::array<float, 3> center_sum{0.0f, 0.0f, 0.0f};
    bool first_v = bounds_was_empty;
    for (const auto& tri : triangles) {
        for (int k = 0; k < 3; ++k) {
            const auto& v = vertices.at(tri[k]);
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
    const float inv = 1.0f / static_cast<float>(draw.count);
    scene.atomic_centers.push_back({center_sum[0] * inv, center_sum[1] * inv, center_sum[2] * inv});
    scene.draws.push_back(draw);
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

pal4::portable::Scene ConvertPolToPortableScene(const PolModel& pol) {
    pal4::portable::Scene scene;
    EnsureDefaultTexture(scene);
    for (const auto& sm : pol.meshes) {
        AppendMeshDrawToScene(scene, sm.vertices, sm.triangles);
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
    ByteReader r{bytes, 0};
    const std::string magic = r.ReadFixedString(4);
    if (magic != "cvdf" && magic != "cvds") {
        throw std::runtime_error("invalid PAL3 .cvd magic (expected cvdf or cvds)");
    }
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
    std::vector<pal4::portable::Vertex> verts(pose.interpolated_positions.size());
    for (std::size_t i = 0; i < pose.interpolated_positions.size(); ++i) {
        for (int c = 0; c < 3; ++c) verts[i].position[c] = pose.interpolated_positions[i][c];
        const auto uv = (i < mv3.texcoords.size()) ? mv3.texcoords[i]
                                                   : std::array<float, 2>{0.0f, 0.0f};
        verts[i].uv0[0] = uv[0];
        verts[i].uv0[1] = uv[1];
        verts[i].uv1[0] = uv[0];
        verts[i].uv1[1] = uv[1];
        for (int c = 0; c < 4; ++c) verts[i].color[c] = 1.0f;
    }
    AppendMeshDrawToScene(scene, verts, mv3.triangles);
    return scene;
}

}  // namespace pal3::portable
