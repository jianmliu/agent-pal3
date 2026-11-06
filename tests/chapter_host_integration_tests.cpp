#include "pal3/portable/chapter_host.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <sstream>

using namespace pal3::portable;

int main() {
    const auto suite = BuildFullPal3CampaignCpkSuite();
    auto make_stream = [](const std::vector<std::uint8_t>& bytes) {
        return std::istringstream(
            std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
            std::ios::binary);
    };

    auto base_s = make_stream(suite.basedata_cpk);
    auto q01_s = make_stream(suite.q01_cpk);
    auto m01_s = make_stream(suite.m01_cpk);
    auto q03_s = make_stream(suite.q03_cpk);
    auto m18_s = make_stream(suite.m18_cpk);
    auto m23_s = make_stream(suite.m23_cpk);

    ChapterHost host;
    host.InitializeFromCpkStreams(base_s, q01_s, m01_s, q03_s, m18_s, m23_s);
    host.BeginNewGame();

    assert(host.current_cpk == "q01");
    assert(host.current_scn == "yn01a");
    assert(host.StoryProgress() == 10101);
    assert(host.total_draw_calls >= 2u);
    assert(host.scene_catalog.size() == 24u);

    // Cross-layer navigation to Zhao Wenchang on layer 1 + Antique Appraisal + SceProcHooks (10101 -> 10202)
    const auto move_res = host.MoveLeaderTo(40.0f, 40.0f, 1u);
    assert(move_res == "move_reached_cross_layer");
    assert(host.leader_nav_layer == 1u);

    host.Interact("npc", 1);
    assert(host.minigames.IsActive());
    host.ExecuteMinigameAction("inspect", 1);
    assert(!host.minigames.IsActive());
    assert(host.StoryProgress() == 10202);

    // Save & restore checkpoint verification
    const auto cp = host.SaveCheckpoint();
    host.SceLoadScene("m23", "m23");
    assert(host.current_cpk == "m23");
    host.LoadCheckpoint(cp);
    assert(host.current_cpk == "q01");
    assert(host.StoryProgress() == 10202);

    // Verify all 5 canonical endings evaluation
    host.demon_sword.equipped_divine_sword = "圣灵魔剑";
    assert(host.EvaluateEndingName().find("完美结局") != std::string::npos);
    host.demon_sword.equipped_divine_sword = "圣灵镇妖剑";
    host.favor[1] = 90;
    host.favor[2] = 40;
    assert(host.EvaluateEndingName().find("雪见结局") != std::string::npos);
    host.favor[1] = 40;
    host.favor[2] = 95;
    assert(host.EvaluateEndingName().find("龙葵结局") != std::string::npos);
    host.favor[2] = 40;
    host.favor[5] = 85;
    assert(host.EvaluateEndingName().find("花楹结局") != std::string::npos);
    host.favor[5] = 20;
    host.favor[3] = 85;
    assert(host.EvaluateEndingName().find("紫萱结局") != std::string::npos);

    // Verify mounting real PAL3 game directory (`/Volumes/T7-Data/pal/pal3-data/PAL3`) if available
    const std::filesystem::path real_dir{"/Volumes/T7-Data/pal/pal3-data/PAL3"};
    std::error_code ec;
    if (std::filesystem::exists(real_dir / "basedata" / "basedata.cpk", ec)) {
        assert(host.MountOriginalGameDirectory(real_dir));
        assert(host.role_mv3_models["101"].frames.size() > 2u);
        assert(host.displayed_world_scene.vertices.size() > 100u);
        // Verify dynamic loading of a raw original PAL3 scene (`m01/1`) from `scene/M01.cpk`
        assert(host.TryLoadSceneFromOriginalGameDir("m01", "1"));
        assert(host.scene_catalog.count("m01/1") == 1u);
        assert(!host.scene_catalog["m01/1"].nav.layers.empty());
        assert(!host.scene_catalog["m01/1"].pol.meshes.empty());
    }

    std::cout << "[PASS] pal3_chapter_host_integration_tests\n";
    return 0;
}
