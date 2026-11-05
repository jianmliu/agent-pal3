#include "pal3/portable/database_combat.h"
#include "pal3/portable/minigames.h"

#include <cassert>
#include <iostream>

using namespace pal3::portable;

int main() {
    // 1. Test PAL3 Five-Element counter cycle (Water -> Fire -> Thunder -> Wind -> Earth -> Water)
    assert(DoesElementCounter(Pal3Element::water, Pal3Element::fire));
    assert(DoesElementCounter(Pal3Element::fire, Pal3Element::thunder));
    assert(DoesElementCounter(Pal3Element::thunder, Pal3Element::wind));
    assert(DoesElementCounter(Pal3Element::wind, Pal3Element::earth));
    assert(DoesElementCounter(Pal3Element::earth, Pal3Element::water));
    assert(ComputeElementMultiplier(Pal3Element::wind, Pal3Element::earth) > 1.4f);

    // 2. Test Max-Round survival combat (`CombatMaxRound(3)` against Chonglou 9001)
    pal4::RandomNumberGenerator rng;
    rng.Seed(42);
    Pal3Database db;
    Pal3CombatSystem combat(db, rng);
    auto party = db.CreateInitialRoles();
    party[1].in_team = true;
    auto favor = db.CreateInitialFavor();
    std::map<std::int32_t, std::int32_t> inventory;
    std::int32_t money = 500;
    DemonSwordState sword{true, false, "魔剑", 2, 50, {Pal3Element::wind}};

    combat.ConfigureNextBattle(3, false);
    const std::uint32_t chonglou_ids[] = {9001};
    combat.StartBattle(party, sword, chonglou_ids, true);
    assert(combat.IsActive());

    CombatTurnOutcome last_outcome;
    while (combat.IsActive()) {
        last_outcome = combat.ExecuteAction(party, sword, inventory, money, "defend");
    }
    assert(last_outcome.battle_finished);
    assert(last_outcome.party_victory);
    assert(last_outcome.survived_max_round);

    // 3. Test Five-Element Taiji Formation slot move & Dual Combo `4001` (【断空噬地】)
    const std::uint32_t guteng_ids[] = {9003};
    combat.StartBattle(party, sword, guteng_ids, true);
    combat.ExecuteAction(party, sword, inventory, money, "move_slot", 0, 0, TaijiSlot::wind);
    while (combat.IsActive()) {
        last_outcome = combat.ExecuteAction(party, sword, inventory, money, "combo", 4001, 0);
    }
    assert(last_outcome.party_victory);
    assert(combat.State().total_combos_used >= 1u);

    // 4. Test all 4 PAL3 Minigames (Appraisal, Encampment, Skiing, YuJian)
    Pal3MinigameManager mg;
    mg.StartAppraisal(6201);
    assert(mg.IsActive());
    const auto appr = mg.SubmitAction("inspect", 1, "", party, favor, inventory, money);
    assert(appr.completed && appr.success);
    assert(mg.LastAppraisalResult() == 1);
    assert(inventory[6201] == 1);

    const int xuejian_before = favor[1];
    mg.StartEncampment(1);
    const auto enc = mg.SubmitAction("talk", 1, "", party, favor, inventory, money);
    assert(enc.completed && favor[1] == xuejian_before + 25);

    mg.StartSkiing(1);
    const auto ski = mg.SubmitAction("jump", 1, "", party, favor, inventory, money);
    assert(ski.completed && inventory[6501] >= 1);

    mg.StartYuJian();
    const auto fly = mg.SubmitAction("fly", 4, "m23", party, favor, inventory, money);
    assert(fly.completed && fly.trigger_load_cpk == "m23");

    std::cout << "[PASS] pal3_portable_combat_minigame_tests\n";
    return 0;
}
