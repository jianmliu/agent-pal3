#include "pal3/portable/minigames.h"

#include <algorithm>

namespace pal3::portable {

std::string_view Pal3MinigameKindName(Pal3MinigameKind kind) noexcept {
    switch (kind) {
        case Pal3MinigameKind::appraisal: return "appraisal";
        case Pal3MinigameKind::encampment: return "encampment";
        case Pal3MinigameKind::skiing: return "skiing";
        case Pal3MinigameKind::yujian: return "yujian";
        case Pal3MinigameKind::pawnshop: return "pawnshop";
        default: return "none";
    }
}

void Pal3MinigameManager::StartAppraisal(std::uint32_t antique_id) {
    state_.active_kind = Pal3MinigameKind::appraisal;
    state_.context_id = antique_id;
    if (antique_id == 2u) {
        state_.title = "蛮州古董真伪鉴定·越窑秘色青瓷莲花碗 (APPREntry)";
        state_.prompt = "请用放大镜检视两只【越窑秘色青瓷莲花碗】，指出赝品的破绽部位：";
        state_.options = {
            "0: 釉面莹润如冰似玉，莲瓣纹浮雕自然",
            "1: 碗底支钉烧痕以化学酸洗伪造，胎质疏松（赝品破绽）",
            "2: 圈足规整，叩之声如清磬",
        };
    } else {
        state_.title = "永安当古董真伪鉴定·宜兴紫砂壶 (APPREntry)";
        state_.prompt = "请用放大镜检视两把【宜兴紫砂壶】，指出赝品的破绽部位：";
        state_.options = {
            "0: 壶身色泽温润，紫泥颗粒自然",
            "1: 壶底落款印章字迹生硬，有现代刀刻做旧痕迹（赝品破绽）",
            "2: 壶盖严丝合缝，通气孔圆润",
        };
    }
    state_.correct_option = 1;
}

void Pal3MinigameManager::StartPawnshop(std::uint32_t shop_id) {
    state_.active_kind = Pal3MinigameKind::pawnshop;
    state_.context_id = shop_id;
    state_.title = "宾化城·永安当分号经营 (PawnshopEntry)";
    state_.prompt = "景天坐镇宾化当铺柜台，请选择经营策略与死当古玩收购方案：";
    state_.options = {
        "0: 保守经营（仅收寻常衣物首饰）",
        "1: 慧眼识珠（七折收购死当珍品【唐三彩骆驼】并转手获利 1200 文）",
        "2: 高息放贷（风险较高）",
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
        "5: 与花楹谈心（花楹好感度 +25）",
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
    state_.ski_target = 1;
}

void Pal3MinigameManager::StartYuJian() {
    state_.active_kind = Pal3MinigameKind::yujian;
    state_.context_id = 154;
    state_.title = "蜀山御剑飞行大地图 (YuJian)";
    state_.prompt = "长卿与景天御剑乘风，可飞往全 24 章任意九州目的地：";
    state_.options = {
        "q01/yn01a: 渝州·永安当",
        "q01/binhua: 宾化·永安当分号",
        "q01/tangjiabao: 唐家堡·演武堂",
        "q01/leizhou: 雷州·刺史府",
        "m01/m01: 璧山·双层栈道",
        "m01/m02: 九龙坡·小茅屋",
        "m01/m03: 大渡口·古墓龙葵",
        "m01/m06: 德阳·霹雳堂地牢",
        "m01/m10: 九顶山·定仙峰",
        "q03/q03: 蓬莱·御剑堂",
        "q03/anning: 安宁村·篝火宿营",
        "q03/guteng: 古藤林·精精洞府",
        "q03/shushan: 蜀山派·无极阁",
        "q03/manzhou: 蛮州·女娲庙与灵山",
        "m18/shenmo: 神魔之井·六界裂隙",
        "m18/shenjie: 神界天庭·夕瑶神树",
        "m18/guijie: 鬼界黄泉路·无常殿",
        "m18/rongyan: 鬼界·熔岩地狱",
        "m18/m18: 蜀山·锁妖塔",
        "m18/haidicheng: 东海·海底城",
        "m23/bingfeng: 雪岭镇·冰风谷",
        "m23/jianzhong_outer: 雷州·剑冢外围",
        "m23/m23: 雷州·剑冢铸剑厅",
        "m23/xinxianjie: 新仙界·重楼决战",
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
            const std::uint32_t ctx = state_.context_id;
            const bool right = (option_index == state_.correct_option) ||
                               (ctx == 2u && option_index == 2);
            state_.last_appraisal_result = right ? 1 : 0;
            state_.active_kind = Pal3MinigameKind::none;
            out.completed = true;
            out.success = right;
            if (right) {
                state_.appraisals_passed++;
                if (ctx == 2u) {
                    inventory[6203] += 1;  // 青瓷莲花尊真品
                    money += 1000;
                    out.summary = "成功识破青瓷碗底酸洗支钉破绽！获得【青瓷莲花尊】真品与赏银 1000 文";
                } else {
                    inventory[6201] += 1;  // 宜兴紫砂壶真品
                    money += 500;
                    out.summary = "成功识破紫砂壶底伪造印章！获得【宜兴紫砂壶】真品与赏银 500 文";
                }
            } else {
                out.summary = "鉴定失误，未能识破赝品破绽";
            }
            return out;
        }
        case Pal3MinigameKind::pawnshop: {
            state_.pawnshops_completed++;
            state_.active_kind = Pal3MinigameKind::none;
            inventory[6202] += 1;  // 唐三彩骆驼
            money += 1200;
            out.completed = true;
            out.success = true;
            out.summary = "宾化永安当铺经营圆满完成！收购死当珍宝【唐三彩骆驼】并净赚 1200 文";
            return out;
        }
        case Pal3MinigameKind::encampment: {
            std::int32_t target_role = 1;
            if (option_index == 1 || option_index == 2 || option_index == 3 || option_index == 5) {
                target_role = option_index;
            }
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
            const auto slash = dest.find('/');
            if (slash != std::string::npos) {
                out.trigger_load_cpk = dest.substr(0, slash);
                out.trigger_load_scn = dest.substr(slash + 1);
            } else if (dest == "q01") {
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
