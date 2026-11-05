#include "pal3/portable/cpk_package.h"

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace pal3::portable;

int main() {
    // 1. Verify gbEngine custom big-endian CRC32 (`OpenPAL3/yaobow/packfs/src/cpk/crc.rs`)
    const std::uint32_t crc_upper = ComputePal3CpkCrc("YN01A.SCN");
    const std::uint32_t crc_lower = ComputePal3CpkCrc("yn01a.scn");
    assert(crc_upper == crc_lower);
    assert(crc_lower != 0u);
    assert(NormalizePal3CpkPath("\\Q01\\YN01A\\yn01a.nav\\") == "q01/yn01a/yn01a.nav");

    // 2. Build a multi-directory PAL3 .cpk archive with raw & LZO1X compressed entries
    const std::string text_payload =
        "PAL3 Yong'an Pawnshop Scene Script - Repeated LZO1X Test Block "
        "PAL3 Yong'an Pawnshop Scene Script - Repeated LZO1X Test Block "
        "PAL3 Yong'an Pawnshop Scene Script - Repeated LZO1X Test Block";
    const std::vector<std::uint8_t> raw_bytes(text_payload.begin(), text_payload.end());
    const std::vector<std::uint8_t> small_bytes = {0x53, 0x43, 0x4E, 0x00, 0x01, 0x02};

    const auto archive_bytes = BuildPal3CpkArchive({
        {"q01.scn", small_bytes, CpkCodec::raw},
        {"yn01/yn01.nav", raw_bytes, CpkCodec::lzo},
        {"yn01/sub/deep.txt", raw_bytes, CpkCodec::lzo},
    });
    assert(archive_bytes.size() > 128u);

    std::istringstream stream(
        std::string(reinterpret_cast<const char*>(archive_bytes.data()), archive_bytes.size()),
        std::ios::binary);
    CpkPackageReader reader(stream);

    // 3. Verify 128-byte RST\x1A header & synthesized father_crc directory tree
    assert(reader.Header().label == 0x1A545352u);
    assert(reader.Header().version == 1u);
    const auto dirs = reader.ListDirectories();
    assert(dirs.size() == 2u);
    assert(dirs[0] == "yn01");
    assert(dirs[1] == "yn01/sub");

    const auto files = reader.ListFiles();
    assert(files.size() == 3u);

    // 4. Verify case-insensitive and backslash-normalized hierarchical binary search (`FindByHierarchy`)
    const auto nav_member = reader.Find("YN01\\YN01.NAV");
    assert(nav_member.has_value());
    assert(nav_member->origin_size == raw_bytes.size());
    assert(nav_member->packed_size < nav_member->origin_size);
    assert(nav_member->father_crc == ComputePal3CpkCrc("yn01"));

    assert(reader.Read("q01.scn") == small_bytes);
    assert(reader.ReadText("YN01/yn01.nav") == text_payload);
    assert(reader.ReadText("yn01\\sub\\DEEP.TXT") == text_payload);

    std::cout << "[PASS] pal3_portable_cpk_package_tests\n";
    return 0;
}
