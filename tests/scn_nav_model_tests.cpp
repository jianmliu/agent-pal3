#include "pal3/portable/scn_nav_model.h"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace pal3::portable;

int main() {
    // 1. Test .scn 820B ScnRole & 620B ScnNode round-trip serialization
    ScnFile scn;
    scn.cpk_name = "q01";
    scn.scn_name = "yn01a";
    scn.scn_base_name = "yn01";
    scn.is_night = 1;
    scn.roles.push_back({1, "赵文昌", 180.0f, 1, 40.0f, 45.0f, 501, "c01", 1, {{10.f, 0.f, 10.f}}, 215.f});

    ScnNode ladder{};
    ladder.index = 10;
    ladder.active = 1;
    ladder.name = "pawnshop_ladder";
    ladder.node_type = static_cast<std::uint16_t>(ScnNodeType::ladder);
    ladder.position = {20.0f, 0.0f, 20.0f};
    ladder.nav_layer = 0;
    ladder.ladder_nav_coord1 = {8, 8};
    ladder.ladder_nav_coord2 = {12, 12};
    ladder.ladder_switch_layer = 1;
    scn.nodes.push_back(ladder);

    const auto scn_bytes = SerializeScnFile(scn);
    const auto parsed_scn = ParseScnFile(scn_bytes);
    assert(parsed_scn.cpk_name == "q01");
    assert(parsed_scn.roles.size() == 1u);
    assert(parsed_scn.roles[0].name == "赵文昌");
    assert(parsed_scn.roles[0].sce_proc_id == 501u);
    assert(parsed_scn.nodes.size() == 1u);
    assert(parsed_scn.nodes[0].node_type == static_cast<std::uint16_t>(ScnNodeType::ladder));
    assert(parsed_scn.nodes[0].ladder_nav_coord2[0] == 12);

    // 2. Test .nav v2 dual-layer grid + A* pathfinder with obstacle wall & cross-layer portal
    NavFile nav;
    nav.version = 2;
    nav.layers.resize(2);
    for (int li = 0; li < 2; ++li) {
        auto& l = nav.layers[li];
        l.min_coord = {-80.0f, li * 20.0f, -80.0f};
        l.max_coord = {80.0f, li * 20.0f + 10.0f, 80.0f};
        l.width = 16;
        l.height = 16;
        l.tiles.resize(16 * 16, {li * 20.0f, 3u});
        l.portals[0] = {{12, 12}, {14, 14}};
    }
    // Add an obstacle wall across x = 8, z = 0..10 on layer 0
    for (int z = 0; z <= 10; ++z) {
        nav.layers[0].At(8, z)->distance_to_border = 0u;
    }

    const auto nav_bytes = SerializeNavFile(nav);
    const auto parsed_nav = ParseNavFile(nav_bytes);
    assert(parsed_nav.layers.size() == 2u);

    // Same-layer A* around the wall
    const auto same_path = FindPal3NavPath(parsed_nav, parsed_scn.nodes, 0, -50.0f, -50.0f, 0, 50.0f, -50.0f);
    assert(same_path.reachable);
    assert(!same_path.crossed_layers);
    assert(same_path.waypoints.size() >= 2u);

    // Cross-layer A* from layer 0 to layer 1 via portal/ladder
    const auto cross_path = FindPal3NavPath(parsed_nav, parsed_scn.nodes, 0, -40.0f, -40.0f, 1, 40.0f, 40.0f);
    assert(cross_path.reachable);
    assert(cross_path.crossed_layers);
    assert(cross_path.waypoints.back().layer == 1u);

    const auto cmesh = ConvertNavFileToCollisionMesh(parsed_nav, 0);
    assert(!cmesh.floor.empty());
    assert(!cmesh.wall.empty());

    // 3. Test .pol, .cvd keyframe interpolation, and .mv3 vertex morph evaluation
    CvdModel cvd;
    cvd.animation_duration_sec = 2.0f;
    CvdNode cnode;
    cnode.name = "bridge";
    cnode.position_keys = {{0.0f, {0.f, 0.f, 0.f}}, {2.0f, {0.f, 10.f, 0.f}}};
    cnode.rotation_keys = {{0.0f, 0.f, {0.f, 1.f, 0.f}}, {2.0f, 90.f, {0.f, 1.f, 0.f}}};
    cnode.vertices = {{{0.f, 0.f, 0.f}, {0.f, 0.f}, {0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}},
                      {{1.f, 0.f, 0.f}, {1.f, 0.f}, {1.f, 0.f}, {1.f, 1.f, 1.f, 1.f}},
                      {{0.f, 0.f, 1.f}, {0.f, 1.f}, {0.f, 1.f}, {1.f, 1.f, 1.f, 1.f}}};
    cnode.triangles = {{0, 1, 2}};
    cvd.nodes.push_back(cnode);
    const auto parsed_cvd = ParseCvdModel(SerializeCvdModel(cvd));
    const auto mid_cvd = EvaluateCvdAnimation(parsed_cvd, 1.0f);
    assert(std::abs(mid_cvd.node_positions[0][1] - 5.0f) < 1e-3f);
    assert(std::abs(mid_cvd.node_rotations_deg[0] - 45.0f) < 1e-3f);

    Mv3Model mv3;
    mv3.action_name = "c01";
    mv3.duration_ticks = 4580;
    mv3.events = {{2290, "hold"}};
    mv3.frames = {
        {0, {{{0, 0, 0}, {64, 0, 0}, {0, 64, 0}}}},
        {4580, {{{0, 640, 0}, {64, 640, 0}, {0, 704, 0}}}},
    };
    mv3.texcoords = {{0.f, 0.f}, {1.f, 0.f}, {0.f, 1.f}};
    mv3.triangles = {{0, 1, 2}};
    const auto parsed_mv3 = ParseMv3Model(SerializeMv3Model(mv3));
    const auto held_pose = EvaluateMv3Pose(parsed_mv3, 0.9f, true);
    assert(held_pose.held_at_event);
    assert(std::abs(held_pose.interpolated_positions[0][1] - 5.0f) < 1e-2f);

    std::cout << "[PASS] pal3_portable_scn_nav_model_tests\n";
    return 0;
}
