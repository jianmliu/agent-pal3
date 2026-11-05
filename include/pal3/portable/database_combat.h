#pragma once

#include "pal4/random_number_generator.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pal3::portable {

// PAL3 Five-Element cycle: Water -> Fire -> Thunder -> Wind -> Earth -> Water
enum class Pal3Element : std::uint8_t {
    none = 0,
    water = 1,    // 水 (克火)
    fire = 2,     // 火 (克雷)
    thunder = 3,  // 雷 (克风)
    wind = 4,     // 风 (克土)
    earth = 5,    // 土 (克水)
};

std::string_view Pal3ElementName(Pal3Element elem) noexcept;
bool DoesElementCounter(Pal3Element attacker, Pal3Element defender) noexcept;
float ComputeElementMultiplier(Pal3Element attacker, Pal3Element defender) noexcept;

// PAL3 Five-Element Taiji Formation (`五灵太极法阵`) 6 slots
enum class TaijiSlot : std::uint8_t {
    center = 0,   // 中央太极阵眼
    water = 1,    // 水阵位
    fire = 2,     // 火阵位
    thunder = 3,  // 雷阵位
    wind = 4,     // 风阵位
    earth = 5,    // 土阵位
};

std::string_view TaijiSlotName(TaijiSlot slot) noexcept;
TaijiSlot ParseTaijiSlot(std::string_view name) noexcept;
Pal3Element SlotBoostElement(TaijiSlot slot) noexcept;

struct Pal3SkillRecord {
    std::uint32_t skill_id{};
    std::string name;
    Pal3Element element{Pal3Element::none};
    bool is_combo{false};
    bool is_aoe{false};
    std::int32_t mp_cost{20};      // 耗神 (Five-Element magic) or 耗气 (Skill)
    std::int32_t base_power{120};
    std::vector<std::int32_t> required_roles;
};

struct Pal3ItemRecord {
    std::int32_t item_id{};
    std::string name;
    std::string category;  // "quest", "antique", "weapon", "consumable", "spirit_pearl"
    std::int32_t hp_restore{0};
    std::int32_t mp_restore{0};
    std::int32_t attack_bonus{0};
    std::int32_t price{100};
};

struct Pal3MonsterRecord {
    std::uint32_t monster_id{};
    std::string name;
    Pal3Element element{Pal3Element::none};
    bool is_boss{false};
    std::int32_t max_hp{500};
    std::int32_t attack{65};
    std::int32_t defense{30};
    std::int32_t speed{50};
    std::int32_t exp_reward{150};
    std::int32_t money_reward{200};
    std::int32_t drop_item_id{0};
};

struct DemonSwordState {
    bool obtained{false};
    bool zhenyaojian_obtained{false};
    std::string equipped_divine_sword{"无"};  // "魔剑", "镇妖剑", or "圣灵魔剑"
    std::uint32_t purification_level{1};      // 1..5
    std::int32_t spirit_atk_bonus{35};
    std::vector<Pal3Element> infused_elements;
};

struct Pal3RoleState {
    std::int32_t role_id{0};       // 0=景天, 1=唐雪见, 2=龙葵, 3=紫萱, 4=徐长卿, 5=花楹
    std::string model_code{"101"};
    std::string name{"景天"};
    std::string title{"永安当伙计"};
    Pal3Element element{Pal3Element::wind};
    bool in_team{false};
    bool is_red_longkui{false};    // For role_id == 2: false=蓝葵(105/水), true=红葵(106/雷)
    std::uint32_t level{1};
    std::int32_t hp{240}, max_hp{240};
    std::int32_t shen{120}, max_shen{120};  // 神 (for Five-Element Magic)
    std::int32_t qi{30}, max_qi{100};       // 气 (for Personal Skills & Combos)
    std::int32_t attack{55}, defense{35}, speed{60};
    TaijiSlot formation_slot{TaijiSlot::center};
    std::vector<std::uint32_t> learned_skills;
};

class Pal3Database {
public:
    Pal3Database();

    std::vector<Pal3RoleState> CreateInitialRoles() const;
    std::map<std::int32_t, std::int32_t> CreateInitialFavor() const;

    const Pal3SkillRecord* FindSkill(std::uint32_t skill_id) const;
    const Pal3ItemRecord* FindItem(std::int32_t item_id) const;
    const Pal3MonsterRecord* FindMonster(std::uint32_t monster_id) const;

    const std::map<std::uint32_t, Pal3SkillRecord>& Skills() const noexcept { return skills_; }
    const std::map<std::int32_t, Pal3ItemRecord>& Items() const noexcept { return items_; }
    const std::map<std::uint32_t, Pal3MonsterRecord>& Monsters() const noexcept { return monsters_; }

private:
    std::map<std::uint32_t, Pal3SkillRecord> skills_;
    std::map<std::int32_t, Pal3ItemRecord> items_;
    std::map<std::uint32_t, Pal3MonsterRecord> monsters_;
};

// ============================================================================
// PAL3 Semi-Realtime ATB + Five-Element Taiji Formation Combat Engine
// ============================================================================

struct CombatEnemyInstance {
    std::uint32_t slot_index{0};
    std::uint32_t monster_id{0};
    std::string name;
    Pal3Element element{Pal3Element::none};
    bool is_boss{false};
    std::int32_t hp{500}, max_hp{500};
    std::int32_t attack{60}, defense{30}, speed{50};
    float atb_gauge{0.0f};  // 0..100
};

struct CombatTurnOutcome {
    bool battle_finished{false};
    bool party_victory{false};
    bool survived_max_round{false};
    std::int32_t actor_role_id{0};
    std::string action_summary;
    std::int32_t total_damage_dealt{0};
    bool elemental_counter_triggered{false};
    bool formation_bonus_triggered{false};
    bool combo_triggered{false};
};

struct CombatSnapshot {
    bool active{false};
    bool is_boss_battle{false};
    bool must_fail{false};
    std::optional<std::int32_t> max_round;
    std::int32_t current_round{1};
    std::int32_t awaiting_role_id{0};
    std::vector<CombatEnemyInstance> enemies;
    std::map<std::int32_t, float> party_atb;
    std::int32_t last_combat_result{1};
    std::uint32_t total_victories{0};
    std::uint32_t total_combos_used{0};
};

class Pal3CombatSystem {
public:
    Pal3CombatSystem(const Pal3Database& db, pal4::RandomNumberGenerator& rng);

    void ConfigureNextBattle(std::optional<std::int32_t> max_round, bool must_fail);
    void StartBattle(std::vector<Pal3RoleState>& party,
                     const DemonSwordState& demon_sword,
                     std::span<const std::uint32_t> monster_ids,
                     bool is_boss);

    CombatTurnOutcome ExecuteAction(std::vector<Pal3RoleState>& party,
                                    const DemonSwordState& demon_sword,
                                    std::map<std::int32_t, std::int32_t>& inventory,
                                    std::int32_t& money,
                                    std::string_view action_kind,
                                    std::uint32_t skill_or_item_id = 0,
                                    std::uint32_t target_enemy_slot = 0,
                                    TaijiSlot target_slot = TaijiSlot::center);

    bool IsActive() const noexcept { return state_.active; }
    const CombatSnapshot& State() const noexcept { return state_; }
    std::int32_t LastCombatResult() const noexcept { return state_.last_combat_result; }

    void RestoreSnapshot(const CombatSnapshot& snap) { state_ = snap; }

private:
    void AdvanceAtbUntilNextPartyTurn(std::vector<Pal3RoleState>& party);
    void CheckBattleEndConditions(std::vector<Pal3RoleState>& party,
                                  std::map<std::int32_t, std::int32_t>& inventory,
                                  std::int32_t& money,
                                  CombatTurnOutcome& outcome);

    const Pal3Database& db_;
    pal4::RandomNumberGenerator& rng_;
    std::optional<std::int32_t> staged_max_round_;
    bool staged_must_fail_{false};
    CombatSnapshot state_;
};

}  // namespace pal3::portable
