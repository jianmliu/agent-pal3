#pragma once

#include "pal3/portable/database_combat.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace pal3::portable {

enum class Pal3MinigameKind : std::uint8_t {
    none = 0,
    appraisal = 1,   // 古董鉴定 (`APPREntry` 104 / `GetAppr` 108)
    encampment = 2,  // 安宁村宿营夜话 (`ENCAMP_Entry` 106)
    skiing = 3,      // 冰风谷滑雪 (`SKEE_Entry` 107)
    yujian = 4,      // 蜀山御剑飞行大地图 (`YuJian` 155)
};

std::string_view Pal3MinigameKindName(Pal3MinigameKind kind) noexcept;

struct MinigameStateSnapshot {
    Pal3MinigameKind active_kind{Pal3MinigameKind::none};
    std::uint32_t context_id{0};
    std::string title;
    std::string prompt;
    std::vector<std::string> options;
    std::int32_t correct_option{1};
    std::int32_t ski_progress{0};
    std::int32_t ski_target{3};
    std::int32_t last_appraisal_result{1};
    std::uint32_t appraisals_passed{0};
    std::uint32_t encampments_completed{0};
    std::uint32_t ski_runs_completed{0};
    std::uint32_t yujian_flights{0};
    std::string pending_yujian_cpk;
    std::string pending_yujian_scn;
};

struct MinigameActionOutcome {
    bool completed{false};
    bool success{false};
    std::string summary;
    std::string trigger_load_cpk;
    std::string trigger_load_scn;
};

class Pal3MinigameManager {
public:
    void StartAppraisal(std::uint32_t antique_id);
    void StartEncampment(std::uint32_t encamp_id);
    void StartSkiing(std::uint32_t course_id);
    void StartYuJian();

    MinigameActionOutcome SubmitAction(std::string_view action,
                                       std::int32_t option_index,
                                       std::string_view destination_code,
                                       std::vector<Pal3RoleState>& party,
                                       std::map<std::int32_t, std::int32_t>& favor,
                                       std::map<std::int32_t, std::int32_t>& inventory,
                                       std::int32_t& money);

    bool IsActive() const noexcept { return state_.active_kind != Pal3MinigameKind::none; }
    const MinigameStateSnapshot& State() const noexcept { return state_; }
    std::int32_t LastAppraisalResult() const noexcept { return state_.last_appraisal_result; }

    void RestoreSnapshot(const MinigameStateSnapshot& snap) { state_ = snap; }

private:
    MinigameStateSnapshot state_;
};

}  // namespace pal3::portable
