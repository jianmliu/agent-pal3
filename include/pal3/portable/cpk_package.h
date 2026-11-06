#pragma once

#include <cstdint>
#include <istream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pal3::portable {

// PAL3 CPK entry compression/storage mode (matching OpenPAL3 `CpkEntryFlag`)
enum class CpkCodec : std::uint32_t {
    raw = 1,   // Flag 0x00010005 (uncompressed file)
    lzo = 2,   // Flag 0x00000005 (LZO1X compressed file)
    dir = 3,   // Flag 0x00000003 / 0x00010001 (directory node)
};

// Exact 128-byte header from gbEngine `CpkHeader` (`OpenPAL3/yaobow/packfs/src/cpk/cpk_archive.rs`)
struct CpkHeader {
    std::uint32_t label{0x1A545352u};  // "RST\x1A"
    std::uint32_t version{1u};
    std::uint32_t table_start{0x80u};
    std::uint32_t data_start{};
    std::uint32_t max_file_num{0x8000u};
    std::uint32_t file_num{};
    std::uint32_t is_formatted{0u};
    std::uint32_t size_of_header{0x80u};
    std::uint32_t valid_table_num{};
    std::uint32_t max_table_num{0x8000u};
    std::uint32_t fragment_num{0u};
    std::uint32_t package_size{};
};

// Exact 28-byte unencrypted table entry from PAL3 `CpkTable` (contrasted with PAL4's 32-byte XXTEA table)
struct CpkTableEntry {
    std::uint32_t crc{};
    std::uint32_t flag{};
    std::uint32_t father_crc{};
    std::uint32_t start_pos{};
    std::uint32_t packed_size{};
    std::uint32_t origin_size{};
    std::uint32_t extra_info_size{};
};

struct CpkMember {
    std::string path;          // Full normalized path, e.g. "q01/yn01a/yn01a.nav"
    std::string name;          // Segment filename from trailing extra_info
    std::uint32_t crc{};
    std::uint32_t father_crc{};
    std::uint32_t flag{};
    std::uint32_t start_pos{};
    std::uint32_t packed_size{};
    std::uint32_t origin_size{};
    std::uint32_t extra_info_size{};
    CpkCodec codec{CpkCodec::raw};
    bool is_directory{false};
};

struct CpkArchiveBuilderEntry {
    std::string path;
    std::vector<std::uint8_t> data;
    CpkCodec codec{CpkCodec::lzo};
};

// Computes PAL3 gbEngine custom big-endian CRC32 on lowercase ASCII/GBK bytes (`OpenPAL3/yaobow/packfs/src/cpk/crc.rs`)
std::uint32_t ComputePal3CpkCrc(std::string_view segment);
std::string NormalizePal3CpkPath(std::string_view path);

// Builds a binary PAL3 .cpk archive with automatic hierarchical directory entries (`father_crc`)
// and trailing null-terminated `extra_info` filenames.
std::vector<std::uint8_t> BuildPal3CpkArchive(std::span<const CpkArchiveBuilderEntry> entries);
inline std::vector<std::uint8_t> BuildPal3CpkArchive(
    std::initializer_list<CpkArchiveBuilderEntry> entries) {
    return BuildPal3CpkArchive(
        std::span<const CpkArchiveBuilderEntry>(entries.begin(), entries.size()));
}

class CpkPackageReader {
public:
    explicit CpkPackageReader(std::istream& stream);

    const CpkHeader& Header() const noexcept { return header_; }
    std::optional<CpkMember> Find(std::string_view path) const;
    std::vector<std::string> ListFiles() const;
    std::vector<std::string> ListDirectories() const;

    std::vector<std::uint8_t> Read(std::string_view path,
                                   std::size_t limit = 64u * 1024u * 1024u);
    std::optional<std::vector<std::uint8_t>> ReadIfPresent(
        std::string_view path, std::size_t limit = 64u * 1024u * 1024u);
    std::string ReadText(std::string_view path, std::size_t limit = 16u * 1024u * 1024u);

private:
    std::optional<std::size_t> FindByHierarchy(std::string_view normalized_path) const;

    std::istream& stream_;
    CpkHeader header_{};
    std::uint64_t file_bytes_{};
    std::vector<CpkMember> members_;  // Sorted by `crc` matching gbEngine binary search order
    std::unordered_map<std::string, std::size_t> by_path_;
};

}  // namespace pal3::portable
