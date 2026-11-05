#include "pal3/portable/database_combat.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pal3::portable {

std::string_view Pal3ElementName(Pal3Element elem) noexcept {
    switch (elem) {
        case Pal3Element::water: return "水";
        case Pal3Element::fire: return "火";
        case Pal3Element::thunder: return "雷";
        case Pal3Element::wind: return "风";
        case Pal3Element::earth: return "土";
        default: return "无";
    }
}

bool DoesElementCounter(Pal3Element attacker, Pal3Element defender) noexcept {
    switch (attacker) {
        case Pal3Element::water: return defender == Pal3Element::fire;
        case Pal3Element::fire: return defender == Pal3Element::thunder;
        case Pal3Element::thunder: return defender == Pal3Element::wind;
        case Pal3Element::wind: return defender == Pal3Element::earth;
        case Pal3Element::earth: return defender == Pal3Element::water;
        default: return false;
    }
}

float ComputeElementMultiplier(Pal3Element attacker, Pal3Element defender) noexcept {
    if (DoesElementCounter(attacker, defender)) return 1.5f;
    if (DoesElementCounter(defender, attacker)) return 0.75f;
    return 1.0f;
}

std::string_view TaijiSlotName(TaijiSlot slot) noexcept {
    switch (slot) {
        case TaijiSlot::center: return "center";
        case TaijiSlot::water: return "water";
        case TaijiSlot::fire: return "fire";
        case TaijiSlot::thunder: return "thunder";
        case TaijiSlot::wind: return "wind";
        case TaijiSlot::earth: return "earth";
    }
    return "center";
}

TaijiSlot ParseTaijiSlot(std::string_view name) noexcept {
    if (name == "water" || name == "水") return TaijiSlot::water;
    if (name == "fire" || name == "火") return TaijiSlot::fire;
    if (name == "thunder" || name == "雷") return TaijiSlot::thunder;
    if (name == "wind" || name == "风") return TaijiSlot::wind;
    if (name == "earth" || name == "土") return TaijiSlot::earth;
    return TaijiSlot::center;
}

Pal3Element SlotBoostElement(TaijiSlot slot) noexcept {
    switch (slot) {
        case TaijiSlot::water: return Pal3Element::water;
        case TaijiSlot::fire: return Pal3Element::fire;
        case TaijiSlot::thunder: return Pal3Element::thunder;
        case TaijiSlot::wind: return Pal3Element::wind;
        case TaijiSlot::earth: return Pal3Element::earth;
        default: return Pal3Element::none;
    }
}

Pal3Database::Pal3Database() {
    // Five-Element Magic (`五灵仙术`) & Personal Skills (`特技`) & Combined Attacks (`合击技`)
    skills_ = {
        {3001, {3001, "风咒", Pal3Element::wind, false, false, 15, 110, {0}}},
        {3002, {3002, "仙风云体", Pal3Element::wind, false, true, 25, 165, {0}}},
        {3003, {3003, "风卷尘生", Pal3Element::wind, false, true, 45, 280, {0}}},
        {3101, {3101, "土咒", Pal3Element::earth, false, false, 15, 115, {1}}},
        {3102, {3102, "泰山压顶", Pal3Element::earth, false, true, 35, 240, {1}}},
        {3201, {3201, "冰咒", Pal3Element::water, false, false, 15, 120, {2, 3}}},
        {3202, {3202, "惊雷闪", Pal3Element::thunder, false, true, 30, 235, {2}}},
        {3301, {3301, "万蚁蚀象", Pal3Element::water, false, true, 40, 270, {3}}},
        {3401, {3401, "炎宿", Pal3Element::fire, false, false, 20, 150, {4}}},
        {3402, {3402, "天师符法", Pal3Element::fire, false, false, 35, 290, {4}}},
        {3501, {3501, "洒金笺", Pal3Element::wind, false, true, 20, 195, {0}}},
        // Combined Formation Attacks (`五灵法阵合击技`)
        {4001, {4001, "断空噬地", Pal3Element::wind, true, true, 30, 520, {0, 1}}},
        {4002, {4002, "飞鸟裂天", Pal3Element::thunder, true, true, 40, 860, {0, 1, 2}}},
        {4003, {4003, "灵奴奈落", Pal3Element::fire, true, true, 50, 1250, {0, 3, 4}}},
    };

    items_ = {
        {6101, {6101, "止血草", "consumable", 150, 0, 0, 50}},
        {6102, {6102, "鼠儿果", "consumable", 0, 80, 0, 60}},
        {6201, {6201, "宜兴紫砂壶", "antique", 0, 0, 0, 1000}},
        {6202, {6202, "唐三彩骆驼", "antique", 0, 0, 0, 2500}},
        {6203, {6203, "青瓷莲花尊", "antique", 0, 0, 0, 4000}},
        {6301, {6301, "魔剑", "weapon", 0, 0, 45, 0}},
        {6302, {6302, "镇妖剑", "weapon", 0, 0, 85, 0}},
        {6401, {6401, "雷灵珠", "spirit_pearl", 0, 0, 20, 0}},
        {6402, {6402, "风灵珠", "spirit_pearl", 0, 0, 25, 0}},
        {6403, {6403, "圣灵珠", "spirit_pearl", 0, 0, 35, 0}},
        {6404, {6404, "土灵珠", "spirit_pearl", 0, 0, 20, 0}},
        {6405, {6405, "火灵珠", "spirit_pearl", 0, 0, 25, 0}},
        {6406, {6406, "水灵珠", "spirit_pearl", 0, 0, 30, 0}},
        {6501, {6501, "雪莲精", "consumable", 500, 300, 0, 800}},
    };

    monsters_ = {
        // Stage 1: Yuzhou Bamboo Forest - Chonglou 3-round encounter
        {9001, {9001, "魔尊·重楼(初战)", Pal3Element::fire, true, 9999, 75, 60, 75, 500, 800, 0}},
        // Stage 2: Bishan & Jiulongpo - Pilittang Disciple Leader
        {9002, {9002, "霹雳堂头目", Pal3Element::thunder, true, 680, 55, 25, 52, 350, 500, 6101}},
        // Stage 3: Penglai / Guteng Forest - Ancient Vine Immortal (`古藤仙人`)
        {9003, {9003, "古藤仙人", Pal3Element::earth, true, 1250, 72, 35, 58, 800, 1200, 6401}},
        // Stage 4: Demon-Locking Tower 4F - Emperor of Sky Demons (`天妖皇`)
        {9004, {9004, "天妖皇", Pal3Element::wind, true, 1950, 95, 45, 65, 1600, 2500, 6302}},
        // Stage 5: Sword Tomb Finale - Evil Sword Immortal (`邪剑仙`) & New Immortal Realm Chonglou (`重楼·终战`)
        {9005, {9005, "邪剑仙", Pal3Element::water, true, 2600, 115, 55, 72, 3000, 5000, 6403}},
        {9006, {9006, "魔尊·重楼(新仙界)", Pal3Element::fire, true, 3200, 130, 65, 80, 5000, 9999, 6402}},
        // Additional Bosses across the full 24-Chapter PAL3 Campaign
        {9007, {9007, "唐门长老·唐泰", Pal3Element::earth, true, 520, 52, 22, 50, 300, 450, 6101}},
        {9008, {9008, "雷州刺史少爷·云霆", Pal3Element::thunder, true, 620, 60, 28, 56, 500, 800, 6401}},
        {9009, {9009, "霹雳堂堂主·罗如烈", Pal3Element::earth, true, 780, 65, 30, 54, 650, 1000, 6404}},
        {9010, {9010, "九顶山·人面兽", Pal3Element::wind, true, 820, 68, 32, 58, 700, 1100, 6102}},
        {9011, {9011, "蓬莱·邪剑仙幻影", Pal3Element::water, true, 900, 70, 34, 60, 750, 1200, 6101}},
        {9012, {9012, "灵山仙人洞·银眉妖狐", Pal3Element::wind, true, 1100, 74, 36, 62, 900, 1400, 6102}},
        {9013, {9013, "神魔之井·神魔守卫", Pal3Element::thunder, true, 1350, 80, 40, 64, 1100, 1600, 6101}},
        {9014, {9014, "鬼界无常殿·赵无延", Pal3Element::earth, true, 1400, 82, 38, 60, 1200, 2000, 6102}},
        {9015, {9015, "熔岩地狱·火鬼王", Pal3Element::fire, true, 1650, 88, 42, 66, 1500, 2200, 6405}},
        {9016, {9016, "海底城·帝江神兽", Pal3Element::water, true, 1850, 92, 46, 68, 1800, 2600, 6406}},
        {9017, {9017, "剑冢外围·魔化罗如烈", Pal3Element::fire, true, 2100, 98, 48, 70, 2200, 3000, 6501}},
    };
}

std::vector<Pal3RoleState> Pal3Database::CreateInitialRoles() const {
    return {
        {0, "101", "景天", "永安当伙计", Pal3Element::wind, true, false, 1, 280, 280, 140, 140, 50,
         100, 68, 42, 65, TaijiSlot::wind, {3001, 3002, 3501, 4001, 4002, 4003}},
        {1, "104", "唐雪见", "唐门大小姐", Pal3Element::earth, false, false, 1, 240, 240, 130, 130,
         40, 100, 60, 38, 68, TaijiSlot::earth, {3101, 3102, 4001, 4002}},
        {2, "105", "龙葵", "古姜国公主", Pal3Element::water, false, false, 5, 260, 260, 180, 180,
         50, 100, 64, 40, 64, TaijiSlot::thunder, {3201, 3202, 4002}},
        {3, "107", "紫萱", "女娲后人", Pal3Element::water, false, false, 12, 340, 340, 240, 240, 60,
         100, 78, 48, 62, TaijiSlot::water, {3201, 3301, 4003}},
        {4, "102", "徐长卿", "蜀山大弟子", Pal3Element::fire, false, false, 12, 380, 380, 200, 200,
         60, 100, 86, 54, 60, TaijiSlot::fire, {3401, 3402, 4003}},
        {5, "109", "花楹", "五毒仙兽", Pal3Element::earth, false, false, 1, 200, 200, 150, 150, 30,
         100, 50, 35, 72, TaijiSlot::center, {3101}},
    };
}

std::map<std::int32_t, std::int32_t> Pal3Database::CreateInitialFavor() const {
    return {
        {1, 20},  // 唐雪见
        {2, 25},  // 龙葵
        {3, 15},  // 紫萱
        {5, 15},  // 花楹
    };
}

const Pal3SkillRecord* Pal3Database::FindSkill(std::uint32_t skill_id) const {
    auto it = skills_.find(skill_id);
    return (it != skills_.end()) ? &it->second : nullptr;
}

const Pal3ItemRecord* Pal3Database::FindItem(std::int32_t item_id) const {
    auto it = items_.find(item_id);
    return (it != items_.end()) ? &it->second : nullptr;
}

const Pal3MonsterRecord* Pal3Database::FindMonster(std::uint32_t monster_id) const {
    auto it = monsters_.find(monster_id);
    return (it != monsters_.end()) ? &it->second : nullptr;
}

// ============================================================================
// Pal3CombatSystem Implementation
// ============================================================================

Pal3CombatSystem::Pal3CombatSystem(const Pal3Database& db, pal4::RandomNumberGenerator& rng)
    : db_(db), rng_(rng) {}

void Pal3CombatSystem::ConfigureNextBattle(std::optional<std::int32_t> max_round, bool must_fail) {
    staged_max_round_ = max_round;
    staged_must_fail_ = must_fail;
}

void Pal3CombatSystem::StartBattle(std::vector<Pal3RoleState>& party,
                                   const DemonSwordState& /*demon_sword*/,
                                   std::span<const std::uint32_t> monster_ids,
                                   bool is_boss) {
    state_.active = true;
    state_.is_boss_battle = is_boss;
    state_.max_round = staged_max_round_;
    state_.must_fail = staged_must_fail_;
    staged_max_round_.reset();
    staged_must_fail_ = false;
    state_.current_round = 1;
    state_.enemies.clear();
    state_.party_atb.clear();

    std::uint32_t slot = 0;
    for (std::uint32_t mid : monster_ids) {
        const auto* rec = db_.FindMonster(mid);
        if (!rec) continue;
        CombatEnemyInstance enemy;
        enemy.slot_index = slot++;
        enemy.monster_id = rec->monster_id;
        enemy.name = rec->name;
        enemy.element = rec->element;
        enemy.is_boss = rec->is_boss;
        enemy.hp = rec->max_hp;
        enemy.max_hp = rec->max_hp;
        enemy.attack = rec->attack;
        enemy.defense = rec->defense;
        enemy.speed = rec->speed;
        enemy.atb_gauge = 20.0f;
        state_.enemies.push_back(std::move(enemy));
    }

    for (auto& member : party) {
        if (member.in_team) {
            member.hp = std::max(1, member.hp);
            state_.party_atb[member.role_id] = 80.0f + static_cast<float>(member.speed) * 0.25f;
        }
    }
    AdvanceAtbUntilNextPartyTurn(party);
}

void Pal3CombatSystem::AdvanceAtbUntilNextPartyTurn(std::vector<Pal3RoleState>& party) {
    for (int step = 0; step < 64; ++step) {
        // Check if any party member has reached ATB >= 100
        std::int32_t best_role = -1;
        float best_atb = 99.99f;
        for (const auto& m : party) {
            if (!m.in_team || m.hp <= 0) continue;
            const float g = state_.party_atb[m.role_id];
            if (g >= 100.0f && g > best_atb) {
                best_atb = g;
                best_role = m.role_id;
            }
        }
        if (best_role >= 0) {
            state_.awaiting_role_id = best_role;
            return;
        }

        // Advance ATB bars for all living allies and enemies
        for (const auto& m : party) {
            if (m.in_team && m.hp > 0) {
                state_.party_atb[m.role_id] += static_cast<float>(std::max(20, m.speed)) * 0.35f;
            }
        }
        for (auto& e : state_.enemies) {
            if (e.hp <= 0) continue;
            e.atb_gauge += static_cast<float>(std::max(20, e.speed)) * 0.25f;
            if (e.atb_gauge >= 100.0f) {
                e.atb_gauge -= 100.0f;
                // Enemy attacks frontmost living party member
                for (auto& m : party) {
                    if (m.in_team && m.hp > 0) {
                        const int raw = std::max(8, e.attack - m.defense / 2);
                        m.hp = std::max(1, m.hp - raw);  // Keep at least 1 HP unless must_fail
                        m.qi = std::min(m.max_qi, m.qi + 15);
                        break;
                    }
                }
            }
        }
    }
    state_.awaiting_role_id = 0;
}

CombatTurnOutcome Pal3CombatSystem::ExecuteAction(
    std::vector<Pal3RoleState>& party,
    const DemonSwordState& demon_sword,
    std::map<std::int32_t, std::int32_t>& inventory,
    std::int32_t& money,
    std::string_view action_kind,
    std::uint32_t skill_or_item_id,
    std::uint32_t target_enemy_slot,
    TaijiSlot target_slot) {
    CombatTurnOutcome outcome;
    if (!state_.active) return outcome;

    auto actor_it = std::find_if(party.begin(), party.end(), [&](const Pal3RoleState& r) {
        return r.role_id == state_.awaiting_role_id && r.in_team;
    });
    if (actor_it == party.end()) {
        actor_it = party.begin();
    }
    auto& actor = *actor_it;
    outcome.actor_role_id = actor.role_id;

    auto find_target_enemy = [&]() -> CombatEnemyInstance* {
        for (auto& e : state_.enemies) {
            if (e.slot_index == target_enemy_slot && e.hp > 0) return &e;
        }
        for (auto& e : state_.enemies) {
            if (e.hp > 0) return &e;
        }
        return nullptr;
    };

    const int sword_bonus =
        (actor.role_id == 0 && demon_sword.obtained) ? demon_sword.spirit_atk_bonus : 0;

    if (action_kind == "move_slot") {
        actor.formation_slot = target_slot;
        outcome.action_summary =
            actor.name + " 换位至五灵太极阵【" + std::string(TaijiSlotName(target_slot)) + "】位";
    } else if (action_kind == "defend") {
        actor.hp = std::min(actor.max_hp, actor.hp + 40);
        actor.qi = std::min(actor.max_qi, actor.qi + 20);
        outcome.action_summary = actor.name + " 凝神防御";
    } else if (action_kind == "item") {
        const auto* item = db_.FindItem(static_cast<std::int32_t>(skill_or_item_id));
        if (item && inventory[item->item_id] > 0) {
            inventory[item->item_id]--;
            actor.hp = std::min(actor.max_hp, actor.hp + item->hp_restore);
            actor.shen = std::min(actor.max_shen, actor.shen + item->mp_restore);
            outcome.action_summary = actor.name + " 使用【" + item->name + "】";
        }
    } else if (action_kind == "magic" || action_kind == "skill" || action_kind == "combo") {
        const std::uint32_t sid = (skill_or_item_id != 0u)
                                      ? skill_or_item_id
                                      : (actor.learned_skills.empty() ? 3001u : actor.learned_skills.front());
        const auto* skill = db_.FindSkill(sid);
        if (!skill) {
            skill = db_.FindSkill(3001u);
        }
        if (skill->is_combo || action_kind == "combo") {
            outcome.combo_triggered = true;
            state_.total_combos_used++;
        }
        actor.shen = std::max(0, actor.shen - skill->mp_cost / 2);

        const bool slot_matches = (SlotBoostElement(actor.formation_slot) == skill->element) ||
                                  (actor.formation_slot == TaijiSlot::center);
        const float slot_mult = slot_matches ? 1.25f : 1.0f;
        outcome.formation_bonus_triggered = slot_matches;

        auto apply_skill_to = [&](CombatEnemyInstance& enemy) {
            const float elem_mult = ComputeElementMultiplier(skill->element, enemy.element);
            if (elem_mult > 1.1f) outcome.elemental_counter_triggered = true;
            const float raw =
                static_cast<float>(skill->base_power + actor.attack * 2 + sword_bonus - enemy.defense / 2) *
                elem_mult * slot_mult;
            const int dmg = std::max(45, static_cast<int>(std::round(raw)));
            enemy.hp = std::max(0, enemy.hp - dmg);
            outcome.total_damage_dealt += dmg;
        };

        if (skill->is_aoe || skill->is_combo) {
            for (auto& e : state_.enemies) {
                if (e.hp > 0) apply_skill_to(e);
            }
        } else if (auto* target = find_target_enemy()) {
            apply_skill_to(*target);
        }
        outcome.action_summary = actor.name + (skill->is_combo ? " 发动五灵合击技【" : " 施展仙术【") +
                                 skill->name + "】";
    } else {
        // Default: normal attack (with Demon Sword bonus for Jingtian)
        if (auto* target = find_target_enemy()) {
            const int dmg = std::max(30, (actor.attack + sword_bonus) * 2 - target->defense / 2 +
                                             static_cast<int>(rng_.Next() % 15u));
            target->hp = std::max(0, target->hp - dmg);
            outcome.total_damage_dealt += dmg;
            actor.qi = std::min(actor.max_qi, actor.qi + 20);
            outcome.action_summary = actor.name + " 挥剑斩击 " + target->name;
        }
    }

    state_.party_atb[actor.role_id] = 0.0f;
    state_.current_round++;

    CheckBattleEndConditions(party, inventory, money, outcome);
    if (!state_.active) {
        return outcome;
    }

    AdvanceAtbUntilNextPartyTurn(party);
    return outcome;
}

void Pal3CombatSystem::CheckBattleEndConditions(
    std::vector<Pal3RoleState>& party,
    std::map<std::int32_t, std::int32_t>& inventory,
    std::int32_t& money,
    CombatTurnOutcome& outcome) {
    const bool all_enemies_dead =
        std::all_of(state_.enemies.begin(), state_.enemies.end(),
                    [](const CombatEnemyInstance& e) { return e.hp <= 0; });

    // Check `CombatMaxRound` survival condition (e.g., 5 rounds against Chonglou in Yuzhou Bamboo Forest)
    const bool reached_max_round =
        state_.max_round.has_value() && state_.current_round > *state_.max_round;

    if (all_enemies_dead || reached_max_round) {
        state_.active = false;
        state_.last_combat_result = 1;
        state_.total_victories++;
        outcome.battle_finished = true;
        outcome.party_victory = true;
        outcome.survived_max_round = reached_max_round && !all_enemies_dead;

        for (const auto& e : state_.enemies) {
            if (const auto* rec = db_.FindMonster(e.monster_id)) {
                money += rec->money_reward;
                if (rec->drop_item_id != 0) {
                    inventory[rec->drop_item_id] += 1;
                }
            }
        }
        for (auto& m : party) {
            if (m.in_team) {
                m.level += 2;
                m.max_hp += 45;
                m.hp = m.max_hp;
                m.max_shen += 25;
                m.shen = m.max_shen;
                m.attack += 14;
                m.defense += 8;
            }
        }
    }
}

}  // namespace pal3::portable
