#include "pal3/portable/chapter_host.h"
#include "GLES3/gl3.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace pal3::portable;

namespace {

std::string EscapeJson(std::string_view input) {
    std::ostringstream out;
    for (unsigned char c : input) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    out << "\\u00"
                        << "0123456789abcdef"[(c >> 4) & 0xF]
                        << "0123456789abcdef"[c & 0xF];
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    return out.str();
}

std::optional<std::string> ExtractJsonString(std::string_view json, std::string_view key) {
    const std::string pattern = "\"" + std::string(key) + "\"";
    auto pos = json.find(pattern);
    if (pos == std::string_view::npos) return std::nullopt;
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string_view::npos) return std::nullopt;
    pos = json.find('"', pos + 1);
    if (pos == std::string_view::npos) return std::nullopt;
    std::string result;
    for (std::size_t i = pos + 1; i < json.size(); ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) {
            result.push_back(json[++i]);
        } else if (json[i] == '"') {
            return result;
        } else {
            result.push_back(json[i]);
        }
    }
    return std::nullopt;
}

std::optional<double> ExtractJsonNumber(std::string_view json, std::string_view key) {
    const std::string pattern = "\"" + std::string(key) + "\"";
    auto pos = json.find(pattern);
    if (pos == std::string_view::npos) return std::nullopt;
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string_view::npos) return std::nullopt;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    if (pos >= json.size()) return std::nullopt;
    char* end = nullptr;
    const std::string tail(json.substr(pos));
    const double val = std::strtod(tail.c_str(), &end);
    if (end == tail.c_str()) return std::nullopt;
    return val;
}

struct AgentPal3Session {
    std::filesystem::path work_dir{"/tmp/agent-pal3-bench"};
    std::string mode{"normal"};
    ChapterHost host;
    std::map<std::string, Pal3HostCheckpoint> checkpoints;
    std::string last_event{"initialized"};

    void WriteAndLoadCpkSuite() {
        const auto basedata_dir = work_dir / "basedata";
        const auto scene_dir = work_dir / "scene";
        std::filesystem::create_directories(basedata_dir);
        std::filesystem::create_directories(scene_dir);

        const auto suite = BuildFullPal3CampaignCpkSuite();

        auto write_cpk = [&](const std::filesystem::path& path,
                             const std::vector<std::uint8_t>& bytes) {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
            return path;
        };

        const auto base_path = write_cpk(basedata_dir / "basedata.cpk", suite.basedata_cpk);
        const auto q01_path = write_cpk(scene_dir / "q01.cpk", suite.q01_cpk);
        const auto m01_path = write_cpk(scene_dir / "m01.cpk", suite.m01_cpk);
        const auto q03_path = write_cpk(scene_dir / "q03.cpk", suite.q03_cpk);
        const auto m18_path = write_cpk(scene_dir / "m18.cpk", suite.m18_cpk);
        const auto m23_path = write_cpk(scene_dir / "m23.cpk", suite.m23_cpk);

        std::ifstream base_in(base_path, std::ios::binary);
        std::ifstream q01_in(q01_path, std::ios::binary);
        std::ifstream m01_in(m01_path, std::ios::binary);
        std::ifstream q03_in(q03_path, std::ios::binary);
        std::ifstream m18_in(m18_path, std::ios::binary);
        std::ifstream m23_in(m23_path, std::ios::binary);

        host.InitializeFromCpkStreams(base_in, q01_in, m01_in, q03_in, m18_in, m23_in);
        host.BeginNewGame();
    }

    std::string BuildObservationJson(bool ok = true, std::string_view error_msg = "") const {
        std::ostringstream out;
        out << std::fixed << std::setprecision(2);
        out << "{";
        out << "\"ok\":" << (ok ? "true" : "false") << ",";
        if (!error_msg.empty()) {
            out << "\"error\":\"" << EscapeJson(error_msg) << "\",";
        }
        out << "\"mode\":\"" << EscapeJson(mode) << "\",";
        out << "\"event\":\"" << EscapeJson(last_event) << "\",";
        out << "\"cpk\":\"" << EscapeJson(host.current_cpk) << "\",";
        out << "\"scn\":\"" << EscapeJson(host.current_scn) << "\",";
        out << "\"title\":\"" << EscapeJson(host.current_title) << "\",";
        out << "\"bgm\":\"" << EscapeJson(host.active_bgm) << "\",";
        if (mode != "pure") {
            out << "\"story_progress\":" << host.StoryProgress() << ",";
        }
        out << "\"game_cleared\":" << (host.game_cleared ? "true" : "false") << ",";
        out << "\"ending_name\":\"" << EscapeJson(host.ending_name) << "\",";

        // Leader state
        const auto& leader = host.LeaderRole();
        std::array<std::int32_t, 2> nav_tile{0, 0};
        if (!host.active_nav.layers.empty()) {
            nav_tile = WorldToNavTile(host.active_nav.layers[host.leader_nav_layer],
                                      host.movement_state.position[0],
                                      host.movement_state.position[2]);
        }
        out << "\"leader\":{"
            << "\"role_id\":" << leader.role_id << ","
            << "\"model_code\":\"" << EscapeJson(leader.model_code) << "\","
            << "\"name\":\"" << EscapeJson(leader.name) << "\","
            << "\"element\":\"" << Pal3ElementName(leader.element) << "\","
            << "\"nav_layer\":" << host.leader_nav_layer << ","
            << "\"nav_tile\":[" << nav_tile[0] << "," << nav_tile[1] << "],"
            << "\"position\":[" << host.movement_state.position[0] << ","
            << host.movement_state.position[1] << "," << host.movement_state.position[2] << "],"
            << "\"rotation_yaw\":" << (host.movement_state.yaw * 57.2957795f) << ","
            << "\"mv3_pose_checksum\":" << host.leader_mv3_pose.pose_checksum << "},";

        // Party & Five-Element Taiji Formation + Favorability + Demon Sword
        out << "\"party\":[";
        for (std::size_t i = 0; i < host.party.size(); ++i) {
            const auto& p = host.party[i];
            if (i > 0) out << ",";
            out << "{"
                << "\"role_id\":" << p.role_id << ","
                << "\"model_code\":\"" << EscapeJson(p.model_code) << "\","
                << "\"name\":\"" << EscapeJson(p.name) << "\","
                << "\"element\":\"" << Pal3ElementName(p.element) << "\","
                << "\"in_team\":" << (p.in_team ? "true" : "false") << ","
                << "\"is_red_longkui\":" << (p.is_red_longkui ? "true" : "false") << ","
                << "\"level\":" << p.level << ","
                << "\"hp\":" << p.hp << ",\"max_hp\":" << p.max_hp << ","
                << "\"shen\":" << p.shen << ",\"max_shen\":" << p.max_shen << ","
                << "\"qi\":" << p.qi << ",\"max_qi\":" << p.max_qi << ","
                << "\"attack\":" << p.attack << ",\"defense\":" << p.defense << ","
                << "\"formation_slot\":\"" << TaijiSlotName(p.formation_slot) << "\"}";
        }
        out << "],";

        out << "\"favor\":{"
            << "\"xuejian\":" << (host.favor.count(1) ? host.favor.at(1) : 0) << ","
            << "\"longkui\":" << (host.favor.count(2) ? host.favor.at(2) : 0) << ","
            << "\"zixuan\":" << (host.favor.count(3) ? host.favor.at(3) : 0) << ","
            << "\"huaying\":" << (host.favor.count(5) ? host.favor.at(5) : 0) << "},";

        out << "\"demon_sword\":{"
            << "\"obtained\":" << (host.demon_sword.obtained ? "true" : "false") << ","
            << "\"zhenyaojian_obtained\":" << (host.demon_sword.zhenyaojian_obtained ? "true" : "false") << ","
            << "\"equipped_divine_sword\":\"" << EscapeJson(host.demon_sword.equipped_divine_sword) << "\","
            << "\"purification_level\":" << host.demon_sword.purification_level << ","
            << "\"spirit_atk_bonus\":" << host.demon_sword.spirit_atk_bonus << ","
            << "\"infused_elements\":[";
        for (std::size_t i = 0; i < host.demon_sword.infused_elements.size(); ++i) {
            if (i > 0) out << ",";
            out << "\"" << Pal3ElementName(host.demon_sword.infused_elements[i]) << "\"";
        }
        out << "]},";

        out << "\"inventory\":{"
            << "\"money\":" << host.money << ","
            << "\"items\":[";
        bool first_item = true;
        for (const auto& [item_id, cnt] : host.inventory) {
            if (cnt <= 0) continue;
            if (!first_item) out << ",";
            first_item = false;
            const auto* rec = host.database.FindItem(item_id);
            out << "{\"id\":" << item_id << ",\"name\":\""
                << EscapeJson(rec ? rec->name : "Unknown") << "\",\"count\":" << cnt << "}";
        }
        out << "]},";

        // Dialog & Choice state
        out << "\"dialog\":{"
            << "\"choice_pending\":" << (host.dialog.choice_pending ? "true" : "false") << ","
            << "\"choices\":[";
        for (std::size_t i = 0; i < host.dialog.choices.size(); ++i) {
            if (i > 0) out << ",";
            out << "\"" << EscapeJson(host.dialog.choices[i]) << "\"";
        }
        out << "],\"history\":[";
        for (std::size_t i = 0; i < host.dialog.recent_history.size(); ++i) {
            const auto& d = host.dialog.recent_history[i];
            if (i > 0) out << ",";
            out << "{\"speaker\":\"" << EscapeJson(d.speaker) << "\",\"text\":\""
                << EscapeJson(d.text) << "\"}";
        }
        out << "]},";

        // Scene NPCs & Objects
        out << "\"npcs\":[";
        bool first_npc = true;
        for (std::size_t i = 0; i < host.scene_npcs.size(); ++i) {
            const auto& n = host.scene_npcs[i];
            if (!n.active) continue;
            if (!first_npc) out << ",";
            first_npc = false;
            const float dist = std::hypot(n.position[0] - host.movement_state.position[0],
                                          n.position[2] - host.movement_state.position[2]);
            out << "{\"id\":" << n.id << ",\"name\":\"" << EscapeJson(n.name)
                << "\",\"layer\":" << n.nav_layer << ",\"distance\":" << dist
                << ",\"position\":[" << n.position[0] << ","
                << n.position[1] << "," << n.position[2] << "]}";
        }
        out << "],\"objects\":[";
        bool first_obj = true;
        for (std::size_t i = 0; i < host.scene_objects.size(); ++i) {
            const auto& o = host.scene_objects[i];
            if (!o.active) continue;
            if (!first_obj) out << ",";
            first_obj = false;
            std::string_view type_name = "object";
            if (o.node_type == static_cast<std::uint16_t>(ScnNodeType::tile_trigger)) {
                type_name = "tile_trigger";
            } else if (o.node_type == static_cast<std::uint16_t>(ScnNodeType::ladder)) {
                type_name = "ladder";
            } else if (o.node_type == static_cast<std::uint16_t>(ScnNodeType::mechanism)) {
                type_name = "mechanism";
            } else if (o.node_type == static_cast<std::uint16_t>(ScnNodeType::aabb_trigger)) {
                type_name = "aabb";
            } else if (o.node_type == static_cast<std::uint16_t>(ScnNodeType::elevator)) {
                type_name = "elevator";
            }
            const float dist = std::hypot(o.position[0] - host.movement_state.position[0],
                                          o.position[2] - host.movement_state.position[2]);
            out << "{\"id\":" << o.id << ",\"name\":\"" << EscapeJson(o.name)
                << "\",\"node_type\":" << o.node_type << ",\"type_name\":\"" << type_name
                << "\",\"layer\":" << o.nav_layer << ",\"distance\":" << dist
                << ",\"cvd_anim_time\":" << o.cvd_anim_time_sec << ",\"position\":["
                << o.position[0] << "," << o.position[1] << "," << o.position[2] << "]}";
        }
        out << "],";

        // ATB & Five-Element Taiji Formation Combat state
        const auto& cs = host.combat.State();
        out << "\"combat\":{"
            << "\"active\":" << (cs.active ? "true" : "false") << ","
            << "\"round\":" << cs.current_round << ","
            << "\"max_round\":" << cs.max_round.value_or(0) << ","
            << "\"awaiting_role_id\":" << cs.awaiting_role_id << ","
            << "\"victories\":" << cs.total_victories << ","
            << "\"combos_used\":" << cs.total_combos_used << ",";
        if (cs.active) {
            out << "\"available_skills\":[";
            bool first_sk = true;
            std::vector<std::uint32_t> seen_skills;
            for (const auto& m : host.party) {
                if (!m.in_team) continue;
                for (std::uint32_t sid : m.learned_skills) {
                    if (std::find(seen_skills.begin(), seen_skills.end(), sid) != seen_skills.end()) {
                        continue;
                    }
                    const auto* sk = host.database.FindSkill(sid);
                    if (!sk) continue;
                    seen_skills.push_back(sid);
                    if (!first_sk) out << ",";
                    first_sk = false;
                    out << "{\"skill_id\":" << sk->skill_id << ",\"name\":\"" << EscapeJson(sk->name)
                        << "\",\"element\":\"" << Pal3ElementName(sk->element) << "\",\"action\":\""
                        << (sk->is_combo ? "combo" : "magic") << "\"}";
                }
            }
            out << "],";
        }
        out << "\"enemies\":[";
        for (std::size_t i = 0; i < cs.enemies.size(); ++i) {
            const auto& e = cs.enemies[i];
            if (i > 0) out << ",";
            out << "{\"slot\":" << e.slot_index << ",\"id\":" << e.monster_id << ",\"name\":\""
                << EscapeJson(e.name) << "\",\"element\":\"" << Pal3ElementName(e.element)
                << "\",\"hp\":" << e.hp << ",\"max_hp\":" << e.max_hp << "}";
        }
        out << "]},";

        // Minigame state (Appraisal / Pawnshop / Encampment / Skiing / YuJian)
        const auto& ms = host.minigames.State();
        out << "\"minigame\":{"
            << "\"active\":" << (host.minigames.IsActive() ? "true" : "false") << ","
            << "\"kind\":\"" << Pal3MinigameKindName(ms.active_kind) << "\","
            << "\"title\":\"" << EscapeJson(ms.title) << "\",";
        if (host.minigames.IsActive()) {
            out << "\"prompt\":\"" << EscapeJson(ms.prompt) << "\",\"options\":[";
            for (std::size_t i = 0; i < ms.options.size(); ++i) {
                if (i > 0) out << ",";
                out << "\"" << EscapeJson(ms.options[i]) << "\"";
            }
            out << "],";
        }
        out << "\"appraisals_passed\":" << ms.appraisals_passed << ","
            << "\"pawnshops_completed\":" << ms.pawnshops_completed << ","
            << "\"encampments_completed\":" << ms.encampments_completed << ","
            << "\"ski_runs_completed\":" << ms.ski_runs_completed << ","
            << "\"yujian_flights\":" << ms.yujian_flights << "},";

        if (mode != "pure") {
            out << "\"diagnostics\":{"
                << "\"sce_run_mode\":" << host.sce_vm->RunMode() << ","
                << "\"sce_opcodes_registered\":" << RegisteredSceOpcodeCount() << ","
                << "\"sce_idle\":" << (host.sce_vm->IsIdle() ? "true" : "false") << "},";
        }

        out << "\"gles3\":{"
            << "\"frames\":" << host.total_frames << ","
            << "\"draw_calls\":" << host.total_draw_calls << ","
            << "\"world_vertices\":" << host.displayed_world_scene.vertices.size() << ","
            << "\"mv3_models_loaded\":" << host.role_mv3_models.size() << ","
            << "\"original_cpk_mounted\":" << (!host.original_game_dir.empty() ? "true" : "false") << ","
            << "\"chapters_mounted\":" << host.scene_catalog.size() << "}"
            << "}";
        return out.str();
    }

    std::string HandleCommand(std::string_view line) {
        const auto cmd_opt = ExtractJsonString(line, "cmd");
        if (!cmd_opt) {
            return BuildObservationJson(false, "missing 'cmd' field");
        }
        const std::string& cmd = *cmd_opt;

        if (mode == "pure" &&
            (cmd == "load_scene" || cmd == "set_story_var" || cmd == "teleport")) {
            return BuildObservationJson(false, "command '" + cmd + "' is disabled in --mode pure");
        }

        try {
            if (cmd == "observe" || cmd == "noop") {
                last_event = "observed";
                return BuildObservationJson(true);
            }
            if (cmd == "reset") {
                host.BeginNewGame();
                last_event = "reset";
                return BuildObservationJson(true);
            }
            if (cmd == "step" || cmd == "wait") {
                const std::uint32_t frames =
                    static_cast<std::uint32_t>(ExtractJsonNumber(line, "frames").value_or(1.0));
                host.StepFrames(std::max(1u, frames));
                last_event = "stepped";
                return BuildObservationJson(true);
            }
            if (cmd == "move_to" || cmd == "walk_to") {
                const float x = static_cast<float>(ExtractJsonNumber(line, "x").value_or(0.0));
                const float z = static_cast<float>(ExtractJsonNumber(line, "z").value_or(0.0));
                std::optional<std::uint32_t> layer;
                if (auto l_num = ExtractJsonNumber(line, "layer")) {
                    layer = static_cast<std::uint32_t>(*l_num);
                }
                const std::uint32_t max_steps =
                    static_cast<std::uint32_t>(ExtractJsonNumber(line, "max_steps").value_or(160.0));
                last_event = host.MoveLeaderTo(x, z, layer, max_steps);
                return BuildObservationJson(true);
            }
            if (cmd == "interact") {
                const std::string target_kind =
                    ExtractJsonString(line, "target_type")
                        .value_or(ExtractJsonString(line, "target_kind").value_or(""));
                const std::int32_t target_id =
                    static_cast<std::int32_t>(ExtractJsonNumber(line, "id").value_or(-1.0));
                last_event = host.Interact(target_kind, target_id);
                return BuildObservationJson(true);
            }
            if (cmd == "choose") {
                const std::int32_t index =
                    static_cast<std::int32_t>(ExtractJsonNumber(line, "index").value_or(0.0));
                last_event = host.ChooseDialogOption(index);
                return BuildObservationJson(true);
            }
            if (cmd == "switch_leader") {
                const std::int32_t role_id = static_cast<std::int32_t>(
                    ExtractJsonNumber(line, "role_id")
                        .value_or(ExtractJsonNumber(line, "index").value_or(0.0)));
                last_event = host.SwitchLeader(role_id);
                return BuildObservationJson(true);
            }
            if (cmd == "toggle_longkui_form") {
                last_event = host.ToggleLongkuiForm();
                return BuildObservationJson(true);
            }
            if (cmd == "combat_action") {
                const std::string action = ExtractJsonString(line, "action").value_or("attack");
                const std::uint32_t skill_id =
                    static_cast<std::uint32_t>(ExtractJsonNumber(line, "skill_id").value_or(0.0));
                const std::uint32_t target_slot = static_cast<std::uint32_t>(
                    ExtractJsonNumber(line, "target_slot").value_or(0.0));
                const std::string slot_name = ExtractJsonString(line, "slot").value_or("center");
                const auto outcome =
                    host.ExecuteCombatAction(action, skill_id, target_slot, slot_name);
                last_event = "combat:" + outcome.action_summary;
                return BuildObservationJson(true);
            }
            if (cmd == "minigame_action") {
                const std::string action = ExtractJsonString(line, "action").value_or("submit");
                const std::int32_t option = static_cast<std::int32_t>(
                    ExtractJsonNumber(line, "option")
                        .value_or(ExtractJsonNumber(line, "choice").value_or(1.0)));
                const std::string dest = ExtractJsonString(line, "destination").value_or("");
                const auto outcome = host.ExecuteMinigameAction(action, option, dest);
                last_event = "minigame:" + outcome.summary;
                return BuildObservationJson(true);
            }
            if (cmd == "save_checkpoint") {
                const std::string slot =
                    ExtractJsonString(line, "slot")
                        .value_or(ExtractJsonString(line, "name").value_or("default"));
                checkpoints[slot] = host.SaveCheckpoint();
                last_event = "checkpoint_saved:" + slot;
                return BuildObservationJson(true);
            }
            if (cmd == "load_checkpoint") {
                const std::string slot =
                    ExtractJsonString(line, "slot")
                        .value_or(ExtractJsonString(line, "name").value_or("default"));
                auto it = checkpoints.find(slot);
                if (it == checkpoints.end()) {
                    return BuildObservationJson(false, "unknown checkpoint slot: " + slot);
                }
                host.LoadCheckpoint(it->second);
                last_event = "checkpoint_loaded:" + slot;
                return BuildObservationJson(true);
            }
            if (cmd == "load_scene") {
                const std::string cpk = ExtractJsonString(line, "cpk").value_or("q01");
                const std::string scn = ExtractJsonString(line, "scn").value_or("yn01a");
                host.SceLoadScene(cpk, scn);
                last_event = "scene_loaded:" + cpk + "/" + scn;
                return BuildObservationJson(true);
            }
            if (cmd == "set_story_var") {
                const std::int32_t val =
                    static_cast<std::int32_t>(ExtractJsonNumber(line, "value").value_or(10101.0));
                host.SetStoryProgress(val);
                last_event = "story_var_set:" + std::to_string(val);
                return BuildObservationJson(true);
            }
            if (cmd == "mount_game_dir") {
                const std::string dir = ExtractJsonString(line, "path").value_or("");
                const bool mounted = host.MountOriginalGameDirectory(dir);
                last_event = mounted ? ("mounted_game_dir:" + dir) : "mount_game_dir_failed";
                return BuildObservationJson(mounted, mounted ? "" : "game_dir does not exist");
            }
            if (cmd == "render_frame") {
                const std::string path =
                    ExtractJsonString(line, "path").value_or("/tmp/agent-pal3-frame.bmp");
                const int w = static_cast<int>(ExtractJsonNumber(line, "width").value_or(640.0));
                const int h = static_cast<int>(ExtractJsonNumber(line, "height").value_or(360.0));
                std::optional<float> yaw_opt;
                if (auto yv = ExtractJsonNumber(line, "yaw")) {
                    yaw_opt = static_cast<float>(*yv);
                }
                const float pitch =
                    static_cast<float>(ExtractJsonNumber(line, "pitch").value_or(24.0));
                const float dist =
                    static_cast<float>(ExtractJsonNumber(line, "distance").value_or(96.0));
                const bool ok = host.RenderFrameToBmp(path, w, h, yaw_opt, pitch, dist);
                return std::string("{\"ok\":") + (ok ? "true" : "false") +
                       ",\"event\":\"frame_rendered\",\"path\":\"" + EscapeJson(path) +
                       "\",\"width\":" + std::to_string(w) + ",\"height\":" + std::to_string(h) +
                       "}";
            }
            if (cmd == "render_substeps") {
                const std::string prefix =
                    ExtractJsonString(line, "prefix").value_or("/tmp/agent-pal3-sub");
                const float from_x =
                    static_cast<float>(ExtractJsonNumber(line, "from_x").value_or(0.0));
                const float from_y =
                    static_cast<float>(ExtractJsonNumber(line, "from_y").value_or(0.0));
                const float from_z =
                    static_cast<float>(ExtractJsonNumber(line, "from_z").value_or(0.0));
                const float to_x =
                    static_cast<float>(ExtractJsonNumber(line, "to_x").value_or(0.0));
                const float to_y =
                    static_cast<float>(ExtractJsonNumber(line, "to_y").value_or(0.0));
                const float to_z =
                    static_cast<float>(ExtractJsonNumber(line, "to_z").value_or(0.0));
                const int w = static_cast<int>(ExtractJsonNumber(line, "width").value_or(512.0));
                const int h = static_cast<int>(ExtractJsonNumber(line, "height").value_or(288.0));
                const int max_sub =
                    static_cast<int>(ExtractJsonNumber(line, "max_substeps").value_or(3.0));
                return host.RenderSubstepsToBmp(
                    prefix, from_x, from_y, from_z, to_x, to_y, to_z, w, h, max_sub);
            }
            return BuildObservationJson(false, "unknown command: " + cmd);
        } catch (const std::exception& ex) {
            return BuildObservationJson(false, ex.what());
        }
    }
};

}  // namespace

int main(int argc, char** argv) {
    AgentPal3Session session;
    std::filesystem::path game_dir;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--work-dir" && i + 1 < argc) {
            session.work_dir = argv[++i];
        } else if (arg == "--mode" && i + 1 < argc) {
            session.mode = argv[++i];
        } else if (arg == "--game-dir" && i + 1 < argc) {
            game_dir = argv[++i];
        } else if (arg == "--full-campaign") {
            session.host.enforce_full_campaign = true;
        }
    }

    if (game_dir.empty()) {
        if (const char* env_dir = std::getenv("PAL3_GAME_DIR"); env_dir && *env_dir) {
            game_dir = env_dir;
        } else {
            for (const char* candidate : {
                     "/Volumes/T7-Data/pal/pal3-data/PAL3",
                     "/Users/jianmingliu/Downloads/xjqxzyx/PAL3",
                 }) {
                std::error_code ec;
                if (std::filesystem::exists(std::filesystem::path(candidate) / "basedata", ec)) {
                    game_dir = candidate;
                    break;
                }
            }
        }
    }

    session.WriteAndLoadCpkSuite();
    if (!game_dir.empty()) {
        session.host.MountOriginalGameDirectory(game_dir);
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        if (line == "quit" || line == "exit") break;
        std::cout << session.HandleCommand(line) << "\n" << std::flush;
    }
    return 0;
}
