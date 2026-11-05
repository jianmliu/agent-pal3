#pragma once

#include "pal4/portable/scene.h"
#include "pal4/portable/world.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pal3::portable {

// ============================================================================
// 1. PAL3 .scn Scene Definition (`OpenPAL3/yaobow/shared/src/openpal3/loaders/scn_loader.rs`)
// ============================================================================

enum class ScnNodeType : std::uint16_t {
    tile_trigger = 0,          // Auto-triggered when stepping into [nav_trigger_coord_min, max] on same layer
    chest = 11,                // Treasure chest (interactive within D <= 100)
    cross_layer_trigger = 14,  // Auto-triggered when stepping into tile rect on any layer
    ladder = 15,               // Climbable ladder / stair portal between ladder_nav_coord1 & 2
    mechanism = 16,            // Interactive switch / bridge mechanism (.cvd animated)
    aabb_trigger = 20,         // Sphere-AABB investigate trigger (R = 50)
    antique_pickup = 23,       // Collectable antique / item on ground
    inspectable = 33,          // Inspectable plaque / object
    elevator = 40,             // Multi-layer elevator / iron chain platform (Locking-Demon Tower)
};

struct ScnRole {
    std::uint8_t index{};
    std::string name;         // Model ID or NPC name (64B GBK in .scn)
    float facing_deg{0.0f};   // Offset 0x44
    std::uint32_t nav_layer{0};  // Offset 0x48: 0 or 1
    float position_x{0.0f};   // Offset 0x4C
    float position_z{0.0f};   // Offset 0x54
    std::uint32_t sce_proc_id{0};  // Offset 0x5C: .sce script procedure triggered on talk
    std::string action_name{"c01"};
    std::uint32_t path_mode{0};
    std::vector<std::array<float, 3>> patrol_path;
    float move_speed{215.0f};
};

struct ScnNode {
    std::uint8_t index{};
    std::uint8_t active{1};
    std::uint8_t times{255};  // Remaining trigger count (255 = infinite)
    std::uint8_t switch_state{0};
    std::string name;         // 32B model/identifier name (e.g. "_f01.cvd" or "_g01.pol")
    std::uint16_t node_type{0};
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    float rotation_deg{0.0f};
    std::array<std::int32_t, 2> nav_trigger_coord_min{0, 0};
    std::array<std::int32_t, 2> nav_trigger_coord_max{0, 0};
    std::uint32_t nav_layer{0};
    std::array<std::int32_t, 2> ladder_nav_coord1{0, 0};
    std::array<std::int32_t, 2> ladder_nav_coord2{0, 0};
    std::int32_t ladder_switch_layer{0};
    std::uint32_t sce_proc_id{0};
    std::array<float, 3> aabb_trigger_coord1{0.0f, 0.0f, 0.0f};
    std::array<float, 3> aabb_trigger_coord2{0.0f, 0.0f, 0.0f};
};

struct ScnFile {
    std::string cpk_name{"q01"};
    std::string scn_name{"yn01a"};
    std::string scn_base_name{"yn01"};
    std::uint32_t is_night{0};
    std::uint32_t skybox_id{0};
    std::vector<ScnRole> roles;
    std::vector<ScnNode> nodes;
};

std::vector<std::uint8_t> SerializeScnFile(const ScnFile& scn);
ScnFile ParseScnFile(std::span<const std::uint8_t> bytes);

// ============================================================================
// 2. PAL3 .nav Dual-Layer Tile Navigation Mesh & A* Pathfinder
//    (`OpenPAL3/yaobow/shared/src/openpal3/loaders/nav_loader.rs`)
// ============================================================================

struct NavLayerPortal {
    std::array<std::int32_t, 2> min_coord{0, 0};
    std::array<std::int32_t, 2> max_coord{0, 0};
};

struct NavTilePoint {
    float height{0.0f};
    std::uint32_t distance_to_border{0};  // 0 = obstacle/impassable, 1..6 = walkable clearance
};

struct NavLayer {
    std::array<NavLayerPortal, 8> portals{};
    std::array<float, 3> max_coord{200.0f, 50.0f, 200.0f};
    std::array<float, 3> min_coord{-200.0f, -10.0f, -200.0f};
    std::uint32_t width{32};
    std::uint32_t height{32};
    std::vector<NavTilePoint> tiles;  // Row-major: tiles[z * width + x]

    const NavTilePoint* At(std::int32_t tx, std::int32_t tz) const noexcept;
    NavTilePoint* At(std::int32_t tx, std::int32_t tz) noexcept;
};

struct NavFile {
    std::uint8_t version{2};
    std::vector<NavLayer> layers;  // 1 or 2 layers (Layer 0 = ground, Layer 1 = upper walkway)
};

std::vector<std::uint8_t> SerializeNavFile(const NavFile& nav);
NavFile ParseNavFile(std::span<const std::uint8_t> bytes);

std::array<std::int32_t, 2> WorldToNavTile(const NavLayer& layer, float world_x, float world_z);
std::array<float, 3> NavTileToWorld(const NavLayer& layer, std::int32_t tx, std::int32_t tz);
float SampleNavHeight(const NavLayer& layer, float world_x, float world_z);
bool IsNavTileWalkable(const NavLayer& layer, std::int32_t tx, std::int32_t tz);

struct NavWaypoint {
    std::uint32_t layer{0};
    std::array<std::int32_t, 2> tile{0, 0};
    std::array<float, 3> world_pos{0.0f, 0.0f, 0.0f};
    bool via_portal_or_ladder{false};
};

struct NavPathResult {
    bool reachable{false};
    bool crossed_layers{false};
    float total_cost{0.0f};
    std::vector<NavWaypoint> waypoints;
};

// Finds an optimal path on PAL3's dual-layer .nav grid using 8-neighbor A* with
// `distance_to_border` clearance weighting, Bresenham line-of-sight string-pulling,
// and automatic cross-layer routing through `NavLayerPortal` or `ScnNode` ladders.
NavPathResult FindPal3NavPath(const NavFile& nav,
                              std::span<const ScnNode> scn_nodes,
                              std::uint32_t start_layer,
                              float start_x,
                              float start_z,
                              std::uint32_t goal_layer,
                              float goal_x,
                              float goal_z);

// Bridges a PAL3 .nav dual-layer grid into OpenPAL4's `CollisionMesh` (floor triangles +
// obstacle boundary wall triangles) for `pal4::portable::SceneCollision` & `StepActorMovement`.
pal4::portable::CollisionMesh ConvertNavFileToCollisionMesh(const NavFile& nav,
                                                            std::uint32_t active_layer = 0);

// ============================================================================
// 3. PAL3 3D Models: .pol (Static), .cvd (Keyframe Mechanism), .mv3 (Morph Role)
// ============================================================================

struct PolSubMesh {
    std::string name;
    std::string texture_name;
    std::array<float, 3> aabb_min{-50.0f, 0.0f, -50.0f};
    std::array<float, 3> aabb_max{50.0f, 50.0f, 50.0f};
    std::vector<pal4::portable::Vertex> vertices;
    std::vector<std::array<std::uint16_t, 3>> triangles;
};

struct PolModel {
    std::uint32_t version{1};
    std::vector<PolSubMesh> meshes;
};

std::vector<std::uint8_t> SerializePolModel(const PolModel& pol);
PolModel ParsePolModel(std::span<const std::uint8_t> bytes);
pal4::portable::Scene ConvertPolToPortableScene(const PolModel& pol);

struct CvdPositionKey {
    float timestamp_sec{0.0f};
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
};

struct CvdRotationKey {
    float timestamp_sec{0.0f};
    float angle_deg{0.0f};
    std::array<float, 3> axis{0.0f, 1.0f, 0.0f};
};

struct CvdNode {
    std::string name;
    float scale{1.0f};
    std::vector<CvdPositionKey> position_keys;
    std::vector<CvdRotationKey> rotation_keys;
    std::vector<pal4::portable::Vertex> vertices;
    std::vector<std::array<std::uint16_t, 3>> triangles;
};

struct CvdModel {
    float animation_duration_sec{1.0f};
    std::vector<CvdNode> nodes;
};

struct EvaluatedCvdState {
    float time_sec{0.0f};
    bool finished{false};
    std::vector<std::array<float, 3>> node_positions;
    std::vector<float> node_rotations_deg;
    double state_checksum{0.0};
};

std::vector<std::uint8_t> SerializeCvdModel(const CvdModel& cvd);
CvdModel ParseCvdModel(std::span<const std::uint8_t> bytes);
EvaluatedCvdState EvaluateCvdAnimation(const CvdModel& cvd, float time_sec);
pal4::portable::Scene ConvertCvdToPortableScene(const CvdModel& cvd, float time_sec = 0.0f);

// .mv3 Character Per-Vertex Morph Animation (`OpenPAL3/yaobow/fileformats/src/mv3.rs`)
struct Mv3MorphFrame {
    std::uint32_t timestamp_ticks{0};  // 1 sec = 4580 ticks in PAL3 MV3
    std::vector<std::array<std::int16_t, 3>> vertex_positions_q6;  // Scaled by 0.015625f (1/64)
};

struct Mv3ActionEvent {
    std::uint32_t timestamp_ticks{0};
    std::string name;  // e.g. "hold" for repeat_mode == -2
};

struct Mv3Model {
    std::uint32_t version{100};
    std::string action_name{"c01"};
    std::string texture_name{"role.tga"};
    std::uint32_t duration_ticks{4580};
    std::vector<Mv3ActionEvent> events;
    std::vector<Mv3MorphFrame> frames;
    std::vector<std::array<float, 2>> texcoords;
    std::vector<std::array<std::uint16_t, 3>> triangles;

    float DurationSec() const noexcept { return static_cast<float>(duration_ticks) / 4580.0f; }
    std::optional<float> HoldMarkSec() const noexcept;
};

struct EvaluatedMv3Pose {
    std::string action_name;
    float time_sec{0.0f};
    bool held_at_event{false};
    std::vector<std::array<float, 3>> interpolated_positions;
    double pose_checksum{0.0};
};

std::vector<std::uint8_t> SerializeMv3Model(const Mv3Model& mv3);
Mv3Model ParseMv3Model(std::span<const std::uint8_t> bytes);
EvaluatedMv3Pose EvaluateMv3Pose(const Mv3Model& mv3, float time_sec, bool hold_at_mark = false);
pal4::portable::Scene ConvertMv3ToPortableScene(const Mv3Model& mv3,
                                                float time_sec = 0.0f,
                                                bool hold_at_mark = false);

}  // namespace pal3::portable
