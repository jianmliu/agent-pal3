#include "pal3/portable/cpk_package.h"

#include "minilzo.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>

namespace pal3::portable {
namespace {

constexpr std::uint32_t kCpkMagic = 0x1A545352u;  // "RST\x1A"
constexpr std::uint32_t kFlagValid = 0x00000001u;
constexpr std::uint32_t kFlagDir = 0x00000002u;
constexpr std::uint32_t kFlagFileCompressed = 0x00000005u;
constexpr std::uint32_t kFlagFileRaw = 0x00010005u;
constexpr std::uint32_t kFlagDirEntry = 0x00000003u;
constexpr std::uint32_t kFlagDeleted = 0x00000010u;

// Exact 256-entry CRC table from `OpenPAL3/yaobow/packfs/src/cpk/crc.rs`
constexpr std::array<std::uint32_t, 256> kPal3CrcTable = {
    0u, 79764919u, 159529838u, 222504665u, 319059676u, 398814059u, 445009330u, 507990021u, 638119352u,
    583659535u, 797628118u, 726387553u, 890018660u, 835552979u, 1015980042u, 944750013u, 1276238704u,
    1221641927u, 1167319070u, 1095957929u, 1595256236u, 1540665371u, 1452775106u, 1381403509u, 1780037320u,
    1859660671u, 1671105958u, 1733955601u, 2031960084u, 2111593891u, 1889500026u, 1952343757u, 2552477408u,
    2632100695u, 2443283854u, 2506133561u, 2334638140u, 2414271883u, 2191915858u, 2254759653u, 3190512472u,
    3135915759u, 3081330742u, 3009969537u, 2905550212u, 2850959411u, 2762807018u, 2691435357u, 3560074640u,
    3505614887u, 3719321342u, 3648080713u, 3342211916u, 3287746299u, 3467911202u, 3396681109u, 4063920168u,
    4143685023u, 4223187782u, 4286162673u, 3779000052u, 3858754371u, 3904687514u, 3967668269u, 881225847u,
    809987520u, 1023691545u, 969234094u, 662832811u, 591600412u, 771767749u, 717299826u, 311336399u,
    374308984u, 453813921u, 533576470u, 25881363u, 88864420u, 134795389u, 214552010u, 2023205639u,
    2086057648u, 1897238633u, 1976864222u, 1804852699u, 1867694188u, 1645340341u, 1724971778u, 1587496639u,
    1516133128u, 1461550545u, 1406951526u, 1302016099u, 1230646740u, 1142491917u, 1087903418u, 2896545431u,
    2825181984u, 2770861561u, 2716262478u, 3215044683u, 3143675388u, 3055782693u, 3001194130u, 2326604591u,
    2389456536u, 2200899649u, 2280525302u, 2578013683u, 2640855108u, 2418763421u, 2498394922u, 3769900519u,
    3832873040u, 3912640137u, 3992402750u, 4088425275u, 4151408268u, 4197601365u, 4277358050u, 3334271071u,
    3263032808u, 3476998961u, 3422541446u, 3585640067u, 3514407732u, 3694837229u, 3640369242u, 1762451694u,
    1842216281u, 1619975040u, 1682949687u, 2047383090u, 2127137669u, 1938468188u, 2001449195u, 1325665622u,
    1271206113u, 1183200824u, 1111960463u, 1543535498u, 1489069629u, 1434599652u, 1363369299u, 622672798u,
    568075817u, 748617968u, 677256519u, 907627842u, 853037301u, 1067152940u, 995781531u, 51762726u,
    131386257u, 177728840u, 240578815u, 269590778u, 349224269u, 429104020u, 491947555u, 4046411278u,
    4126034873u, 4172115296u, 4234965207u, 3794477266u, 3874110821u, 3953728444u, 4016571915u, 3609705398u,
    3555108353u, 3735388376u, 3664026991u, 3290680682u, 3236090077u, 3449943556u, 3378572211u, 3174993278u,
    3120533705u, 3032266256u, 2961025959u, 2923101090u, 2868635157u, 2813903052u, 2742672763u, 2604032198u,
    2683796849u, 2461293480u, 2524268063u, 2284983834u, 2364738477u, 2175806836u, 2238787779u, 1569362073u,
    1498123566u, 1409854455u, 1355396672u, 1317987909u, 1246755826u, 1192025387u, 1137557660u, 2072149281u,
    2135122070u, 1912620623u, 1992383480u, 1753615357u, 1816598090u, 1627664531u, 1707420964u, 295390185u,
    358241886u, 404320391u, 483945776u, 43990325u, 106832002u, 186451547u, 266083308u, 932423249u,
    861060070u, 1041341759u, 986742920u, 613929101u, 542559546u, 756411363u, 701822548u, 3316196985u,
    3244833742u, 3425377559u, 3370778784u, 3601682597u, 3530312978u, 3744426955u, 3689838204u, 3819031489u,
    3881883254u, 3928223919u, 4007849240u, 4037393693u, 4100235434u, 4180117107u, 4259748804u, 2310601993u,
    2373574846u, 2151335527u, 2231098320u, 2596047829u, 2659030626u, 2470359227u, 2550115596u, 2947551409u,
    2876312838u, 2788305887u, 2733848168u, 3165939309u, 3094707162u, 3040238851u, 2985771188u,
};

void WriteU32At(std::vector<std::uint8_t>& buf, std::size_t offset, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) {
        buf[offset + i] = static_cast<std::uint8_t>((v >> (i * 8u)) & 0xFFu);
    }
}

std::uint32_t ReadU32(std::istream& s) {
    std::uint8_t b[4]{};
    if (!s.read(reinterpret_cast<char*>(b), 4)) {
        throw std::runtime_error("unexpected EOF reading u32 from PAL3 .cpk");
    }
    return static_cast<std::uint32_t>(b[0]) |
           (static_cast<std::uint32_t>(b[1]) << 8u) |
           (static_cast<std::uint32_t>(b[2]) << 16u) |
           (static_cast<std::uint32_t>(b[3]) << 24u);
}

std::vector<std::string> SplitPathSegments(std::string_view norm_path) {
    std::vector<std::string> segments;
    std::size_t start = 0;
    while (start < norm_path.size()) {
        auto slash = norm_path.find('/', start);
        if (slash == std::string_view::npos) {
            segments.emplace_back(norm_path.substr(start));
            break;
        }
        if (slash > start) {
            segments.emplace_back(norm_path.substr(start, slash - start));
        }
        start = slash + 1;
    }
    return segments;
}

}  // namespace

std::string NormalizePal3CpkPath(std::string_view path) {
    std::string out;
    out.reserve(path.size());
    for (unsigned char c : path) {
        if (c == '\\') {
            if (out.empty() || out.back() != '/') out.push_back('/');
        } else if (c == '/') {
            if (out.empty() || out.back() != '/') out.push_back('/');
        } else {
            out.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    while (!out.empty() && out.front() == '/') out.erase(out.begin());
    while (!out.empty() && out.back() == '/') out.pop_back();
    return out;
}

std::uint32_t ComputePal3CpkCrc(std::string_view segment) {
    std::string lower;
    lower.reserve(segment.size());
    for (unsigned char c : segment) {
        if (c == '/') {
            lower.push_back('\\');
        } else {
            lower.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    if (lower.empty()) {
        return 0u;
    }

    std::uint32_t initial = 0u;
    for (std::size_t i = 0; i < 4; ++i) {
        const std::uint8_t byte = (i < lower.size()) ? static_cast<std::uint8_t>(lower[i]) : 0u;
        initial = (initial << 8u) | static_cast<std::uint32_t>(byte);
    }

    std::uint32_t value = ~initial;
    for (std::size_t i = 4; i < lower.size(); ++i) {
        const std::uint8_t byte = static_cast<std::uint8_t>(lower[i]);
        value = kPal3CrcTable[(value >> 24u) & 0xFFu] ^
                (static_cast<std::uint32_t>(byte) | (value << 8u));
    }
    return ~value;
}

std::vector<std::uint8_t> BuildPal3CpkArchive(std::span<const CpkArchiveBuilderEntry> entries) {
    static bool lzo_initialized = false;
    if (!lzo_initialized) {
        if (lzo_init() != LZO_E_OK) {
            throw std::runtime_error("failed to initialize miniLZO for PAL3 .cpk builder");
        }
        lzo_initialized = true;
    }

    struct DraftNode {
        std::string full_path;
        std::string segment_name;
        std::uint32_t crc{};
        std::uint32_t father_crc{};
        bool is_dir{false};
        CpkCodec codec{CpkCodec::raw};
        std::vector<std::uint8_t> raw_data;
    };

    std::map<std::string, DraftNode> nodes_by_path;

    // 1. Automatically synthesize intermediate directory entries (`father_crc` tree)
    for (const auto& entry : entries) {
        const std::string norm = NormalizePal3CpkPath(entry.path);
        const auto segments = SplitPathSegments(norm);
        if (segments.empty()) continue;

        std::string prefix;
        std::uint32_t parent_crc = 0u;
        for (std::size_t i = 0; i + 1 < segments.size(); ++i) {
            prefix = prefix.empty() ? segments[i] : (prefix + "/" + segments[i]);
            const std::uint32_t dir_crc = ComputePal3CpkCrc(segments[i]);
            if (nodes_by_path.find(prefix) == nodes_by_path.end()) {
                DraftNode dir_node;
                dir_node.full_path = prefix;
                dir_node.segment_name = segments[i];
                dir_node.crc = dir_crc;
                dir_node.father_crc = parent_crc;
                dir_node.is_dir = true;
                dir_node.codec = CpkCodec::dir;
                nodes_by_path.emplace(prefix, std::move(dir_node));
            }
            parent_crc = dir_crc;
        }

        DraftNode file_node;
        file_node.full_path = norm;
        file_node.segment_name = segments.back();
        file_node.crc = ComputePal3CpkCrc(segments.back());
        file_node.father_crc = parent_crc;
        file_node.is_dir = false;
        file_node.codec = entry.codec;
        file_node.raw_data = entry.data;
        nodes_by_path[norm] = std::move(file_node);
    }

    std::vector<DraftNode> sorted_nodes;
    sorted_nodes.reserve(nodes_by_path.size());
    for (auto& [_, node] : nodes_by_path) {
        sorted_nodes.push_back(std::move(node));
    }
    // Sort by CRC32 ascending as required by gbEngine binary search (`CpkArchive::find_entry`)
    std::stable_sort(sorted_nodes.begin(), sorted_nodes.end(),
                     [](const DraftNode& a, const DraftNode& b) {
                         if (a.crc != b.crc) return a.crc < b.crc;
                         return a.father_crc < b.father_crc;
                     });

    const std::uint32_t valid_count = static_cast<std::uint32_t>(sorted_nodes.size());
    const std::uint32_t table_start = 0x80u;
    const std::uint32_t data_start = table_start + valid_count * 28u;

    std::vector<std::uint8_t> out(data_start, 0u);
    std::vector<std::uint8_t> workmem(LZO1X_1_MEM_COMPRESS, 0u);

    for (std::size_t idx = 0; idx < sorted_nodes.size(); ++idx) {
        const auto& node = sorted_nodes[idx];
        CpkTableEntry tbl{};
        tbl.crc = node.crc;
        tbl.father_crc = node.father_crc;
        tbl.start_pos = static_cast<std::uint32_t>(out.size());

        if (node.is_dir) {
            tbl.flag = kFlagDirEntry;
            tbl.origin_size = 0u;
            tbl.packed_size = 0u;
        } else {
            tbl.origin_size = static_cast<std::uint32_t>(node.raw_data.size());
            if (node.codec == CpkCodec::lzo && !node.raw_data.empty()) {
                std::vector<std::uint8_t> compressed(
                    node.raw_data.size() + node.raw_data.size() / 16u + 64u + 3u, 0u);
                lzo_uint out_len = 0;
                const int rc = lzo1x_1_compress(
                    node.raw_data.data(), static_cast<lzo_uint>(node.raw_data.size()),
                    compressed.data(), &out_len, workmem.data());
                if (rc == LZO_E_OK && out_len < node.raw_data.size()) {
                    tbl.flag = kFlagFileCompressed;
                    tbl.packed_size = static_cast<std::uint32_t>(out_len);
                    out.insert(out.end(), compressed.begin(),
                               compressed.begin() + static_cast<std::ptrdiff_t>(out_len));
                } else {
                    tbl.flag = kFlagFileRaw;
                    tbl.packed_size = tbl.origin_size;
                    out.insert(out.end(), node.raw_data.begin(), node.raw_data.end());
                }
            } else {
                tbl.flag = kFlagFileRaw;
                tbl.packed_size = tbl.origin_size;
                out.insert(out.end(), node.raw_data.begin(), node.raw_data.end());
            }
        }

        // Write trailing null-terminated `extra_info` segment name immediately after `start_pos + packed_size`
        tbl.extra_info_size = static_cast<std::uint32_t>(node.segment_name.size() + 1u);
        out.insert(out.end(), node.segment_name.begin(), node.segment_name.end());
        out.push_back(0u);

        const std::size_t entry_off = table_start + idx * 28u;
        WriteU32At(out, entry_off + 0u, tbl.crc);
        WriteU32At(out, entry_off + 4u, tbl.flag);
        WriteU32At(out, entry_off + 8u, tbl.father_crc);
        WriteU32At(out, entry_off + 12u, tbl.start_pos);
        WriteU32At(out, entry_off + 16u, tbl.packed_size);
        WriteU32At(out, entry_off + 20u, tbl.origin_size);
        WriteU32At(out, entry_off + 24u, tbl.extra_info_size);
    }

    // Populate 128-byte CpkHeader
    WriteU32At(out, 0u, kCpkMagic);
    WriteU32At(out, 4u, 1u);                // version = 1
    WriteU32At(out, 8u, table_start);       // table_start = 0x80
    WriteU32At(out, 12u, data_start);       // data_start
    WriteU32At(out, 16u, 0x8000u);          // max_file_num = 32768
    WriteU32At(out, 20u, valid_count);      // file_num
    WriteU32At(out, 24u, 0u);               // is_formatted
    WriteU32At(out, 28u, 0x80u);            // size_of_header
    WriteU32At(out, 32u, valid_count);      // valid_table_num
    WriteU32At(out, 36u, 0x8000u);          // max_table_num
    WriteU32At(out, 40u, 0u);               // fragment_num
    WriteU32At(out, 44u, static_cast<std::uint32_t>(out.size()));  // package_size

    return out;
}

CpkPackageReader::CpkPackageReader(std::istream& stream) : stream_(stream) {
    stream_.seekg(0, std::ios::end);
    file_bytes_ = static_cast<std::uint64_t>(stream_.tellg());
    if (file_bytes_ < 128u) {
        throw std::runtime_error("PAL3 .cpk file smaller than 128-byte CpkHeader");
    }

    stream_.seekg(0, std::ios::beg);
    header_.label = ReadU32(stream_);
    header_.version = ReadU32(stream_);
    header_.table_start = ReadU32(stream_);
    header_.data_start = ReadU32(stream_);
    header_.max_file_num = ReadU32(stream_);
    header_.file_num = ReadU32(stream_);
    header_.is_formatted = ReadU32(stream_);
    header_.size_of_header = ReadU32(stream_);
    header_.valid_table_num = ReadU32(stream_);
    header_.max_table_num = ReadU32(stream_);
    header_.fragment_num = ReadU32(stream_);
    header_.package_size = ReadU32(stream_);

    if (header_.label != kCpkMagic) {
        throw std::runtime_error("invalid PAL3 .cpk magic (expected RST\\x1A / 0x1A545352)");
    }
    if (header_.version != 1u) {
        throw std::runtime_error("unsupported PAL3 .cpk version");
    }

    const std::uint32_t table_offset = (header_.table_start >= 128u) ? header_.table_start : 128u;
    const std::uint64_t max_possible_entries = (file_bytes_ - table_offset) / 28u;
    const std::uint32_t declared_entries = std::max(header_.file_num, header_.valid_table_num);
    const std::uint32_t count = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(declared_entries, max_possible_entries));

    stream_.seekg(static_cast<std::streamoff>(table_offset), std::ios::beg);
    std::vector<CpkTableEntry> raw_entries(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        raw_entries[i].crc = ReadU32(stream_);
        raw_entries[i].flag = ReadU32(stream_);
        raw_entries[i].father_crc = ReadU32(stream_);
        raw_entries[i].start_pos = ReadU32(stream_);
        raw_entries[i].packed_size = ReadU32(stream_);
        raw_entries[i].origin_size = ReadU32(stream_);
        raw_entries[i].extra_info_size = ReadU32(stream_);
    }

    members_.reserve(count);
    std::unordered_map<std::uint32_t, std::size_t> dir_by_crc;
    constexpr std::uint32_t kFlagNotCompressed = 0x00010000u;

    for (const auto& e : raw_entries) {
        if ((e.flag & (kFlagValid | kFlagDir)) == 0u || (e.flag & kFlagDeleted) != 0u) {
            continue;
        }
        CpkMember m;
        m.crc = e.crc;
        m.father_crc = e.father_crc;
        m.flag = e.flag;
        m.start_pos = e.start_pos;
        m.packed_size = e.packed_size;
        m.origin_size = e.origin_size;
        m.extra_info_size = e.extra_info_size;
        m.is_directory = (e.flag & kFlagDir) != 0u || (e.flag == 0x00010001u);
        if (m.is_directory) {
            m.codec = CpkCodec::dir;
        } else if ((e.flag & kFlagNotCompressed) != 0u) {
            m.codec = CpkCodec::raw;
        } else {
            m.codec = CpkCodec::lzo;
        }

        const std::uint64_t extra_pos =
            static_cast<std::uint64_t>(e.start_pos) + static_cast<std::uint64_t>(e.packed_size);
        if (e.extra_info_size > 0u && extra_pos + e.extra_info_size <= file_bytes_) {
            stream_.seekg(static_cast<std::streamoff>(extra_pos), std::ios::beg);
            std::vector<char> name_buf(e.extra_info_size, '\0');
            stream_.read(name_buf.data(), static_cast<std::streamsize>(e.extra_info_size));
            const auto nul = std::find(name_buf.begin(), name_buf.end(), '\0');
            m.name.assign(name_buf.begin(), nul);
            for (char& ch : m.name) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
        }

        const std::size_t idx = members_.size();
        members_.push_back(std::move(m));
        if (members_[idx].is_directory) {
            dir_by_crc[members_[idx].crc] = idx;
        }
    }

    // Reconstruct full hierarchical paths via `father_crc` chain
    for (std::size_t i = 0; i < members_.size(); ++i) {
        std::vector<std::string> chain;
        if (!members_[i].name.empty()) {
            chain.push_back(members_[i].name);
        }
        std::uint32_t parent = members_[i].father_crc;
        int depth_guard = 0;
        while (parent != 0u && depth_guard++ < 32) {
            auto it = dir_by_crc.find(parent);
            if (it == dir_by_crc.end()) break;
            const auto& dir_member = members_[it->second];
            if (!dir_member.name.empty()) {
                chain.push_back(dir_member.name);
            }
            parent = dir_member.father_crc;
        }
        std::reverse(chain.begin(), chain.end());
        std::string full;
        for (const auto& seg : chain) {
            full = full.empty() ? seg : (full + "/" + seg);
        }
        members_[i].path = full;
        if (!full.empty()) {
            by_path_[full] = i;
        }
    }
}

std::optional<std::size_t> CpkPackageReader::FindByHierarchy(
    std::string_view normalized_path) const {
    const auto segments = SplitPathSegments(normalized_path);
    if (segments.empty()) return std::nullopt;

    std::uint32_t father_crc = 0u;
    std::optional<std::size_t> found_idx;
    for (const auto& seg : segments) {
        const std::uint32_t seg_crc = ComputePal3CpkCrc(seg);
        // Binary search by `crc` in `members_` (exactly matching OpenPAL3 `CpkArchive::find_entry`)
        auto lower = std::lower_bound(
            members_.begin(), members_.end(), seg_crc,
            [](const CpkMember& m, std::uint32_t target_crc) { return m.crc < target_crc; });
        bool matched = false;
        for (auto it = lower; it != members_.end() && it->crc == seg_crc; ++it) {
            if (it->father_crc == father_crc) {
                found_idx = static_cast<std::size_t>(std::distance(members_.begin(), it));
                father_crc = it->crc;
                matched = true;
                break;
            }
        }
        if (!matched) {
            return std::nullopt;
        }
    }
    return found_idx;
}

std::optional<CpkMember> CpkPackageReader::Find(std::string_view path) const {
    const std::string norm = NormalizePal3CpkPath(path);
    auto it = by_path_.find(norm);
    if (it != by_path_.end()) {
        return members_[it->second];
    }
    if (auto idx = FindByHierarchy(norm)) {
        return members_[*idx];
    }
    const std::uint32_t full_crc = ComputePal3CpkCrc(norm);
    for (const auto& m : members_) {
        if (m.crc == full_crc) {
            return m;
        }
    }
    return std::nullopt;
}

std::vector<std::string> CpkPackageReader::ListFiles() const {
    std::vector<std::string> out;
    for (const auto& m : members_) {
        if (!m.is_directory && !m.path.empty()) {
            out.push_back(m.path);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> CpkPackageReader::ListDirectories() const {
    std::vector<std::string> out;
    for (const auto& m : members_) {
        if (m.is_directory && !m.path.empty()) {
            out.push_back(m.path);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::uint8_t> CpkPackageReader::Read(std::string_view path, std::size_t limit) {
    auto found = Find(path);
    if (!found) {
        throw std::runtime_error("file not found in PAL3 .cpk: " + std::string(path));
    }
    if (found->is_directory) {
        throw std::runtime_error("cannot Read() a directory entry in PAL3 .cpk: " + std::string(path));
    }
    if (found->origin_size > limit) {
        throw std::runtime_error("PAL3 .cpk member exceeds size limit: " + std::string(path));
    }
    if (found->origin_size == 0u) {
        return {};
    }

    std::vector<std::uint8_t> packed(found->packed_size);
    stream_.clear();
    stream_.seekg(static_cast<std::streamoff>(found->start_pos), std::ios::beg);
    if (!stream_.read(reinterpret_cast<char*>(packed.data()),
                      static_cast<std::streamsize>(packed.size()))) {
        throw std::runtime_error("failed reading packed payload from PAL3 .cpk: " + std::string(path));
    }

    if (found->codec == CpkCodec::raw) {
        return packed;
    }

    std::vector<std::uint8_t> decoded(found->origin_size);
    lzo_uint out_len = static_cast<lzo_uint>(decoded.size());
    const int rc = lzo1x_decompress_safe(
        packed.data(), static_cast<lzo_uint>(packed.size()),
        decoded.data(), &out_len, nullptr);
    if (rc != LZO_E_OK || out_len != decoded.size()) {
        if (found->packed_size == found->origin_size) {
            return packed;
        }
        throw std::runtime_error("LZO1X decompression failed for PAL3 .cpk entry: " + std::string(path));
    }
    return decoded;
}

std::optional<std::vector<std::uint8_t>> CpkPackageReader::ReadIfPresent(
    std::string_view path, std::size_t limit) {
    if (!Find(path)) return std::nullopt;
    return Read(path, limit);
}

std::string CpkPackageReader::ReadText(std::string_view path, std::size_t limit) {
    const auto bytes = Read(path, limit);
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

}  // namespace pal3::portable
