#include "pal3/portable/sce_vm.h"

#include <cassert>
#include <iostream>
#include <vector>

using namespace pal3::portable;

namespace {

struct MockSceServices final : ISceEngineServices {
    std::vector<std::string> dialogs;
    std::vector<std::string> choices;
    std::map<std::int32_t, std::int32_t> favor;
    std::map<std::int32_t, std::int32_t> items;
    std::int32_t money{100};
    bool combat_started{false};
    std::optional<std::int32_t> combat_max_round;

    void SceRolePathTo(std::int32_t, std::int32_t, std::int32_t) override {}
    void SceRoleSetPos(std::int32_t, std::int32_t, std::int32_t) override {}
    void SceRoleSetLayer(std::int32_t, std::int32_t) override {}
    void SceRoleSetFace(std::int32_t, std::int32_t) override {}
    void SceRoleSetController(std::int32_t) override {}
    float SceRoleShowAction(std::int32_t, std::string_view, std::int32_t) override { return 0.5f; }
    void SceRoleSetVisible(std::int32_t, bool) override {}
    void SceRoleActive(std::int32_t, bool) override {}
    void SceCameraMove(float, float, float, float, std::int32_t) override {}
    void SceShowDialog(std::string_view text) override { dialogs.emplace_back(text); }
    void SceShowDialogSelection(std::span<const std::string> ch) override {
        choices.assign(ch.begin(), ch.end());
    }
    void SceFavorAdd(std::int32_t role_id, std::int32_t delta) override { favor[role_id] += delta; }
    void SceAddItem(std::int32_t item_id, std::int32_t count) override { items[item_id] += count; }
    void SceRemoveItem(std::int32_t item_id, std::int32_t count) override { items[item_id] -= count; }
    bool SceHaveItem(std::int32_t item_id) const override {
        auto it = items.find(item_id);
        return it != items.end() && it->second > 0;
    }
    void SceAddMoney(std::int32_t delta) override { money += delta; }
    std::int32_t SceGetMoney() const override { return money; }
    void SceAddSkill(std::int32_t, std::int32_t) override {}
    bool SceIsInTeam(std::int32_t role_id) const override { return role_id == 0 || role_id == 1; }
    void SceSetInTeam(std::int32_t, bool) override {}
    void ScePlayMovie(std::string_view) override {}
    void ScePlayMusic(std::string_view, std::int32_t) override {}
    void SceLoadScene(std::string_view, std::string_view) override {}
    void SceObjectActive(std::int32_t, bool) override {}
    void SceObjectMove(std::int32_t, float, float, float, float) override {}
    float SceObjectPlay(std::int32_t, std::string_view, float d) override { return d; }
    void SceSetUserInputEnabled(bool) override {}
    void SceConfigureNextCombat(std::optional<std::int32_t> mr, bool) override {
        combat_max_round = mr;
    }
    void SceStartCombat(std::span<const std::uint32_t>, bool) override { combat_started = true; }
    std::int32_t SceGetLastCombatResult() const override { return 1; }
    void SceStartAppraisalMinigame(std::uint32_t) override {}
    std::int32_t SceGetLastAppraisalResult() const override { return 1; }
    void SceStartEncampmentMinigame(std::uint32_t) override {}
    void SceStartSkiMinigame(std::uint32_t) override {}
    void SceStartPawnshopMinigame(std::uint32_t) override {}
    void SceOpenYuJianMap() override {}
    void SceRestAtInn(std::string_view, std::int32_t, std::uint32_t, std::uint32_t) override {}
};

}  // namespace

int main() {
    MockSceServices svc;
    SceVm vm(svc);

    // Verify full OpenPAL3 opcode table registration (>= 110 opcodes) & GBK decoding
    assert(RegisteredSceOpcodeCount() >= 110u);
    assert(ConvertGbkOrUtf8ToUtf8("\xBE\xB0\xCC\xEC") == "景天");

    // Build a .sce file testing FOP (AND/OR), Between, HaveItem, DlgSel, CombatMaxRound, and SceProcHooks (501 & 1415)
    SceFile sce;
    {
        SceAssembler a;
        a.EmitAssign(kSceVarMainStory, 10101);
        a.EmitAddItem(6301, 1);
        a.EmitHaveItem(6301);
        a.EmitFop(SceFopOp::logical_and);
        a.EmitBetween(kSceVarMainStory, 10000, 10200);
        const auto fail_jmp = a.EmitTestGotoPlaceholder();
        a.EmitFavorAdd(1, 20);
        std::vector<std::string> opts = {"铸魔剑", "铸镇妖剑"};
        a.EmitDlgSel(opts);
        a.EmitCombatMaxRound(5);
        a.EmitCombatBoss({9001, 0, 0, 0, 0, 0});
        a.EmitGetCombat(0);
        a.PatchJumpTarget(fail_jmp, static_cast<std::uint32_t>(a.CurrentOffset()));
        sce.procs[501] = a.BuildProc(501, "test_proc_501");
    }

    const auto bytes = SerializeSceFile(sce);
    const auto parsed = ParseSceFile(bytes);
    vm.SetSceneScript("q01", "yn01a", parsed);
    vm.CallProc(501);

    // Step 1: Should execute up to DlgSel and pause waiting for choice
    vm.Step(1.0f / 60.0f, true);
    assert(vm.IsWaitingForChoice());
    assert(svc.favor[1] == 20);
    assert(svc.choices.size() == 2u);

    // Step 2: Submit choice 1 -> resumes and pauses on CombatBoss with max_round=5
    vm.SubmitDialogSelection(1);
    vm.Step(1.0f / 60.0f, true);
    assert(vm.GetVar(kSceVarDlgSelect) == 1);
    assert(vm.IsWaitingForCombat());
    assert(svc.combat_started);
    assert(svc.combat_max_round.value_or(0) == 5);

    // Step 3: Finish combat -> finishes proc 501 -> SceProcHooks upgrades main story 10101 -> 10202
    vm.NotifyCombatFinished();
    vm.Step(1.0f / 60.0f, true);
    assert(vm.IsIdle());
    assert(vm.GetVar(kSceVarMainStory) == 10202);

    std::cout << "[PASS] pal3_portable_sce_vm_tests\n";
    return 0;
}
