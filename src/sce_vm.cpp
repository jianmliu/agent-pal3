#include "pal3/portable/sce_vm.h"

#include <iconv.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <stdexcept>

namespace pal3::portable {
namespace {

bool IsValidUtf8(std::string_view s) noexcept {
    std::size_t i = 0;
    const auto n = s.size();
    while (i < n) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80u) {
            ++i;
        } else if ((c & 0xE0u) == 0xC0u) {
            if (i + 1 >= n) return false;
            const auto c1 = static_cast<unsigned char>(s[i + 1]);
            if ((c1 & 0xC0u) != 0x80u || c < 0xC2u) return false;
            i += 2;
        } else if ((c & 0xF0u) == 0xE0u) {
            if (i + 2 >= n) return false;
            const auto c1 = static_cast<unsigned char>(s[i + 1]);
            const auto c2 = static_cast<unsigned char>(s[i + 2]);
            if ((c1 & 0xC0u) != 0x80u || (c2 & 0xC0u) != 0x80u) return false;
            i += 3;
        } else if ((c & 0xF8u) == 0xF0u) {
            if (i + 3 >= n) return false;
            const auto c1 = static_cast<unsigned char>(s[i + 1]);
            const auto c2 = static_cast<unsigned char>(s[i + 2]);
            const auto c3 = static_cast<unsigned char>(s[i + 3]);
            if ((c1 & 0xC0u) != 0x80u || (c2 & 0xC0u) != 0x80u || (c3 & 0xC0u) != 0x80u) {
                return false;
            }
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

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
        return ConvertGbkOrUtf8ToUtf8(std::string_view(begin, actual));
    }

    std::string ReadFixedString(std::size_t fixed_len) {
        if (pos + fixed_len > buf.size()) throw std::runtime_error("SceByteReader EOF on FixedString");
        const auto* begin = reinterpret_cast<const char*>(buf.data() + pos);
        std::size_t actual = 0;
        while (actual < fixed_len && begin[actual] != '\0') ++actual;
        pos += fixed_len;
        return ConvertGbkOrUtf8ToUtf8(std::string_view(begin, actual));
    }
};

}  // namespace

std::string ConvertGbkOrUtf8ToUtf8(std::string_view raw) {
    if (raw.empty() || IsValidUtf8(raw)) {
        return std::string(raw);
    }
    iconv_t cd = iconv_open("UTF-8", "GBK");
    if (cd == reinterpret_cast<iconv_t>(-1)) {
        return std::string(raw);
    }
    std::string out(raw.size() * 3 + 4, '\0');
    char* in_ptr = const_cast<char*>(raw.data());
    std::size_t in_left = raw.size();
    char* out_ptr = out.data();
    std::size_t out_left = out.size();
    const std::size_t res = iconv(cd, &in_ptr, &in_left, &out_ptr, &out_left);
    iconv_close(cd);
    if (res == static_cast<std::size_t>(-1)) {
        return std::string(raw);
    }
    out.resize(out.size() - out_left);
    return out;
}

std::size_t RegisteredSceOpcodeCount() noexcept {
    return 112u;
}

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

    std::vector<std::size_t> offset_patch_locs;
    offset_patch_locs.reserve(sce.procs.size());
    for (const auto& [id, proc] : sce.procs) {
        WriteU32(out, id);
        offset_patch_locs.push_back(out.size());
        WriteU32(out, 0u);
        WriteFixedString(out, proc.name, 64);
    }

    std::size_t idx = 0;
    for (const auto& [id, proc] : sce.procs) {
        WriteU32At(out, offset_patch_locs[idx++], static_cast<std::uint32_t>(out.size()));
        WriteU32(out, id);
        const std::uint16_t name_len = static_cast<std::uint16_t>(proc.name.size() + 1);
        WriteU16(out, name_len);
        WriteFixedString(out, proc.name, name_len);
        WriteU16(out, proc.local_var_count);
        for (std::uint16_t v = 0; v < proc.local_var_count; ++v) {
            WriteU8(out, 1u);
            WriteU16(out, 4u);
            WriteU32(out, 0u);
        }
        WriteU32(out, static_cast<std::uint32_t>(proc.instructions.size()));
        out.insert(out.end(), proc.instructions.begin(), proc.instructions.end());
    }

    return out;
}

SceFile ParseSceFile(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 7 || std::memcmp(bytes.data(), "SCE\0", 4) != 0) {
        throw std::runtime_error("invalid PAL3 .sce header (expected 'SCE\\0')");
    }
    SceByteReader r{bytes, 4};
    SceFile sce;
    sce.version = r.ReadU8();
    if (sce.version != 1) {
        throw std::runtime_error("unsupported PAL3 .sce version");
    }
    const std::uint16_t proc_num = r.ReadU16();
    for (std::uint16_t i = 0; i < proc_num; ++i) {
        (void)r.ReadU32();  // id
        (void)r.ReadU32();  // offset
        (void)r.ReadFixedString(64);
    }

    for (std::uint16_t i = 0; i < proc_num; ++i) {
        SceProc proc;
        proc.id = r.ReadU32();
        const std::uint16_t name_len = r.ReadU16();
        proc.name = r.ReadFixedString(name_len);
        proc.local_var_count = r.ReadU16();
        for (std::uint16_t v = 0; v < proc.local_var_count; ++v) {
            (void)r.ReadU8();
            const std::uint16_t var_init_len = r.ReadU16();
            if (r.pos + var_init_len > r.buf.size()) {
                throw std::runtime_error("corrupt .sce local var table");
            }
            r.pos += var_init_len;
        }
        const std::uint32_t inst_size = r.ReadU32();
        if (r.pos + inst_size > r.buf.size()) {
            throw std::runtime_error("corrupt .sce instruction stream");
        }
        proc.instructions.assign(r.buf.begin() + static_cast<std::ptrdiff_t>(r.pos),
                                 r.buf.begin() + static_cast<std::ptrdiff_t>(r.pos + inst_size));
        r.pos += inst_size;
        sce.procs[proc.id] = std::move(proc);
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
    const auto len = static_cast<std::uint16_t>(s.size() + 1);
    WriteU16(len);
    for (char c : s) code_.push_back(static_cast<std::uint8_t>(c));
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
    EmitOpHeader(cmp_op, 1u);
    WriteI16(var);
    WriteI32(value);
}

void SceAssembler::EmitCompareVarVar(SceOpcode cmp_op, std::int16_t var_a, std::int16_t var_b) {
    EmitOpHeader(cmp_op, 3u);
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
    EmitOpHeader(SceOpcode::assign, 1u);
    WriteI16(var);
    WriteI32(value);
}

void SceAssembler::EmitAdd(std::int16_t var, std::int32_t value) {
    EmitOpHeader(SceOpcode::add, 1u);
    WriteI16(var);
    WriteI32(value);
}

void SceAssembler::EmitSub(std::int16_t var, std::int32_t value) {
    EmitOpHeader(SceOpcode::sub, 1u);
    WriteI16(var);
    WriteI32(value);
}

void SceAssembler::EmitBetween(std::int16_t var, std::int32_t lb, std::int32_t ub) {
    EmitOpHeader(SceOpcode::between, 1u);
    WriteI16(var);
    WriteI32(lb);
    WriteI32(ub);
}

void SceAssembler::EmitRolePathTo(std::int32_t role_id, std::int32_t nav_x, std::int32_t nav_z, std::int32_t flag) {
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

void SceAssembler::EmitRoleShowAction(std::int32_t role_id, std::string_view action_name, std::int32_t repeat_mode) {
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
    for (std::uint32_t id : monster_ids) {
        WriteU32(id);
    }
}

void SceAssembler::EmitCombat(std::array<std::uint32_t, 3> monster_ids) {
    EmitOpHeader(SceOpcode::combat);
    for (std::uint32_t id : monster_ids) {
        WriteU32(id);
    }
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

void SceAssembler::EmitPawnshopEntry(std::uint32_t shop_id) {
    EmitOpHeader(SceOpcode::pawnshop_entry);
    WriteU32(shop_id);
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
    std::string upper_cpk = active_cpk_;
    for (char& c : upper_cpk) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (upper_cpk == "Q01") {
        const std::int32_t cur = GetVar(kSceVarMainStory);
        if (proc_id == 501u && cur == 10101) {
            SetVar(kSceVarMainStory, 10202);
        } else if (proc_id == 1415u) {
            // Exact hooks from `OpenPAL3/yaobow/yaobow/src/openpal3/sce_proc_hooks.rs`
            if (cur == 11101) {
                SetVar(kSceVarMainStory, 11200);
            } else if (cur == 120301) {
                SetVar(kSceVarMainStory, 120302);
            }
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
            case SceOpcode::call_proc:
            case SceOpcode::call_proc_16: {
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
            case SceOpcode::rnd: {
                const std::int16_t var = r.ReadI16();
                const std::int32_t max_val = std::max(1, r.ReadI32());
                SetVar(var, static_cast<std::int32_t>(frame.pc) % max_val);
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
            case SceOpcode::role_path_to_20:
            case SceOpcode::role_path_to:
            case SceOpcode::role_move_to: {
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
            case SceOpcode::role_set_pos_21:
            case SceOpcode::role_set_pos: {
                const std::int32_t role_id = r.ReadI32();
                const std::int32_t nx = r.ReadI32();
                const std::int32_t nz = r.ReadI32();
                frame.pc = r.pos;
                services_.SceRoleSetPos(role_id, nx, nz);
                return true;
            }
            case SceOpcode::role_set_layer:
            case SceOpcode::role_set_layer_203: {
                const std::int32_t role_id = r.ReadI32();
                const std::int32_t layer = r.ReadI32();
                frame.pc = r.pos;
                services_.SceRoleSetLayer(role_id, layer);
                return true;
            }
            case SceOpcode::role_set_face:
            case SceOpcode::role_turn_face_a: {
                const std::int32_t role_id = r.ReadI32();
                const std::int32_t dir = r.ReadI32();
                frame.pc = r.pos;
                services_.SceRoleSetFace(role_id, dir);
                return true;
            }
            case SceOpcode::role_set_controller:
            case SceOpcode::role_ctrl_204: {
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
            case SceOpcode::dlg_62:
            case SceOpcode::dlg:
            case SceOpcode::dlg_time: {
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
            case SceOpcode::dlg_sel_65: {
                const std::uint16_t cnt = r.ReadU16();
                std::vector<std::string> choices(cnt);
                for (std::uint16_t i = 0; i < cnt; ++i) {
                    if (r.pos < r.buf.size() && r.buf[r.pos] <= 8u) {
                        (void)r.ReadU8();
                    }
                    choices[i] = r.ReadString();
                }
                frame.pc = r.pos;
                services_.SceShowDialogSelection(choices);
                active_tasks_.push_back({op, 0.0f, true, false, false, "DlgSel"});
                return false;
            }
            case SceOpcode::get_dlg_sel:
            case SceOpcode::get_time_sel: {
                const std::int16_t out_var = r.ReadI16();
                frame.pc = r.pos;
                SetVar(out_var, GetVar(kSceVarDlgSelect));
                return true;
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
            case SceOpcode::get_favor: {
                const std::int16_t out_var = r.ReadI16();
                (void)r.ReadI32();
                frame.pc = r.pos;
                SetVar(out_var, 50);
                return true;
            }
            case SceOpcode::add_skill: {
                const std::int32_t role_id = r.ReadI32();
                const std::int32_t skill_id = r.ReadI32();
                frame.pc = r.pos;
                services_.SceAddSkill(role_id, skill_id);
                return true;
            }
            case SceOpcode::get_favorite:
            case SceOpcode::give_cloth: {
                const std::int16_t out_var = r.ReadI16();
                frame.pc = r.pos;
                SetVar(out_var, 1);
                return true;
            }
            case SceOpcode::movie:
            case SceOpcode::movie_115: {
                const std::string movie = r.ReadString();
                frame.pc = r.pos;
                services_.ScePlayMovie(movie);
                return true;
            }
            case SceOpcode::load_scene: {
                const std::string cpk = r.ReadString();
                const std::string scn = r.ReadString();
                frame.pc = r.pos;
                services_.SceLoadScene(cpk, scn);
                return true;
            }
            case SceOpcode::role_active_28:
            case SceOpcode::role_active: {
                const std::int32_t role_id = r.ReadI32();
                const std::int32_t act = r.ReadI32();
                frame.pc = r.pos;
                services_.SceRoleActive(role_id, act != 0);
                return true;
            }
            case SceOpcode::music:
            case SceOpcode::music_133:
            case SceOpcode::play_sound: {
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
            case SceOpcode::combat:
            case SceOpcode::combat_84: {
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
            case SceOpcode::object_active_85:
            case SceOpcode::object_active: {
                const std::int32_t obj_id = r.ReadI32();
                const std::int32_t act = r.ReadI32();
                frame.pc = r.pos;
                services_.SceObjectActive(obj_id, act != 0);
                return true;
            }
            case SceOpcode::object_move_90:
            case SceOpcode::object_move:
            case SceOpcode::object_move_251: {
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
                const std::uint32_t antique_id =
                    (r.pos + 4 <= r.buf.size()) ? r.ReadU32() : 1u;
                frame.pc = r.pos;
                services_.SceStartAppraisalMinigame(antique_id);
                active_tasks_.push_back({op, 0.0f, false, false, true, "APPREntry"});
                return false;
            }
            case SceOpcode::pawnshop_entry: {
                const std::uint32_t shop_id = r.ReadU32();
                frame.pc = r.pos;
                services_.SceStartPawnshopMinigame(shop_id);
                active_tasks_.push_back({op, 0.0f, false, false, true, "PawnshopEntry"});
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
            case SceOpcode::show_chat_rest_119:
            case SceOpcode::show_chat_rest: {
                const std::string prompt = r.ReadString();
                const std::int32_t cost = r.ReadI32();
                const std::uint32_t no_money_proc = r.ReadU32();
                const std::uint32_t after_rest_proc = r.ReadU32();
                frame.pc = r.pos;
                services_.SceRestAtInn(prompt, cost, no_money_proc, after_rest_proc);
                return true;
            }
            // Zero-argument presentation/state opcodes from `OpenPAL3/yaobow/shared/src/scripting/sce/vm.rs`
            case SceOpcode::team_close:
            case SceOpcode::camera_push_state:
            case SceOpcode::camera_pop_state:
            case SceOpcode::fade_out:
            case SceOpcode::fade_in:
            case SceOpcode::start_hide_fight:
            case SceOpcode::combat_not_game_over:
            case SceOpcode::stop_music:
            case SceOpcode::switch_2_menu:
            case SceOpcode::full_team_att:
            case SceOpcode::xj_pic:
            case SceOpcode::init_flower:
            case SceOpcode::team_open_a:
            case SceOpcode::team_close_a: {
                frame.pc = r.pos;
                return true;
            }
            // Single i32/f32 presentation opcodes
            case SceOpcode::camera_focus_role:
            case SceOpcode::lk_ghost:
            case SceOpcode::role_stop:
            case SceOpcode::open_door:
            case SceOpcode::hy_mode:
            case SceOpcode::switch_rs:
            case SceOpcode::enable_sword:
            case SceOpcode::specify_compos:
            case SceOpcode::trigger:
            case SceOpcode::role_fade_out:
            case SceOpcode::role_fade_in:
            case SceOpcode::enable_sword_skill:
            case SceOpcode::snow:
            case SceOpcode::scr_eft:
            case SceOpcode::ceft:
            case SceOpcode::ceft_role:
            case SceOpcode::ceft_load:
            case SceOpcode::water_magic:
            case SceOpcode::camera_yaw:
            case SceOpcode::obj_not_load:
            case SceOpcode::role_end_action:
            case SceOpcode::camera_free: {
                (void)r.ReadU32();
                frame.pc = r.pos;
                return true;
            }
            // Two i32/f32 presentation opcodes
            case SceOpcode::full_role_att:
            case SceOpcode::role_emote:
            case SceOpcode::climb:
            case SceOpcode::quake:
            case SceOpcode::set_bigmap_element:
            case SceOpcode::entry_row:
            case SceOpcode::average_lv:
            case SceOpcode::role_overlap:
            case SceOpcode::role_scale:
            case SceOpcode::role_act_auto_stand:
            case SceOpcode::role_move_back:
            case SceOpcode::role_face_role: {
                (void)r.ReadU32();
                (void)r.ReadU32();
                frame.pc = r.pos;
                return true;
            }
            // Three i32/f32 presentation opcodes
            case SceOpcode::camera_push:
            case SceOpcode::hy_fly:
            case SceOpcode::rotate:
            case SceOpcode::rotate_inv:
            case SceOpcode::ceft_pos: {
                (void)r.ReadU32();
                (void)r.ReadU32();
                (void)r.ReadU32();
                frame.pc = r.pos;
                return true;
            }
            // Four i32/f32 presentation opcodes
            case SceOpcode::camera_rotate:
            case SceOpcode::camera_wag: {
                for (int i = 0; i < 4; ++i) (void)r.ReadU32();
                frame.pc = r.pos;
                return true;
            }
            // Six f32 camera set
            case SceOpcode::camera_set: {
                for (int i = 0; i < 6; ++i) (void)r.ReadU32();
                frame.pc = r.pos;
                return true;
            }
            case SceOpcode::note: {
                (void)r.ReadString();
                frame.pc = r.pos;
                return true;
            }
            case SceOpcode::caption: {
                (void)r.ReadString();
                (void)r.ReadI32();
                frame.pc = r.pos;
                return true;
            }
            case SceOpcode::set_role_texture:
            case SceOpcode::load_act: {
                (void)r.ReadI32();
                (void)r.ReadString();
                frame.pc = r.pos;
                return true;
            }
            case SceOpcode::get_switch: {
                (void)r.ReadString();
                (void)r.ReadI32();
                const std::int16_t out_var = r.ReadI16();
                frame.pc = r.pos;
                SetVar(out_var, 1);
                return true;
            }
            case SceOpcode::dist: {
                (void)r.ReadI16();
                (void)r.ReadI16();
                frame.pc = r.pos;
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
