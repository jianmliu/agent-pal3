#include "pal3/portable/sce_vm.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace pal3::portable {
namespace {

void WriteU8(std::vector<std::uint8_t>& out, std::uint8_t v) { out.push_back(v); }

void WriteU16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8u) & 0xFFu));
}

void WriteU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (i * 8u)) & 0xFFu));
    }
}

void WriteU32At(std::vector<std::uint8_t>& out, std::size_t off, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) {
        out[off + i] = static_cast<std::uint8_t>((v >> (i * 8u)) & 0xFFu);
    }
}

void WriteFixedString(std::vector<std::uint8_t>& out, std::string_view s, std::size_t len) {
    const std::size_t n = std::min(s.size(), len > 0 ? len - 1 : 0);
    for (std::size_t i = 0; i < n; ++i) out.push_back(static_cast<std::uint8_t>(s[i]));
    for (std::size_t i = n; i < len; ++i) out.push_back(0u);
}

struct SceByteReader {
    std::span<const std::uint8_t> buf;
    std::size_t pos{0};

    void Seek(std::size_t p) {
        if (p > buf.size()) throw std::runtime_error("SceByteReader seek past end");
        pos = p;
    }

    std::uint8_t ReadU8() {
        if (pos + 1 > buf.size()) throw std::runtime_error("SceByteReader EOF on u8");
        return buf[pos++];
    }

    std::uint16_t ReadU16() {
        if (pos + 2 > buf.size()) throw std::runtime_error("SceByteReader EOF on u16");
        const auto v = static_cast<std::uint16_t>(buf[pos]) |
                       (static_cast<std::uint16_t>(buf[pos + 1]) << 8u);
        pos += 2;
        return v;
    }

    std::int16_t ReadI16() { return static_cast<std::int16_t>(ReadU16()); }

    std::uint32_t ReadU32() {
        if (pos + 4 > buf.size()) throw std::runtime_error("SceByteReader EOF on u32");
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

    std::string ReadString() {
        const std::uint16_t len = ReadU16();
        if (pos + len > buf.size()) throw std::runtime_error("SceByteReader EOF on string");
        const auto* begin = reinterpret_cast<const char*>(buf.data() + pos);
        std::size_t actual = 0;
        while (actual < len && begin[actual] != '\0') ++actual;
        pos += len;
        return std::string(begin, actual);
    }

    std::string ReadFixedString(std::size_t fixed_len) {
        if (pos + fixed_len > buf.size()) throw std::runtime_error("SceByteReader EOF on FixedString");
        const auto* begin = reinterpret_cast<const char*>(buf.data() + pos);
        std::size_t actual = 0;
        while (actual < fixed_len && begin[actual] != '\0') ++actual;
        pos += fixed_len;
        return std::string(begin, actual);
    }
};

}  // namespace

// ============================================================================
// 1. SceFopState (`OpenPAL3/yaobow/shared/src/openpal3/states/global_state.rs`)
// ============================================================================

void SceFopState::SetOp(SceFopOp next_op) {
    if (lhs.has_value() && rhs.has_value()) {
        if (op == SceFopOp::logical_and) {
            lhs = (*lhs && *rhs);
        } else if (op == SceFopOp::logical_or) {
            lhs = (*lhs || *rhs);
        }
        rhs.reset();
    }
    op = next_op;
}

void SceFopState::PushValue(bool value) {
    if (!lhs.has_value()) {
        lhs = value;
    } else if (op == SceFopOp::nop) {
        lhs = value;
    } else {
        rhs = value;
        SetOp(op);
    }
}

bool SceFopState::ReadValueAndReset() {
    SetOp(SceFopOp::nop);
    const bool res = lhs.value_or(true);
    lhs.reset();
    rhs.reset();
    op = SceFopOp::nop;
    return res;
}

// ============================================================================
// 2. .sce Binary Serialization & Parsing (`sce_loader.rs`)
// ============================================================================

std::vector<std::uint8_t> SerializeSceFile(const SceFile& sce) {
    std::vector<std::uint8_t> out;
    out.insert(out.end(), {'S', 'C', 'E', '\0'});
    WriteU8(out, sce.version);
    WriteU16(out, static_cast<std::uint16_t>(sce.procs.size()));

    struct HeaderPos {
        std::uint32_t id;
        std::size_t offset_pos;
    };
    std::vector<HeaderPos> header_positions;
    header_positions.reserve(sce.procs.size());

    for (const auto& [id, proc] : sce.procs) {
        WriteU32(out, id);
        const std::size_t off_pos = out.size();
        WriteU32(out, 0u);
        WriteFixedString(out, proc.name, 64);
        header_positions.push_back({id, off_pos});
    }

    std::size_t idx = 0;
    for (const auto& [id, proc] : sce.procs) {
        const std::uint32_t body_offset = static_cast<std::uint32_t>(out.size());
        WriteU32At(out, header_positions[idx++].offset_pos, body_offset);

        WriteU32(out, proc.id);
        WriteU16(out, 0u);
        WriteFixedString(out, proc.name, 64);
        WriteU16(out, proc.local_var_count);
        for (std::uint16_t v = 0; v < proc.local_var_count; ++v) {
            // data_type = 0 (i32) + initial value (0)
            WriteU8(out, 0u);
            WriteU32(out, 0u);
        }
        WriteU32(out, static_cast<std::uint32_t>(proc.instructions.size()));
        out.insert(out.end(), proc.instructions.begin(), proc.instructions.end());
    }

    return out;
}

SceFile ParseSceFile(std::span<const std::uint8_t> bytes) {
    SceByteReader r{bytes, 0};
    if (r.ReadFixedString(4) != "SCE") {
        throw std::runtime_error("invalid PAL3 .sce magic (expected SCE\\0)");
    }
    SceFile sce;
    sce.version = r.ReadU8();
    if (sce.version != 1u) {
        throw std::runtime_error("unsupported PAL3 .sce version");
    }
    const std::uint16_t proc_num = r.ReadU16();
    struct RawHeader {
        std::uint32_t id;
        std::uint32_t offset;
        std::string name;
    };
    std::vector<RawHeader> headers(proc_num);
    for (std::uint16_t i = 0; i < proc_num; ++i) {
        headers[i].id = r.ReadU32();
        headers[i].offset = r.ReadU32();
        headers[i].name = r.ReadFixedString(64);
    }

    for (const auto& h : headers) {
        r.Seek(h.offset);
        SceProc proc;
        proc.id = r.ReadU32();
        (void)r.ReadU16();
        proc.name = r.ReadFixedString(64);
        proc.local_var_count = r.ReadU16();
        for (std::uint16_t v = 0; v < proc.local_var_count; ++v) {
            const std::uint8_t dt = r.ReadU8();
            if (dt == 0u || dt == 1u) {
                (void)r.ReadU32();
            } else if (dt == 3u) {
                (void)r.ReadString();
            } else if (dt == 4u) {
                (void)r.ReadU16();
            }
        }
        const std::uint32_t inst_len = r.ReadU32();
        if (r.pos + inst_len > r.buf.size()) {
            throw std::runtime_error("PAL3 .sce proc instructions exceed file bounds");
        }
        proc.instructions.assign(r.buf.begin() + static_cast<std::ptrdiff_t>(r.pos),
                                 r.buf.begin() + static_cast<std::ptrdiff_t>(r.pos + inst_len));
        sce.procs.emplace(proc.id, std::move(proc));
    }

    return sce;
}

// ============================================================================
// 3. SceAssembler Implementation
// ============================================================================

void SceAssembler::EmitOpHeader(SceOpcode op, std::uint16_t param_flag) {
    WriteU16(static_cast<std::uint16_t>(op));
    WriteU16(param_flag);
}

void SceAssembler::WriteU16(std::uint16_t v) { pal3::portable::WriteU16(code_, v); }
void SceAssembler::WriteI16(std::int16_t v) { WriteU16(static_cast<std::uint16_t>(v)); }
void SceAssembler::WriteU32(std::uint32_t v) { pal3::portable::WriteU32(code_, v); }
void SceAssembler::WriteI32(std::int32_t v) { WriteU32(static_cast<std::uint32_t>(v)); }
void SceAssembler::WriteF32(float v) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(float));
    WriteU32(bits);
}
void SceAssembler::WriteString(std::string_view s) {
    WriteU16(static_cast<std::uint16_t>(s.size() + 1u));
    code_.insert(code_.end(), s.begin(), s.end());
    code_.push_back(0u);
}

void SceAssembler::EmitIdle(float seconds) {
    EmitOpHeader(SceOpcode::idle);
    WriteF32(seconds);
}

void SceAssembler::EmitScriptRunMode(std::int32_t mode) {
    EmitOpHeader(SceOpcode::script_run_mode);
    WriteI32(mode);
}

std::size_t SceAssembler::EmitGotoPlaceholder() {
    EmitOpHeader(SceOpcode::goto_addr);
    const std::size_t pos = code_.size();
    WriteU32(0u);
    return pos;
}

void SceAssembler::EmitGoto(std::uint32_t target_offset) {
    EmitOpHeader(SceOpcode::goto_addr);
    WriteU32(target_offset);
}

void SceAssembler::PatchJumpTarget(std::size_t addr_pos, std::uint32_t target_offset) {
    WriteU32At(code_, addr_pos, target_offset);
}

void SceAssembler::EmitCall(std::uint32_t proc_id) {
    EmitOpHeader(SceOpcode::call_proc);
    WriteU32(proc_id);
}

void SceAssembler::EmitFop(SceFopOp op) {
    EmitOpHeader(SceOpcode::fop);
    WriteI32(static_cast<std::int32_t>(op));
}

void SceAssembler::EmitCompareVarConst(SceOpcode cmp_op, std::int16_t var, std::int32_t value) {
    EmitOpHeader(cmp_op, 0u);
    WriteI16(var);
    WriteI32(value);
}

void SceAssembler::EmitCompareVarVar(SceOpcode cmp_op, std::int16_t var_a, std::int16_t var_b) {
    EmitOpHeader(cmp_op, 2u);
    WriteI16(var_a);
    WriteI16(var_b);
}

std::size_t SceAssembler::EmitTestGotoPlaceholder() {
    EmitOpHeader(SceOpcode::test_goto);
    const std::size_t pos = code_.size();
    WriteU32(0u);
    return pos;
}

void SceAssembler::EmitTestGoto(std::uint32_t target_offset) {
    EmitOpHeader(SceOpcode::test_goto);
    WriteU32(target_offset);
}

void SceAssembler::EmitAssign(std::int16_t var, std::int32_t value) {
    EmitOpHeader(SceOpcode::assign, 0u);
    WriteI16(var);
    WriteI32(value);
}

void SceAssembler::EmitAdd(std::int16_t var, std::int32_t value) {
    EmitOpHeader(SceOpcode::add, 0u);
    WriteI16(var);
    WriteI32(value);
}

void SceAssembler::EmitSub(std::int16_t var, std::int32_t value) {
    EmitOpHeader(SceOpcode::sub, 0u);
    WriteI16(var);
    WriteI32(value);
}

void SceAssembler::EmitBetween(std::int16_t var, std::int32_t lb, std::int32_t ub) {
    EmitOpHeader(SceOpcode::between, 0u);
    WriteI16(var);
    WriteI32(lb);
    WriteI32(ub);
}

void SceAssembler::EmitRolePathTo(std::int32_t role_id, std::int32_t nav_x, std::int32_t nav_z,
                                  std::int32_t flag) {
    EmitOpHeader(SceOpcode::role_path_to);
    WriteI32(role_id);
    WriteI32(nav_x);
    WriteI32(nav_z);
    WriteI32(flag);
}

void SceAssembler::EmitRoleSetPos(std::int32_t role_id, std::int32_t nav_x, std::int32_t nav_z) {
    EmitOpHeader(SceOpcode::role_set_pos);
    WriteI32(role_id);
    WriteI32(nav_x);
    WriteI32(nav_z);
}

void SceAssembler::EmitRoleSetLayer(std::int32_t role_id, std::int32_t layer) {
    EmitOpHeader(SceOpcode::role_set_layer);
    WriteI32(role_id);
    WriteI32(layer);
}

void SceAssembler::EmitRoleSetFace(std::int32_t role_id, std::int32_t dir) {
    EmitOpHeader(SceOpcode::role_set_face);
    WriteI32(role_id);
    WriteI32(dir);
}

void SceAssembler::EmitRoleSetController(std::int32_t role_id) {
    EmitOpHeader(SceOpcode::role_set_controller);
    WriteI32(role_id);
}

void SceAssembler::EmitRoleShowAction(std::int32_t role_id, std::string_view action_name,
                                      std::int32_t repeat_mode) {
    EmitOpHeader(SceOpcode::role_show_action);
    WriteI32(role_id);
    WriteString(action_name);
    WriteI32(repeat_mode);
}

void SceAssembler::EmitRoleSetVisible(std::int32_t role_id, std::int32_t visible) {
    EmitOpHeader(SceOpcode::role_set_visible);
    WriteI32(role_id);
    WriteI32(visible);
}

void SceAssembler::EmitCameraMove(float x, float y, float z, float duration_sec, std::int32_t mode) {
    EmitOpHeader(SceOpcode::camera_move);
    WriteF32(x);
    WriteF32(y);
    WriteF32(z);
    WriteF32(duration_sec);
    WriteI32(mode);
}

void SceAssembler::EmitDlg(std::string_view text) {
    EmitOpHeader(SceOpcode::dlg);
    WriteString(text);
}

void SceAssembler::EmitDlgSel(std::span<const std::string> choices) {
    EmitOpHeader(SceOpcode::dlg_sel);
    WriteU16(static_cast<std::uint16_t>(choices.size()));
    for (const auto& c : choices) {
        WriteString(c);
    }
}

void SceAssembler::EmitFavorAdd(std::int32_t role_id, std::int32_t delta) {
    EmitOpHeader(SceOpcode::favor_add);
    WriteI32(role_id);
    WriteI32(delta);
}

void SceAssembler::EmitAddItem(std::int32_t item_id, std::int32_t count) {
    EmitOpHeader(SceOpcode::add_item);
    WriteI32(item_id);
    WriteI32(count);
}

void SceAssembler::EmitRemoveItem(std::int32_t item_id, std::int32_t count) {
    EmitOpHeader(SceOpcode::remove_item);
    WriteI32(item_id);
    WriteI32(count);
}

void SceAssembler::EmitAddMoney(std::int32_t delta) {
    EmitOpHeader(SceOpcode::add_money);
    WriteI32(delta);
}

void SceAssembler::EmitGetMoney(std::int16_t out_var) {
    EmitOpHeader(SceOpcode::get_money);
    WriteI16(out_var);
}

void SceAssembler::EmitAddSkill(std::int32_t role_id, std::int32_t skill_id) {
    EmitOpHeader(SceOpcode::add_skill);
    WriteI32(role_id);
    WriteI32(skill_id);
}

void SceAssembler::EmitMovie(std::string_view movie_name) {
    EmitOpHeader(SceOpcode::movie);
    WriteString(movie_name);
}

void SceAssembler::EmitLoadScene(std::string_view cpk_name, std::string_view scn_name) {
    EmitOpHeader(SceOpcode::load_scene);
    WriteString(cpk_name);
    WriteString(scn_name);
}

void SceAssembler::EmitRoleActive(std::int32_t role_id, std::int32_t active) {
    EmitOpHeader(SceOpcode::role_active);
    WriteI32(role_id);
    WriteI32(active);
}

void SceAssembler::EmitMusic(std::string_view bgm_name, std::int32_t loop) {
    EmitOpHeader(SceOpcode::music);
    WriteString(bgm_name);
    WriteI32(loop);
}

void SceAssembler::EmitHaveItem(std::int32_t item_id) {
    EmitOpHeader(SceOpcode::have_item);
    WriteI32(item_id);
}

void SceAssembler::EmitCombatBoss(std::array<std::uint32_t, 6> monster_ids) {
    EmitOpHeader(SceOpcode::combat_boss);
    for (std::uint32_t id : monster_ids) WriteU32(id);
}

void SceAssembler::EmitCombat(std::array<std::uint32_t, 3> monster_ids) {
    EmitOpHeader(SceOpcode::combat);
    for (std::uint32_t id : monster_ids) WriteU32(id);
}

void SceAssembler::EmitCombatMaxRound(std::int32_t max_round) {
    EmitOpHeader(SceOpcode::combat_max_round);
    WriteI32(max_round);
}

void SceAssembler::EmitCombatMustFail() {
    EmitOpHeader(SceOpcode::combat_must_fail);
}

void SceAssembler::EmitGetCombat(std::int16_t out_var) {
    EmitOpHeader(SceOpcode::get_combat);
    WriteI16(out_var);
}

void SceAssembler::EmitObjectActive(std::int32_t obj_id, std::int32_t active) {
    EmitOpHeader(SceOpcode::object_active);
    WriteI32(obj_id);
    WriteI32(active);
}

void SceAssembler::EmitObjectMove(std::int32_t obj_id, float x, float y, float z, float duration_sec) {
    EmitOpHeader(SceOpcode::object_move);
    WriteI32(obj_id);
    WriteF32(x);
    WriteF32(y);
    WriteF32(z);
    WriteF32(duration_sec);
}

void SceAssembler::EmitObjectPlay(std::int32_t obj_id, std::string_view anim_name, float duration_sec) {
    EmitOpHeader(SceOpcode::object_play);
    WriteI32(obj_id);
    WriteString(anim_name);
    WriteF32(duration_sec);
}

void SceAssembler::EmitApprEntry(std::uint32_t antique_id) {
    EmitOpHeader(SceOpcode::appr_entry);
    WriteU32(antique_id);
}

void SceAssembler::EmitGetAppr(std::int16_t out_var) {
    EmitOpHeader(SceOpcode::get_appr);
    WriteI16(out_var);
}

void SceAssembler::EmitEncampEntry(std::uint32_t encamp_id) {
    EmitOpHeader(SceOpcode::encamp_entry);
    WriteU32(encamp_id);
}

void SceAssembler::EmitSkeeEntry(std::uint32_t course_id) {
    EmitOpHeader(SceOpcode::skee_entry);
    WriteU32(course_id);
}

void SceAssembler::EmitYuJian() {
    EmitOpHeader(SceOpcode::yujian);
}

void SceAssembler::EmitIfInTeam(std::int32_t role_id) {
    EmitOpHeader(SceOpcode::if_in_team);
    WriteI32(role_id);
}

void SceAssembler::EmitInTeam(std::int32_t role_id, std::int32_t in_team) {
    EmitOpHeader(SceOpcode::in_team);
    WriteI32(role_id);
    WriteI32(in_team);
}

void SceAssembler::EmitUserInput(std::int32_t enable) {
    EmitOpHeader(SceOpcode::user_input);
    WriteI32(enable);
}

void SceAssembler::EmitShowChatRest(std::string_view prompt, std::int32_t cost,
                                    std::uint32_t no_money_proc, std::uint32_t after_rest_proc) {
    EmitOpHeader(SceOpcode::show_chat_rest);
    WriteString(prompt);
    WriteI32(cost);
    WriteU32(no_money_proc);
    WriteU32(after_rest_proc);
}

SceProc SceAssembler::BuildProc(std::uint32_t id, std::string name,
                                std::uint16_t local_var_count) const {
    return SceProc{id, std::move(name), local_var_count, code_};
}

// ============================================================================
// 4. SceVm Runtime Execution (`OpenPAL3/yaobow/shared/src/scripting/sce/vm.rs`)
// ============================================================================

SceVm::SceVm(ISceEngineServices& services) : services_(services) {
    global_vars_[kSceVarMainStory] = 0;
    global_vars_[kSceVarDlgSelect] = 0;
}

void SceVm::SetSceneScript(std::string_view cpk_name, std::string_view scn_name, SceFile sce) {
    active_cpk_ = std::string(cpk_name);
    active_scn_ = std::string(scn_name);
    scene_sce_ = std::move(sce);
    call_stack_.clear();
    active_tasks_.clear();
    run_mode_ = 1;
}

void SceVm::SetSystemScript(SceFile sys_sce) { sys_sce_ = std::move(sys_sce); }

const SceProc* SceVm::FindProc(std::uint32_t proc_id) const {
    auto it = scene_sce_.procs.find(proc_id);
    if (it != scene_sce_.procs.end()) return &it->second;
    auto sit = sys_sce_.procs.find(proc_id);
    if (sit != sys_sce_.procs.end()) return &sit->second;
    return nullptr;
}

void SceVm::CallProc(std::uint32_t proc_id) {
    const auto* proc = FindProc(proc_id);
    if (!proc) return;
    SceStackFrame frame;
    frame.proc_id = proc->id;
    frame.proc_name = proc->name;
    frame.pc = 0;
    call_stack_.push_back(std::move(frame));
}

std::int32_t SceVm::GetVar(std::int16_t var) const {
    if (var < 0) {
        auto it = global_vars_.find(var);
        return (it != global_vars_.end()) ? it->second : 0;
    }
    if (!call_stack_.empty()) {
        const auto& locals = call_stack_.back().local_vars;
        auto it = locals.find(var);
        return (it != locals.end()) ? it->second : 0;
    }
    return 0;
}

void SceVm::SetVar(std::int16_t var, std::int32_t value) {
    if (var < 0) {
        global_vars_[var] = value;
    } else if (!call_stack_.empty()) {
        call_stack_.back().local_vars[var] = value;
    }
}

bool SceVm::IsWaitingForChoice() const noexcept {
    return std::any_of(active_tasks_.begin(), active_tasks_.end(),
                       [](const SceActiveTask& t) { return t.waiting_for_choice; });
}

bool SceVm::IsWaitingForCombat() const noexcept {
    return std::any_of(active_tasks_.begin(), active_tasks_.end(),
                       [](const SceActiveTask& t) { return t.waiting_for_combat; });
}

bool SceVm::IsWaitingForMinigame() const noexcept {
    return std::any_of(active_tasks_.begin(), active_tasks_.end(),
                       [](const SceActiveTask& t) { return t.waiting_for_minigame; });
}

void SceVm::SubmitDialogSelection(std::int32_t choice_index) {
    SetVar(kSceVarDlgSelect, choice_index);
    active_tasks_.erase(
        std::remove_if(active_tasks_.begin(), active_tasks_.end(),
                       [](const SceActiveTask& t) { return t.waiting_for_choice; }),
        active_tasks_.end());
}

void SceVm::NotifyCombatFinished() {
    active_tasks_.erase(
        std::remove_if(active_tasks_.begin(), active_tasks_.end(),
                       [](const SceActiveTask& t) { return t.waiting_for_combat; }),
        active_tasks_.end());
}

void SceVm::NotifyMinigameFinished() {
    active_tasks_.erase(
        std::remove_if(active_tasks_.begin(), active_tasks_.end(),
                       [](const SceActiveTask& t) { return t.waiting_for_minigame; }),
        active_tasks_.end());
}

// Reproduces `OpenPAL3/yaobow/yaobow/src/openpal3/sce_proc_hooks.rs`
void SceVm::RunProcExitHooks(std::uint32_t proc_id) {
    if (active_cpk_ == "q01" && proc_id == 501u) {
        // Yuzhou Yong'an Pawnshop tutorial hook: advances main story var -32768 from 10101 to 10202
        if (GetVar(kSceVarMainStory) == 10101) {
            SetVar(kSceVarMainStory, 10202);
        }
    }
}

void SceVm::Step(float dt_sec, bool fast_forward_timers) {
    // 1. Update active blocking/concurrent tasks
    for (auto& task : active_tasks_) {
        if (task.waiting_for_choice || task.waiting_for_combat || task.waiting_for_minigame) {
            continue;
        }
        if (fast_forward_timers) {
            task.remaining_sec = 0.0f;
        } else {
            task.remaining_sec = std::max(0.0f, task.remaining_sec - dt_sec);
        }
    }
    active_tasks_.erase(
        std::remove_if(active_tasks_.begin(), active_tasks_.end(),
                       [](const SceActiveTask& t) {
                           return !t.waiting_for_choice && !t.waiting_for_combat &&
                                  !t.waiting_for_minigame && t.remaining_sec <= 0.0f;
                       }),
        active_tasks_.end());

    // 2. If waiting on interactive decision (choice / combat / minigame), suspend VM fetching
    if (IsWaitingForChoice() || IsWaitingForCombat() || IsWaitingForMinigame()) {
        return;
    }

    // 3. In serial mode (`run_mode_ == 1`), wait until active timed tasks finish before fetching
    if (run_mode_ == 1 && !active_tasks_.empty()) {
        return;
    }

    // 4. Fetch and execute instructions
    int guard = 0;
    while (!call_stack_.empty() && guard++ < 512) {
        const bool can_continue = FetchAndExecuteNextInstruction(fast_forward_timers);
        if (!can_continue) break;
        if (IsWaitingForChoice() || IsWaitingForCombat() || IsWaitingForMinigame()) break;
        if (run_mode_ == 1 && !active_tasks_.empty()) break;
    }
}

bool SceVm::FetchAndExecuteNextInstruction(bool fast_forward_timers) {
    while (!call_stack_.empty()) {
        auto& frame = call_stack_.back();
        const auto* proc = FindProc(frame.proc_id);
        if (!proc || frame.pc >= proc->instructions.size()) {
            const std::uint32_t finished_id = frame.proc_id;
            call_stack_.pop_back();
            RunProcExitHooks(finished_id);
            continue;
        }

        SceByteReader r{proc->instructions, frame.pc};
        const auto op = static_cast<SceOpcode>(r.ReadU16());
        const std::uint16_t param_flag = r.ReadU16();

        switch (op) {
            case SceOpcode::idle: {
                const float secs = r.ReadF32();
                frame.pc = r.pos;
                if (!fast_forward_timers && secs > 0.0f) {
                    active_tasks_.push_back({op, secs, false, false, false, "Idle"});
                }
                return run_mode_ == 2 || fast_forward_timers;
            }
            case SceOpcode::script_run_mode: {
                run_mode_ = r.ReadI32();
                frame.pc = r.pos;
                return true;
            }
            case SceOpcode::goto_addr: {
                const std::uint32_t target = r.ReadU32();
                frame.pc = target;
                return true;
            }
            case SceOpcode::call_proc: {
                const std::uint32_t callee_id = r.ReadU32();
                frame.pc = r.pos;
                CallProc(callee_id);
                return true;
            }
            case SceOpcode::fop: {
                const auto fop_op = static_cast<SceFopOp>(r.ReadI32());
                fop_state_.SetOp(fop_op);
                frame.pc = r.pos;
                return true;
            }
            case SceOpcode::gt:
            case SceOpcode::ls:
            case SceOpcode::eq:
            case SceOpcode::neq:
            case SceOpcode::geq:
            case SceOpcode::leq: {
                const std::int16_t var_a = r.ReadI16();
                const std::int32_t lhs = GetVar(var_a);
                const std::int32_t rhs =
                    ((param_flag & 2u) != 0u) ? GetVar(r.ReadI16()) : r.ReadI32();
                bool cmp = false;
                switch (op) {
                    case SceOpcode::gt: cmp = (lhs > rhs); break;
                    case SceOpcode::ls: cmp = (lhs < rhs); break;
                    case SceOpcode::eq: cmp = (lhs == rhs); break;
                    case SceOpcode::neq: cmp = (lhs != rhs); break;
                    case SceOpcode::geq: cmp = (lhs >= rhs); break;
                    case SceOpcode::leq: cmp = (lhs <= rhs); break;
                    default: break;
                }
                fop_state_.PushValue(cmp);
                frame.pc = r.pos;
                return true;
            }
            case SceOpcode::test_goto: {
                const std::uint32_t addr = r.ReadU32();
                const bool cond = fop_state_.ReadValueAndReset();
                frame.pc = cond ? r.pos : static_cast<std::size_t>(addr);
                return true;
            }
            case SceOpcode::assign: {
                const std::int16_t var = r.ReadI16();
                const std::int32_t val =
                    ((param_flag & 2u) != 0u) ? GetVar(r.ReadI16()) : r.ReadI32();
                SetVar(var, val);
                frame.pc = r.pos;
                return true;
            }
            case SceOpcode::add: {
                const std::int16_t var = r.ReadI16();
                const std::int32_t delta = r.ReadI32();
                SetVar(var, GetVar(var) + delta);
                frame.pc = r.pos;
                return true;
            }
            case SceOpcode::sub: {
                const std::int16_t var = r.ReadI16();
                const std::int32_t delta = r.ReadI32();
                SetVar(var, GetVar(var) - delta);
                frame.pc = r.pos;
                return true;
            }
            case SceOpcode::between: {
                const std::int16_t var = r.ReadI16();
                const std::int32_t lb = r.ReadI32();
                const std::int32_t ub = r.ReadI32();
                const std::int32_t val = GetVar(var);
                fop_state_.PushValue(val >= lb && val <= ub);
                frame.pc = r.pos;
                return true;
            }
            case SceOpcode::role_path_to: {
                const std::int32_t role_id = r.ReadI32();
                const std::int32_t nx = r.ReadI32();
                const std::int32_t nz = r.ReadI32();
                (void)r.ReadI32();
                frame.pc = r.pos;
                services_.SceRolePathTo(role_id, nx, nz);
                if (!fast_forward_timers) {
                    active_tasks_.push_back({op, 0.25f, false, false, false, "RolePathTo"});
                }
                return run_mode_ == 2 || fast_forward_timers;
            }
            case SceOpcode::role_set_pos: {
                const std::int32_t role_id = r.ReadI32();
                const std::int32_t nx = r.ReadI32();
                const std::int32_t nz = r.ReadI32();
                frame.pc = r.pos;
                services_.SceRoleSetPos(role_id, nx, nz);
                return true;
            }
            case SceOpcode::role_set_layer: {
                const std::int32_t role_id = r.ReadI32();
                const std::int32_t layer = r.ReadI32();
                frame.pc = r.pos;
                services_.SceRoleSetLayer(role_id, layer);
                return true;
            }
            case SceOpcode::role_set_face: {
                const std::int32_t role_id = r.ReadI32();
                const std::int32_t dir = r.ReadI32();
                frame.pc = r.pos;
                services_.SceRoleSetFace(role_id, dir);
                return true;
            }
            case SceOpcode::role_set_controller: {
                const std::int32_t role_id = r.ReadI32();
                frame.pc = r.pos;
                services_.SceRoleSetController(role_id);
                return true;
            }
            case SceOpcode::role_show_action: {
                const std::int32_t role_id = r.ReadI32();
                const std::string act = r.ReadString();
                const std::int32_t repeat_mode = r.ReadI32();
                frame.pc = r.pos;
                const float dur = services_.SceRoleShowAction(role_id, act, repeat_mode);
                if (!fast_forward_timers && dur > 0.0f) {
                    active_tasks_.push_back({op, dur, false, false, false, "RoleShowAction:" + act});
                }
                return run_mode_ == 2 || fast_forward_timers;
            }
            case SceOpcode::role_set_visible: {
                const std::int32_t role_id = r.ReadI32();
                const std::int32_t vis = r.ReadI32();
                frame.pc = r.pos;
                services_.SceRoleSetVisible(role_id, vis != 0);
                return true;
            }
            case SceOpcode::camera_move: {
                const float x = r.ReadF32();
                const float y = r.ReadF32();
                const float z = r.ReadF32();
                const float dur = r.ReadF32();
                const std::int32_t mode = r.ReadI32();
                frame.pc = r.pos;
                services_.SceCameraMove(x, y, z, dur, mode);
                if (!fast_forward_timers && dur > 0.0f) {
                    active_tasks_.push_back({op, dur, false, false, false, "CameraMove"});
                }
                return run_mode_ == 2 || fast_forward_timers;
            }
            case SceOpcode::dlg: {
                const std::string text = r.ReadString();
                frame.pc = r.pos;
                services_.SceShowDialog(text);
                return true;
            }
            case SceOpcode::dlg_sel: {
                const std::uint16_t cnt = r.ReadU16();
                std::vector<std::string> choices(cnt);
                for (std::uint16_t i = 0; i < cnt; ++i) {
                    choices[i] = r.ReadString();
                }
                frame.pc = r.pos;
                services_.SceShowDialogSelection(choices);
                active_tasks_.push_back({op, 0.0f, true, false, false, "DlgSel"});
                return false;
            }
            case SceOpcode::favor_add: {
                const std::int32_t role_id = r.ReadI32();
                const std::int32_t delta = r.ReadI32();
                frame.pc = r.pos;
                services_.SceFavorAdd(role_id, delta);
                return true;
            }
            case SceOpcode::add_item: {
                const std::int32_t item_id = r.ReadI32();
                const std::int32_t count = r.ReadI32();
                frame.pc = r.pos;
                services_.SceAddItem(item_id, count);
                return true;
            }
            case SceOpcode::remove_item: {
                const std::int32_t item_id = r.ReadI32();
                const std::int32_t count = r.ReadI32();
                frame.pc = r.pos;
                services_.SceRemoveItem(item_id, count);
                return true;
            }
            case SceOpcode::add_money: {
                const std::int32_t delta = r.ReadI32();
                frame.pc = r.pos;
                services_.SceAddMoney(delta);
                return true;
            }
            case SceOpcode::get_money: {
                const std::int16_t out_var = r.ReadI16();
                frame.pc = r.pos;
                SetVar(out_var, services_.SceGetMoney());
                return true;
            }
            case SceOpcode::add_skill: {
                const std::int32_t role_id = r.ReadI32();
                const std::int32_t skill_id = r.ReadI32();
                frame.pc = r.pos;
                services_.SceAddSkill(role_id, skill_id);
                return true;
            }
            case SceOpcode::movie: {
                const std::string movie = r.ReadString();
                frame.pc = r.pos;
                services_.ScePlayMovie(movie);
                return true;
            }
            case SceOpcode::load_scene: {
                const std::string cpk = r.ReadString();
                const std::string scn = r.ReadString();
                frame.pc = r.pos;
                // Note: SceLoadScene replaces scene_sce_ and call_stack_, so return immediately
                services_.SceLoadScene(cpk, scn);
                return true;
            }
            case SceOpcode::role_active: {
                const std::int32_t role_id = r.ReadI32();
                const std::int32_t act = r.ReadI32();
                frame.pc = r.pos;
                services_.SceRoleActive(role_id, act != 0);
                return true;
            }
            case SceOpcode::music: {
                const std::string bgm = r.ReadString();
                const std::int32_t loop = r.ReadI32();
                frame.pc = r.pos;
                services_.ScePlayMusic(bgm, loop);
                return true;
            }
            case SceOpcode::have_item: {
                const std::int32_t item_id = r.ReadI32();
                frame.pc = r.pos;
                fop_state_.PushValue(services_.SceHaveItem(item_id));
                return true;
            }
            case SceOpcode::combat_max_round: {
                pending_combat_max_round_ = r.ReadI32();
                frame.pc = r.pos;
                return true;
            }
            case SceOpcode::combat_must_fail: {
                pending_combat_must_fail_ = true;
                frame.pc = r.pos;
                return true;
            }
            case SceOpcode::combat_boss: {
                std::vector<std::uint32_t> monsters;
                for (int i = 0; i < 6; ++i) {
                    const std::uint32_t mid = r.ReadU32();
                    if (mid != 0u) monsters.push_back(mid);
                }
                frame.pc = r.pos;
                services_.SceConfigureNextCombat(pending_combat_max_round_, pending_combat_must_fail_);
                pending_combat_max_round_.reset();
                pending_combat_must_fail_ = false;
                services_.SceStartCombat(monsters, true);
                active_tasks_.push_back({op, 0.0f, false, true, false, "CombatBoss"});
                return false;
            }
            case SceOpcode::combat: {
                std::vector<std::uint32_t> monsters;
                for (int i = 0; i < 3; ++i) {
                    const std::uint32_t mid = r.ReadU32();
                    if (mid != 0u) monsters.push_back(mid);
                }
                frame.pc = r.pos;
                services_.SceConfigureNextCombat(pending_combat_max_round_, pending_combat_must_fail_);
                pending_combat_max_round_.reset();
                pending_combat_must_fail_ = false;
                services_.SceStartCombat(monsters, false);
                active_tasks_.push_back({op, 0.0f, false, true, false, "Combat"});
                return false;
            }
            case SceOpcode::get_combat: {
                const std::int16_t out_var = r.ReadI16();
                frame.pc = r.pos;
                SetVar(out_var, services_.SceGetLastCombatResult());
                return true;
            }
            case SceOpcode::object_active: {
                const std::int32_t obj_id = r.ReadI32();
                const std::int32_t act = r.ReadI32();
                frame.pc = r.pos;
                services_.SceObjectActive(obj_id, act != 0);
                return true;
            }
            case SceOpcode::object_move: {
                const std::int32_t obj_id = r.ReadI32();
                const float x = r.ReadF32();
                const float y = r.ReadF32();
                const float z = r.ReadF32();
                const float dur = r.ReadF32();
                frame.pc = r.pos;
                services_.SceObjectMove(obj_id, x, y, z, dur);
                if (!fast_forward_timers && dur > 0.0f) {
                    active_tasks_.push_back({op, dur, false, false, false, "ObjectMove"});
                }
                return run_mode_ == 2 || fast_forward_timers;
            }
            case SceOpcode::object_play: {
                const std::int32_t obj_id = r.ReadI32();
                const std::string anim = r.ReadString();
                const float dur = r.ReadF32();
                frame.pc = r.pos;
                const float actual_dur = services_.SceObjectPlay(obj_id, anim, dur);
                if (!fast_forward_timers && actual_dur > 0.0f) {
                    active_tasks_.push_back({op, actual_dur, false, false, false, "ObjectPlay:" + anim});
                }
                return run_mode_ == 2 || fast_forward_timers;
            }
            case SceOpcode::appr_entry: {
                const std::uint32_t antique_id = r.ReadU32();
                frame.pc = r.pos;
                services_.SceStartAppraisalMinigame(antique_id);
                active_tasks_.push_back({op, 0.0f, false, false, true, "APPREntry"});
                return false;
            }
            case SceOpcode::get_appr: {
                const std::int16_t out_var = r.ReadI16();
                frame.pc = r.pos;
                SetVar(out_var, services_.SceGetLastAppraisalResult());
                return true;
            }
            case SceOpcode::encamp_entry: {
                const std::uint32_t encamp_id = r.ReadU32();
                frame.pc = r.pos;
                services_.SceStartEncampmentMinigame(encamp_id);
                active_tasks_.push_back({op, 0.0f, false, false, true, "ENCAMP_Entry"});
                return false;
            }
            case SceOpcode::skee_entry: {
                const std::uint32_t course_id = r.ReadU32();
                frame.pc = r.pos;
                services_.SceStartSkiMinigame(course_id);
                active_tasks_.push_back({op, 0.0f, false, false, true, "SKEE_Entry"});
                return false;
            }
            case SceOpcode::yujian: {
                frame.pc = r.pos;
                services_.SceOpenYuJianMap();
                active_tasks_.push_back({op, 0.0f, false, false, true, "YuJian"});
                return false;
            }
            case SceOpcode::if_in_team: {
                const std::int32_t role_id = r.ReadI32();
                frame.pc = r.pos;
                fop_state_.PushValue(services_.SceIsInTeam(role_id));
                return true;
            }
            case SceOpcode::in_team: {
                const std::int32_t role_id = r.ReadI32();
                const std::int32_t in_team = r.ReadI32();
                frame.pc = r.pos;
                services_.SceSetInTeam(role_id, in_team != 0);
                return true;
            }
            case SceOpcode::user_input: {
                const std::int32_t enable = r.ReadI32();
                frame.pc = r.pos;
                services_.SceSetUserInputEnabled(enable != 0);
                return true;
            }
            case SceOpcode::show_chat_rest: {
                const std::string prompt = r.ReadString();
                const std::int32_t cost = r.ReadI32();
                const std::uint32_t no_money_proc = r.ReadU32();
                const std::uint32_t after_rest_proc = r.ReadU32();
                frame.pc = r.pos;
                services_.SceRestAtInn(prompt, cost, no_money_proc, after_rest_proc);
                return true;
            }
        }
        throw std::runtime_error("unhandled PAL3 .sce opcode: " +
                                 std::to_string(static_cast<std::uint16_t>(op)));
    }
    return false;
}

SceVmSnapshot SceVm::CaptureSnapshot() const {
    return SceVmSnapshot{
        active_cpk_, active_scn_, run_mode_, fop_state_, global_vars_, call_stack_, active_tasks_};
}

void SceVm::RestoreSnapshot(const SceVmSnapshot& snap) {
    active_cpk_ = snap.active_cpk;
    active_scn_ = snap.active_scn;
    run_mode_ = snap.run_mode;
    fop_state_ = snap.fop_state;
    global_vars_ = snap.global_vars;
    call_stack_ = snap.call_stack;
    active_tasks_ = snap.active_tasks;
}

}  // namespace pal3::portable
