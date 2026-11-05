#pragma once

#include "pal3/portable/cpk_package.h"
#include "pal3/portable/database_combat.h"
#include "pal3/portable/minigames.h"
#include "pal3/portable/sce_vm.h"
#include "pal3/portable/scn_nav_model.h"

#include "pal4/portable/gles_renderer.h"
#include "pal4/portable/movement.h"
#include "pal4/portable/scene.h"
#include "pal4/portable/scene_collision.h"
#include "pal4/portable/world.h"
#include "pal4/random_number_generator.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pal3::portable {

struct DialogEntry {
    std::string speaker;
    std::string text;
};

struct DialogBoxSnapshot {
    bool choice_pending{false};
    std::vector<std::string> choices;
    std::vector<DialogEntry> recent_history;
};

struct RuntimeRoleEntity {
    std::int32_t id{0};
    std::string name;
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    float facing_deg{0.0f};
    std::uint32_t nav_layer{0};
    bool active{true};
    bool visible{true};
    std::uint32_t sce_proc_id{0};
    std::string current_action{"c01"};
};

struct RuntimeSceneObject {
    std::int32_t id{0};
    std::string name;
    std::uint16_t node_type{0};
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    std::uint32_t nav_layer{0};
    bool active{true};
    std::uint8_t times{255};
    std::uint32_t sce_proc_id{0};
    std::array<std::int32_t, 2> nav_trigger_min{0, 0};
    std::array<std::int32_t, 2> nav_trigger_max{0, 0};
    std::array<std::int32_t, 2> ladder_coord1{0, 0};
    std::array<std::int32_t, 2> ladder_coord2{0, 0};
    std::int32_t ladder_switch_layer{0};
    std::array<float, 3> aabb_coord1{0.0f, 0.0f, 0.0f};
    std::array<float, 3> aabb_coord2{0.0f, 0.0f, 0.0f};
    float cvd_anim_time_sec{0.0f};
};

// Complete in-memory snapshot for sub-millisecond `save_checkpoint` / `load_checkpoint`
struct Pal3HostCheckpoint {
    std::string current_cpk;
    std::string current_scn;
    std::string current_title;
    std::int32_t leader_role_id{0};
    std::uint32_t leader_nav_layer{0};
    pal4::portable::Point3 leader_position{};
    bool user_input_enabled{true};
    bool game_cleared{false};
    std::string ending_name;
    std::string active_bgm;
    std::vector<std::string> played_movies;

    std::vector<Pal3RoleState> party;
    std::map<std::int32_t, std::int32_t> favor;
    std::map<std::int32_t, std::int32_t> inventory;
    std::int32_t money{500};
    DemonSwordState demon_sword;

    std::vector<RuntimeRoleEntity> scene_npcs;
    std::vector<RuntimeSceneObject> scene_objects;
    DialogBoxSnapshot dialog;
    SceVmSnapshot sce_snapshot;
    CombatSnapshot combat_snapshot;
    MinigameStateSnapshot minigame_snapshot;
};

struct CampaignCpkSuite {
    std::vector<std::uint8_t> basedata_cpk;
    std::vector<std::uint8_t> q01_cpk;  // Act I:   渝州·宾化·唐家堡·雷州 (4 Chapters)
    std::vector<std::uint8_t> m01_cpk;  // Act II:  璧山·九龙坡·大渡口·霹雳堂地牢·九顶山 (5 Chapters)
    std::vector<std::uint8_t> q03_cpk;  // Act III: 蓬莱·安宁村·古藤林·蜀山无极阁·蛮州灵山 (5 Chapters)
    std::vector<std::uint8_t> m18_cpk;  // Act IV:  神魔之井·神界·鬼界黄泉·熔岩地狱·锁妖塔·海底城 (6 Chapters)
    std::vector<std::uint8_t> m23_cpk;  // Act V:   冰风谷·剑冢外围·剑冢铸剑厅·新仙界五大结局 (4 Chapters)
};

// Builds the complete 24-chapter PAL3 campaign across 6 authentic .cpk archives
CampaignCpkSuite BuildFullPal3CampaignCpkSuite();

struct ChapterHost final : ISceEngineServices {
    ChapterHost();
    ~ChapterHost() override;

    void InitializeFromCpkStreams(std::istream& basedata_cpk,
                                  std::istream& q01_cpk,
                                  std::istream& m01_cpk,
                                  std::istream& q03_cpk,
                                  std::istream& m18_cpk,
                                  std::istream& m23_cpk);

    // Mounts an original PAL3 retail installation directory (`basedata/basedata.cpk` + `scene/*.cpk`)
    // for on-demand dynamic loading of any original `.cpk`/`.scn`/`.sce`/`.nav`/`.pol` archive.
    bool MountOriginalGameDirectory(const std::filesystem::path& game_dir);

    void BeginNewGame();
    void StepFrames(std::uint32_t frames = 1, float dt_sec = 1.0f / 60.0f);

    // High-level agent actions
    std::string MoveLeaderTo(float target_x, float target_z,
                             std::optional<std::uint32_t> target_layer = std::nullopt,
                             std::uint32_t max_steps = 160);
    std::string Interact(std::string_view target_kind = "", std::int32_t target_id = -1);
    std::string ChooseDialogOption(std::int32_t choice_index);
    std::string SwitchLeader(std::int32_t role_id);
    std::string ToggleLongkuiForm();
    std::string PurifyDemonSword(Pal3Element element);
    std::string EvaluateEndingName() const;
    CombatTurnOutcome ExecuteCombatAction(std::string_view action_kind,
                                          std::uint32_t skill_or_item_id = 0,
                                          std::uint32_t target_enemy_slot = 0,
                                          std::string_view slot_name = "center");
    MinigameActionOutcome ExecuteMinigameAction(std::string_view action,
                                                std::int32_t option_index = 1,
                                                std::string_view destination_code = "");

    Pal3HostCheckpoint SaveCheckpoint() const;
    void LoadCheckpoint(const Pal3HostCheckpoint& cp);

    std::int32_t StoryProgress() const { return sce_vm->GetVar(kSceVarMainStory); }
    void SetStoryProgress(std::int32_t val) { sce_vm->SetVar(kSceVarMainStory, val); }
    Pal3RoleState& LeaderRole();
    const Pal3RoleState& LeaderRole() const;
    void RenderFrameToGles(int width = 1280, int height = 720);

    // ISceEngineServices implementation
    void SceRolePathTo(std::int32_t role_id, std::int32_t nav_x, std::int32_t nav_z) override;
    void SceRoleSetPos(std::int32_t role_id, std::int32_t nav_x, std::int32_t nav_z) override;
    void SceRoleSetLayer(std::int32_t role_id, std::int32_t layer) override;
    void SceRoleSetFace(std::int32_t role_id, std::int32_t dir) override;
    void SceRoleSetController(std::int32_t role_id) override;
    float SceRoleShowAction(std::int32_t role_id, std::string_view action_name,
                            std::int32_t repeat_mode) override;
    void SceRoleSetVisible(std::int32_t role_id, bool visible) override;
    void SceRoleActive(std::int32_t role_id, bool active) override;
    void SceCameraMove(float x, float y, float z, float duration_sec, std::int32_t mode) override;

    void SceShowDialog(std::string_view text) override;
    void SceShowDialogSelection(std::span<const std::string> choices) override;
    void SceFavorAdd(std::int32_t role_id, std::int32_t delta) override;
    void SceAddItem(std::int32_t item_id, std::int32_t count) override;
    void SceRemoveItem(std::int32_t item_id, std::int32_t count) override;
    bool SceHaveItem(std::int32_t item_id) const override;
    void SceAddMoney(std::int32_t delta) override;
    std::int32_t SceGetMoney() const override { return money; }
    void SceAddSkill(std::int32_t role_id, std::int32_t skill_id) override;
    bool SceIsInTeam(std::int32_t role_id) const override;
    void SceSetInTeam(std::int32_t role_id, bool in_team) override;

    void ScePlayMovie(std::string_view movie_name) override;
    void ScePlayMusic(std::string_view bgm_name, std::int32_t loop) override;
    void SceLoadScene(std::string_view cpk_name, std::string_view scn_name) override;
    void SceObjectActive(std::int32_t obj_id, bool active) override;
    void SceObjectMove(std::int32_t obj_id, float x, float y, float z, float duration_sec) override;
    float SceObjectPlay(std::int32_t obj_id, std::string_view anim_name, float duration_sec) override;
    void SceSetUserInputEnabled(bool enabled) override { user_input_enabled = enabled; }

    void SceConfigureNextCombat(std::optional<std::int32_t> max_round, bool must_fail) override;
    void SceStartCombat(std::span<const std::uint32_t> monster_ids, bool is_boss) override;
    std::int32_t SceGetLastCombatResult() const override { return combat.LastCombatResult(); }

    void SceStartAppraisalMinigame(std::uint32_t antique_id) override;
    void SceStartPawnshopMinigame(std::uint32_t shop_id) override;
    std::int32_t SceGetLastAppraisalResult() const override { return minigames.LastAppraisalResult(); }
    void SceStartEncampmentMinigame(std::uint32_t encamp_id) override;
    void SceStartSkiMinigame(std::uint32_t course_id) override;
    void SceOpenYuJianMap() override;
    void SceRestAtInn(std::string_view prompt, std::int32_t cost,
                      std::uint32_t no_money_proc, std::uint32_t after_rest_proc) override;

    // Runtime members
    pal4::RandomNumberGenerator rng;
    Pal3Database database;
    std::unique_ptr<SceVm> sce_vm;
    Pal3CombatSystem combat;
    Pal3MinigameManager minigames;

    struct LoadedSceneBundle {
        std::string title;
        std::uint32_t init_proc_id{1};
        ScnFile scn;
        NavFile nav;
        SceFile sce;
        PolModel pol;
        CvdModel cvd;
    };
    std::map<std::string, LoadedSceneBundle> scene_catalog;
    std::map<std::string, Mv3Model> role_mv3_models;
    std::filesystem::path original_game_dir;

    std::string current_cpk{"q01"};
    std::string current_scn{"yn01a"};
    std::string current_title{"渝州·永安当与竹林"};
    NavFile active_nav;
    PolModel active_pol;
    CvdModel active_cvd;

    std::int32_t leader_role_id{0};
    std::uint32_t leader_nav_layer{0};
    bool user_input_enabled{true};
    bool game_cleared{false};
    std::string ending_name;
    std::string active_bgm{"pi01.mp3"};
    std::vector<std::string> played_movies;

    std::vector<Pal3RoleState> party;
    std::map<std::int32_t, std::int32_t> favor;
    std::map<std::int32_t, std::int32_t> inventory;
    std::int32_t money{500};
    DemonSwordState demon_sword;

    std::vector<RuntimeRoleEntity> scene_npcs;
    std::vector<RuntimeSceneObject> scene_objects;
    DialogBoxSnapshot dialog;
    NavPathResult last_nav_path;

    // Shared OpenPAL4 3D collision, movement, and GLES3 renderer
    pal4::portable::Scene displayed_world_scene;
    pal4::portable::Scene displayed_leader_scene;
    pal4::portable::CollisionMesh collision_mesh;
    std::unique_ptr<pal4::portable::SceneCollision> scene_collision;
    pal4::portable::MovementState movement_state;
    pal4::portable::MovementConfig movement_config;
    pal4::portable::OrbitCamera orbit_camera;
    pal4::portable::GlesRenderer world_renderer;
    pal4::portable::GlesRenderer actor_renderer;
    EvaluatedMv3Pose leader_mv3_pose;
    float animation_clock_sec{0.0f};
    std::uint64_t total_frames{0};
    std::uint64_t total_draw_calls{0};

private:
    void RebuildCollisionAndSceneForLayer(std::uint32_t layer);
    bool CheckAndFireStepTriggers();
    bool TryLoadSceneFromOriginalGameDir(std::string_view cpk_name, std::string_view scn_name);
};

}  // namespace pal3::portable
