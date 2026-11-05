#include "pal3/portable/minigames.h"

#include <algorithm>

namespace pal3::portable {

std::string_view Pal3MinigameKindName(Pal3MinigameKind kind) noexcept {
    switch (kind) {
        case Pal3MinigameKind::appraisal: return "appraisal";
        case Pal3MinigameKind::encampment: return "encampment";
        case Pal3MinigameKind::skiing: return "skiing";
        case Pal3MinigameKind::yujian: return "yujian";
        default: return "none";
    }
}

void Pal3MinigameManager::StartAppraisal(std::uint32_t antique_id) {
    state_.active_kind = Pal3MinigameKind::appraisal;
    state_.context_id = antique_id;
    state_.title = "永安当古董真伪鉴定 (APPREntry)";
    state_.prompt = "请用放大镜检视两把【宜兴紫砂壶】，指出赝品的破绽部位：";
    state_.options = {
        "0: 壶身色泽温润，紫泥颗粒自然",
        "1: 壶底落款印章字迹生硬，有现代刀刻做旧痕迹（赝品破绽）",
        "2: 壶盖严丝合缝，通气孔圆润",
    };
    state_.correct_option = 1;
}

void Pal3MinigameManager::StartEncampment(std::uint32_t encamp_id) {
    state_.active_kind = Pal3MinigameKind::encampment;
    state_.context_id = encamp_id;
    state_.title = "安宁村篝火宿营夜话 (ENCAMP_Entry)";
    state_.prompt = "夜色渐深，景天前去与守夜的伙伴谈心（提升对应角色好感度并恢复全队精气神）：";
    state_.options = {
        "1: 与唐雪见谈心（雪见好感度 +25）",
        "2: 与龙葵谈心（龙葵好感度 +25）",
        "3: 与紫萱谈心（紫萱好感度 +25）",
    };
    state_.correct_option = 1;
}

void Pal3MinigameManager::StartSkiing(std::uint32_t course_id) {
    state_.active_kind = Pal3MinigameKind::skiing;
    state_.context_id = course_id;
    state_.title = "雪岭镇·冰风谷极速滑雪 (SKEE_Entry)";
    state_.prompt = "脚踏木板顺雪坡疾驰而下，操控方向越过冰裂与巨石并拾取【雪莲精】：";
    state_.options = {
        "left: 向左变道避开冰锥",
        "jump: 腾空跃过冰谷裂缝并拾取雪莲精",
        "right: 向右冲刺抵达谷底出口",
    };
    state_.ski_progress = 0;
    state_.ski_target = 1;  // Single decisive jump/sprint or multi-step supported
}

void Pal3MinigameManager::StartYuJian() {
    state_.active_kind = Pal3MinigameKind::yujian;
    state_.context_id = 155;
    state_.title = "蜀山御剑飞行大地图 (YuJian)";
    state_.prompt = "长卿与景天御剑乘风，请选择九州目的地：";
    state_.options = {
        "q01: 渝州·永安当",
        "m01: 璧山·九龙坡",
        "q03: 蓬莱·安宁村",
        "m18: 蜀山·锁妖塔",
        "m23: 雷州·剑冢",
    };
}

MinigameActionOutcome Pal3MinigameManager::SubmitAction(
    std::string_view /*action*/,
    std::int32_t option_index,
    std::string_view destination_code,
    std::vector<Pal3RoleState>& party,
    std::map<std::int32_t, std::int32_t>& favor,
    std::map<std::int32_t, std::int32_t>& inventory,
    std::int32_t& money) {
    MinigameActionOutcome out;
    switch (state_.active_kind) {
        case Pal3MinigameKind::appraisal: {
            const bool right = (option_index == state_.correct_option);
            state_.last_appraisal_result = right ? 1 : 0;
            state_.active_kind = Pal3MinigameKind::none;
            out.completed = true;
            out.success = right;
            if (right) {
                state_.appraisals_passed++;
                inventory[6201] += 1;  // 宜兴紫砂壶真品
                money += 500;
                out.summary = "成功识破紫砂壶底伪造印章！获得【宜兴紫砂壶】真品与赏银 500 文";
            } else {
                out.summary = "鉴定失误，未能识破赝品破绽";
            }
            return out;
        }
        case Pal3MinigameKind::encampment: {
            const std::int32_t target_role =
                (option_index >= 1 && option_index <= 3) ? option_index : 1;
            favor[target_role] += 25;
            for (auto& m : party) {
                m.hp = m.max_hp;
                m.shen = m.max_shen;
                m.qi = m.max_qi;
            }
            state_.encampments_completed++;
            state_.active_kind = Pal3MinigameKind::none;
            out.completed = true;
            out.success = true;
            out.summary = "安宁村篝火夜话完成，同伴好感度 +25，全队精气神完全恢复";
            return out;
        }
        case Pal3MinigameKind::skiing: {
            state_.ski_progress++;
            inventory[6501] += 1;  // 雪莲精
            if (state_.ski_progress >= state_.ski_target) {
                state_.ski_runs_completed++;
                state_.active_kind = Pal3MinigameKind::none;
                out.completed = true;
                out.success = true;
                out.summary = "跃过冰风谷大裂缝顺利抵达谷底，沿途拾获【雪莲精】！";
            } else {
                out.completed = false;
                out.success = true;
                out.summary = "滑雪推进中";
            }
            return out;
        }
        case Pal3MinigameKind::yujian: {
            std::string dest(destination_code);
            if (dest.empty()) {
                if (option_index == 4) dest = "m23";
                else if (option_index == 3) dest = "m18";
                else if (option_index == 2) dest = "q03";
                else if (option_index == 1) dest = "m01";
                else dest = "m23";
            }
            state_.yujian_flights++;
            state_.active_kind = Pal3MinigameKind::none;
            out.completed = true;
            out.success = true;
            if (dest == "q01") {
                out.trigger_load_cpk = "q01";
                out.trigger_load_scn = "yn01a";
            } else {
                out.trigger_load_cpk = dest;
                out.trigger_load_scn = dest;
            }
            out.summary = "御剑飞行降落至 " + out.trigger_load_cpk + "/" + out.trigger_load_scn;
            return out;
        }
        default:
            return out;
    }
}

}  // namespace pal3::portable
