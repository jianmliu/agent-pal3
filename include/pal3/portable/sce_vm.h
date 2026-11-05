#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pal3::portable {

// Global variable indices in PAL3 .sce bytecode (`OpenPAL3/yaobow/shared/src/scripting/sce/vm.rs`)
constexpr std::int16_t kSceVarMainStory = -32768;   // 0x8000: Main story progress `g_story`
constexpr std::int16_t kSceVarDlgSelect = -32767;   // 0x8001: Result of `DlgSel` (0..N-1)

// Converts a raw string from a PAL3 binary (.scn / .sce) that may be either GBK (CP936) or UTF-8 into valid UTF-8.
std::string ConvertGbkOrUtf8ToUtf8(std::string_view raw);

// Full PAL3 .sce opcode table matching `OpenPAL3/yaobow/shared/src/scripting/sce/vm.rs` and classic PAL3 `g_sCmdDef`
enum class SceOpcode : std::uint16_t {
    idle = 1,
    script_run_mode = 2,
    goto_addr = 3,
    call_proc = 4,
    fop = 5,
    gt = 6,
    ls = 7,
    eq = 8,
    neq = 9,
    geq = 10,
    leq = 11,
    test_goto = 12,
    assign = 13,
    add = 14,
    sub = 15,
    call_proc_16 = 16,
    rnd = 17,
    between = 19,
    role_path_to_20 = 20,
    role_set_pos_21 = 21,
    role_path_to = 22,
    role_set_pos = 23,
    role_set_layer = 24,
    role_set_face = 25,
    team_close = 26,
    role_set_controller = 27,
    role_active_28 = 28,
    role_show_action = 29,
    camera_focus_role = 30,
    role_set_visible = 31,
    camera_push = 32,
    camera_rotate = 33,
    camera_move = 34,
    camera_wag = 35,
    camera_set = 36,
    dlg_sel = 37,
    camera_push_state = 38,
    camera_pop_state = 39,
    lk_ghost = 42,
    favor_add = 43,
    add_item = 46,
    remove_item = 47,
    add_money = 48,
    get_money = 49,
    get_favor = 50,
    add_skill = 51,
    get_favorite = 52,
    full_role_att = 54,
    dlg_62 = 62,
    movie = 63,
    load_scene = 64,
    dlg_sel_65 = 65,
    get_dlg_sel = 66,
    role_active = 67,
    note = 68,
    fade_out = 69,
    fade_in = 70,
    role_stop = 71,
    role_emote = 72,
    music = 73,
    climb = 74,
    dlg_time = 76,
    get_time_sel = 77,
    have_item = 78,
    play_sound = 79,
    combat_boss = 80,
    combat = 81,
    combat_max_round = 82,
    combat_must_fail = 83,
    combat_84 = 84,
    object_active_85 = 85,
    caption = 86,
    open_door = 87,
    hy_mode = 88,
    hy_fly = 89,
    object_move_90 = 90,
    object_active = 91,
    object_move = 92,
    object_play = 93,
    switch_rs = 102,
    appr_entry = 104,
    pawnshop_entry = 105,
    encamp_entry = 106,
    skee_entry = 107,
    get_appr = 108,
    enable_sword = 109,
    specify_compos = 111,
    start_hide_fight = 113,
    movie_115 = 115,
    set_role_texture = 116,
    rotate = 117,
    quake = 118,
    show_chat_rest_119 = 119,
    trigger = 124,
    set_bigmap_element = 125,
    get_switch = 126,
    entry_row = 127,
    rotate_inv = 128,
    dist = 130,
    combat_not_game_over = 131,
    get_combat = 132,
    music_133 = 133,
    stop_music = 134,
    role_fade_out = 135,
    role_fade_in = 136,
    if_in_team = 137,
    enable_sword_skill = 138,
    snow = 140,
    scr_eft = 141,
    ceft_pos = 142,
    ceft = 143,
    ceft_role = 144,
    average_lv = 145,
    switch_2_menu = 147,
    ceft_load = 148,
    give_cloth = 149,
    load_act = 150,
    water_magic = 152,
    full_team_att = 153,
    yujian = 154,
    camera_yaw = 155,
    xj_pic = 156,
    obj_not_load = 158,
    init_flower = 159,
    user_input = 200,
    dlg = 201,
    in_team = 202,
    role_set_layer_203 = 203,
    role_ctrl_204 = 204,
    role_overlap = 205,
    role_scale = 206,
    role_act_auto_stand = 207,
    role_move_back = 208,
    role_face_role = 209,
    role_turn_face_a = 210,
    team_open_a = 211,
    team_close_a = 212,
    role_move_to = 214,
    role_end_action = 221,
    show_chat_rest = 249,
    camera_free = 250,
    object_move_251 = 251,
};

// Returns the number of registered PAL3 .sce opcodes supported by SceVm
std::size_t RegisteredSceOpcodeCount() noexcept;

enum class SceFopOp : std::int32_t {
    nop = 0,
    logical_and = 1,
    logical_or = 2,
};

// Restorable boolean accumulator used by FOP(5), GT..LEQ(6..11), Between(19), HaveItem(78), IfInTeam(137), TestGoto(12)
struct SceFopState {
    std::optional<bool> lhs;
    std::optional<bool> rhs;
    SceFopOp op{SceFopOp::nop};

    void SetOp(SceFopOp next_op);
    void PushValue(bool value);
    bool ReadValueAndReset();
};

struct SceProc {
    std::uint32_t id{1};
    std::string name;
    std::uint16_t local_var_count{8};
    std::vector<std::uint8_t> instructions;
};

struct SceFile {
    std::uint8_t version{1};
    std::map<std::uint32_t, SceProc> procs;
};

// Serializes/parses binary PAL3 .sce files (magic "SCE\0")
std::vector<std::uint8_t> SerializeSceFile(const SceFile& sce);
SceFile ParseSceFile(std::span<const std::uint8_t> bytes);

// Ergonomic builder for constructing authentic PAL3 .sce procedure bytecode
class SceAssembler {
public:
    std::size_t CurrentOffset() const noexcept { return code_.size(); }

    void EmitIdle(float seconds);
    void EmitScriptRunMode(std::int32_t mode);
    std::size_t EmitGotoPlaceholder();
    void EmitGoto(std::uint32_t target_offset);
    void PatchJumpTarget(std::size_t addr_pos, std::uint32_t target_offset);
    void EmitCall(std::uint32_t proc_id);
    void EmitFop(SceFopOp op);
    void EmitCompareVarConst(SceOpcode cmp_op, std::int16_t var, std::int32_t value);
    void EmitCompareVarVar(SceOpcode cmp_op, std::int16_t var_a, std::int16_t var_b);
    std::size_t EmitTestGotoPlaceholder();
    void EmitTestGoto(std::uint32_t target_offset);
    void EmitAssign(std::int16_t var, std::int32_t value);
    void EmitAdd(std::int16_t var, std::int32_t value);
    void EmitSub(std::int16_t var, std::int32_t value);
    void EmitBetween(std::int16_t var, std::int32_t lb, std::int32_t ub);
    void EmitRolePathTo(std::int32_t role_id, std::int32_t nav_x, std::int32_t nav_z, std::int32_t flag = 0);
    void EmitRoleSetPos(std::int32_t role_id, std::int32_t nav_x, std::int32_t nav_z);
    void EmitRoleSetLayer(std::int32_t role_id, std::int32_t layer);
    void EmitRoleSetFace(std::int32_t role_id, std::int32_t dir);
    void EmitRoleSetController(std::int32_t role_id);
    void EmitRoleShowAction(std::int32_t role_id, std::string_view action_name, std::int32_t repeat_mode = 1);
    void EmitRoleSetVisible(std::int32_t role_id, std::int32_t visible);
    void EmitCameraMove(float x, float y, float z, float duration_sec, std::int32_t mode = 0);
    void EmitDlg(std::string_view text);
    void EmitDlgSel(std::span<const std::string> choices);
    void EmitFavorAdd(std::int32_t role_id, std::int32_t delta);
    void EmitAddItem(std::int32_t item_id, std::int32_t count);
    void EmitRemoveItem(std::int32_t item_id, std::int32_t count);
    void EmitAddMoney(std::int32_t delta);
    void EmitGetMoney(std::int16_t out_var);
    void EmitAddSkill(std::int32_t role_id, std::int32_t skill_id);
    void EmitMovie(std::string_view movie_name);
    void EmitLoadScene(std::string_view cpk_name, std::string_view scn_name);
    void EmitRoleActive(std::int32_t role_id, std::int32_t active);
    void EmitMusic(std::string_view bgm_name, std::int32_t loop = 1);
    void EmitHaveItem(std::int32_t item_id);
    void EmitCombatBoss(std::array<std::uint32_t, 6> monster_ids);
    void EmitCombat(std::array<std::uint32_t, 3> monster_ids);
    void EmitCombatMaxRound(std::int32_t max_round);
    void EmitCombatMustFail();
    void EmitGetCombat(std::int16_t out_var);
    void EmitObjectActive(std::int32_t obj_id, std::int32_t active);
    void EmitObjectMove(std::int32_t obj_id, float x, float y, float z, float duration_sec);
    void EmitObjectPlay(std::int32_t obj_id, std::string_view anim_name, float duration_sec);
    void EmitApprEntry(std::uint32_t antique_id);
    void EmitPawnshopEntry(std::uint32_t shop_id);
    void EmitGetAppr(std::int16_t out_var);
    void EmitEncampEntry(std::uint32_t encamp_id);
    void EmitSkeeEntry(std::uint32_t course_id);
    void EmitYuJian();
    void EmitIfInTeam(std::int32_t role_id);
    void EmitInTeam(std::int32_t role_id, std::int32_t in_team);
    void EmitUserInput(std::int32_t enable);
    void EmitShowChatRest(std::string_view prompt, std::int32_t cost,
                          std::uint32_t no_money_proc, std::uint32_t after_rest_proc);

    SceProc BuildProc(std::uint32_t id, std::string name, std::uint16_t local_var_count = 16) const;

private:
    void EmitOpHeader(SceOpcode op, std::uint16_t param_flag = 0);
    void WriteU16(std::uint16_t v);
    void WriteI16(std::int16_t v);
    void WriteU32(std::uint32_t v);
    void WriteI32(std::int32_t v);
    void WriteF32(float v);
    void WriteString(std::string_view s);

    std::vector<std::uint8_t> code_;
};

// Host engine services invoked by SceVm instructions
struct ISceEngineServices {
    virtual ~ISceEngineServices() = default;

    virtual void SceRolePathTo(std::int32_t role_id, std::int32_t nav_x, std::int32_t nav_z) = 0;
    virtual void SceRoleSetPos(std::int32_t role_id, std::int32_t nav_x, std::int32_t nav_z) = 0;
    virtual void SceRoleSetLayer(std::int32_t role_id, std::int32_t layer) = 0;
    virtual void SceRoleSetFace(std::int32_t role_id, std::int32_t dir) = 0;
    virtual void SceRoleSetController(std::int32_t role_id) = 0;
    virtual float SceRoleShowAction(std::int32_t role_id, std::string_view action_name,
                                    std::int32_t repeat_mode) = 0;
    virtual void SceRoleSetVisible(std::int32_t role_id, bool visible) = 0;
    virtual void SceRoleActive(std::int32_t role_id, bool active) = 0;
    virtual void SceCameraMove(float x, float y, float z, float duration_sec, std::int32_t mode) = 0;

    virtual void SceShowDialog(std::string_view text) = 0;
    virtual void SceShowDialogSelection(std::span<const std::string> choices) = 0;
    virtual void SceFavorAdd(std::int32_t role_id, std::int32_t delta) = 0;
    virtual void SceAddItem(std::int32_t item_id, std::int32_t count) = 0;
    virtual void SceRemoveItem(std::int32_t item_id, std::int32_t count) = 0;
    virtual bool SceHaveItem(std::int32_t item_id) const = 0;
    virtual void SceAddMoney(std::int32_t delta) = 0;
    virtual std::int32_t SceGetMoney() const = 0;
    virtual void SceAddSkill(std::int32_t role_id, std::int32_t skill_id) = 0;
    virtual bool SceIsInTeam(std::int32_t role_id) const = 0;
    virtual void SceSetInTeam(std::int32_t role_id, bool in_team) = 0;

    virtual void ScePlayMovie(std::string_view movie_name) = 0;
    virtual void ScePlayMusic(std::string_view bgm_name, std::int32_t loop) = 0;
    virtual void SceLoadScene(std::string_view cpk_name, std::string_view scn_name) = 0;
    virtual void SceObjectActive(std::int32_t obj_id, bool active) = 0;
    virtual void SceObjectMove(std::int32_t obj_id, float x, float y, float z, float duration_sec) = 0;
    virtual float SceObjectPlay(std::int32_t obj_id, std::string_view anim_name, float duration_sec) = 0;
    virtual void SceSetUserInputEnabled(bool enabled) = 0;

    virtual void SceConfigureNextCombat(std::optional<std::int32_t> max_round, bool must_fail) = 0;
    virtual void SceStartCombat(std::span<const std::uint32_t> monster_ids, bool is_boss) = 0;
    virtual std::int32_t SceGetLastCombatResult() const = 0;

    virtual void SceStartAppraisalMinigame(std::uint32_t antique_id) = 0;
    virtual void SceStartPawnshopMinigame(std::uint32_t shop_id) {}
    virtual std::int32_t SceGetLastAppraisalResult() const = 0;
    virtual void SceStartEncampmentMinigame(std::uint32_t encamp_id) = 0;
    virtual void SceStartSkiMinigame(std::uint32_t course_id) = 0;
    virtual void SceOpenYuJianMap() = 0;
    virtual void SceRestAtInn(std::string_view prompt, std::int32_t cost,
                              std::uint32_t no_money_proc, std::uint32_t after_rest_proc) = 0;
};

struct SceActiveTask {
    SceOpcode opcode{SceOpcode::idle};
    float remaining_sec{0.0f};
    bool waiting_for_choice{false};
    bool waiting_for_combat{false};
    bool waiting_for_minigame{false};
    std::string description;
};

struct SceStackFrame {
    std::uint32_t proc_id{1};
    std::string proc_name;
    std::size_t pc{0};
    std::map<std::int16_t, std::int32_t> local_vars;
};

struct SceVmSnapshot {
    std::string active_cpk;
    std::string active_scn;
    std::int32_t run_mode{1};
    SceFopState fop_state;
    std::map<std::int16_t, std::int32_t> global_vars;
    std::vector<SceStackFrame> call_stack;
    std::vector<SceActiveTask> active_tasks;
};

class SceVm {
public:
    explicit SceVm(ISceEngineServices& services);

    void SetSceneScript(std::string_view cpk_name, std::string_view scn_name, SceFile sce);
    void SetSystemScript(SceFile sys_sce);

    void CallProc(std::uint32_t proc_id);
    void Step(float dt_sec, bool fast_forward_timers = false);

    // Resumes blocking states when the agent answers a dialog choice, finishes combat, or finishes a minigame
    void SubmitDialogSelection(std::int32_t choice_index);
    void NotifyCombatFinished();
    void NotifyMinigameFinished();

    bool IsIdle() const noexcept { return call_stack_.empty() && active_tasks_.empty(); }
    bool IsWaitingForChoice() const noexcept;
    bool IsWaitingForCombat() const noexcept;
    bool IsWaitingForMinigame() const noexcept;

    std::int32_t RunMode() const noexcept { return run_mode_; }
    const std::vector<SceActiveTask>& ActiveTasks() const noexcept { return active_tasks_; }

    std::int32_t GetVar(std::int16_t var) const;
    void SetVar(std::int16_t var, std::int32_t value);
    const std::map<std::int16_t, std::int32_t>& GlobalVars() const noexcept { return global_vars_; }
    std::map<std::int16_t, std::int32_t>& MutableGlobalVars() noexcept { return global_vars_; }

    SceVmSnapshot CaptureSnapshot() const;
    void RestoreSnapshot(const SceVmSnapshot& snap);

private:
    const SceProc* FindProc(std::uint32_t proc_id) const;
    void RunProcExitHooks(std::uint32_t proc_id);
    bool FetchAndExecuteNextInstruction(bool fast_forward_timers);

    ISceEngineServices& services_;
    std::string active_cpk_;
    std::string active_scn_;
    SceFile scene_sce_;
    SceFile sys_sce_;

    std::int32_t run_mode_{1};  // 1 = serial step, 2 = parallel/concurrent
    SceFopState fop_state_;
    std::map<std::int16_t, std::int32_t> global_vars_;
    std::vector<SceStackFrame> call_stack_;
    std::vector<SceActiveTask> active_tasks_;

    std::optional<std::int32_t> pending_combat_max_round_;
    bool pending_combat_must_fail_{false};
};

}  // namespace pal3::portable
