#include "pal3/portable/chapter_host.h"

#include <cassert>
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

    std::cout << "[PASS] pal3_chapter_host_integration_tests\n";
    return 0;
}
