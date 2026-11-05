#include "pal3/portable/chapter_host.h"
#include "GLES3/gl3.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pal3::portable {
namespace {

NavFile BuildDualLayerNavMesh(bool with_north_wall) {
    NavFile nav;
    nav.version = 2;
    nav.layers.resize(2);

    for (std::size_t li = 0; li < 2; ++li) {
        auto& layer = nav.layers[li];
        layer.min_coord = {-100.0f, (li == 0) ? 0.0f : 20.0f, -100.0f};
        layer.max_coord = {100.0f, (li == 0) ? 10.0f : 30.0f, 100.0f};
        layer.width = 32;
        layer.height = 32;
        layer.tiles.resize(32 * 32);

        // Portal ramp at tile [20..23, 20..23] connecting layer 0 <-> layer 1
        layer.portals[0] = {{20, 20}, {23, 23}};

        for (std::uint32_t z = 0; z < layer.height; ++z) {
            for (std::uint32_t x = 0; x < layer.width; ++x) {
                auto& pt = layer.tiles[z * layer.width + x];
                pt.height = (li == 0) ? 0.0f : 20.0f;
                const std::uint32_t border_dist = std::min({x, z, layer.width - 1 - x, layer.height - 1 - z});
                if (border_dist == 0u || (with_north_wall && z >= 26u)) {
                    pt.distance_to_border = 0u;  // Impassable border / north wall at z >= 60
                } else {
                    pt.distance_to_border = std::min<std::uint32_t>(6u, border_dist);
                }
            }
        }
    }
    return nav;
}

PolModel BuildStagePolModel(std::string_view name) {
    PolModel pol;
    PolSubMesh m;
    m.name = std::string(name);
    m.texture_name = "floor.dds";
    m.aabb_min = {-100.0f, 0.0f, -100.0f};
    m.aabb_max = {100.0f, 30.0f, 100.0f};
    m.vertices = {
        {{-80.0f, 0.0f, -80.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
        {{80.0f, 0.0f, -80.0f}, {1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
        {{80.0f, 0.0f, 80.0f}, {1.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
        {{-80.0f, 0.0f, 80.0f}, {0.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    };
    m.triangles = {{0, 1, 2}, {0, 2, 3}};
    pol.meshes.push_back(std::move(m));
    return pol;
}

CvdModel BuildMechanismCvdModel(std::string_view name) {
    CvdModel cvd;
    cvd.animation_duration_sec = 1.0f;
    CvdNode n;
    n.name = std::string(name);
    n.scale = 1.0f;
    n.position_keys = {
        {0.0f, {0.0f, 0.0f, 0.0f}},
        {1.0f, {0.0f, 12.0f, 0.0f}},
    };
    n.rotation_keys = {
        {0.0f, 0.0f, {0.0f, 1.0f, 0.0f}},
        {1.0f, 90.0f, {0.0f, 1.0f, 0.0f}},
    };
    n.vertices = {
        {{-10.0f, 0.0f, -10.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
        {{10.0f, 0.0f, -10.0f}, {1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
        {{10.0f, 5.0f, 10.0f}, {1.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}},
    };
    n.triangles = {{0, 1, 2}};
    cvd.nodes.push_back(std::move(n));
    return cvd;
}

Mv3Model BuildHeroMv3Model(std::string_view code, std::string_view action) {
    Mv3Model mv3;
    mv3.action_name = std::string(action);
    mv3.texture_name = std::string(code) + ".tga";
    mv3.duration_ticks = 4580;
    mv3.events = {{2290, "hold"}};
    mv3.frames = {
        {0, {{{-320, 0, -320}, {320, 0, -320}, {0, 1100, 320}}}},
        {2290, {{{-320, 128, -320}, {320, 128, -320}, {0, 1280, 320}}}},
        {4580, {{{-320, 0, -320}, {320, 0, -320}, {0, 1100, 320}}}},
    };
    mv3.texcoords = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.5f, 1.0f}};
    mv3.triangles = {{0, 1, 2}};
    return mv3;
}

}  // namespace

CampaignCpkSuite BuildFullPal3CampaignCpkSuite() {
    CampaignCpkSuite suite;

    // 0. basedata.cpk: system init.sce + protagonist MV3 models (101, 104, 105, 106, 107, 102, 109)
    {
        SceFile sys_sce;
        SceAssembler init_asm;
        init_asm.EmitAssign(kSceVarMainStory, 10101);
        sys_sce.procs[1] = init_asm.BuildProc(1, "sys_init");

        std::vector<CpkArchiveBuilderEntry> entries;
        entries.push_back({"init.sce", SerializeSceFile(sys_sce), CpkCodec::lzo});
        for (const char* code : {"101", "104", "105", "106", "107", "102", "109"}) {
            entries.push_back({std::string("role/") + code + "/c01.mv3",
                               SerializeMv3Model(BuildHeroMv3Model(code, "c01")),
                               CpkCodec::lzo});
        }
        suite.basedata_cpk = BuildPal3CpkArchive(entries);
    }

    // 1. q01.cpk (Stage 1: 渝州·永安当与竹林初遇)
    {
        ScnFile scn;
        scn.cpk_name = "q01";
        scn.scn_name = "yn01a";
        scn.scn_base_name = "yn01";
        scn.roles = {
            {1, "赵文昌", 180.0f, 1, 40.0f, 40.0f, 501, "c01", 0, {}, 215.0f},
            {2, "唐雪见", 90.0f, 0, -35.0f, 20.0f, 502, "c01", 0, {}, 215.0f},
            {3, "魔尊·重楼", 0.0f, 0, 0.0f, -45.0f, 503, "c01", 0, {}, 215.0f},
        };
        ScnNode ladder{};
        ladder.index = 10;
        ladder.active = 1;
        ladder.name = "pawnshop_stairs";
        ladder.node_type = static_cast<std::uint16_t>(ScnNodeType::ladder);
        ladder.position = {30.0f, 0.0f, 30.0f};
        ladder.nav_layer = 0;
        ladder.ladder_nav_coord1 = {20, 20};
        ladder.ladder_nav_coord2 = {21, 21};
        ladder.ladder_switch_layer = 1;

        ScnNode exit_trigger{};
        exit_trigger.index = 20;
        exit_trigger.active = 1;
        exit_trigger.name = "exit_to_m01";
        exit_trigger.node_type = static_cast<std::uint16_t>(ScnNodeType::tile_trigger);
        exit_trigger.position = {0.0f, 0.0f, -75.0f};
        exit_trigger.nav_layer = 0;
        exit_trigger.nav_trigger_coord_min = {14, 2};
        exit_trigger.nav_trigger_coord_max = {18, 5};
        exit_trigger.sce_proc_id = 599;
        scn.nodes = {ladder, exit_trigger};

        SceFile sce;
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 10101);
            a.EmitMusic("pi01.mp3", 1);
            a.EmitDlg("景天：天亮了，赵管事又在永安当二楼账房催着鉴宝了。");
            sce.procs[1] = a.BuildProc(1, "yn01a_init");
        }
        {
            // proc 501: Zhao Wenchang antique appraisal tutorial + SceProcHooks (10101 -> 10202)
            SceAssembler a;
            a.EmitDlg("赵文昌：景天！快把这把【宜兴紫砂壶】的赝品破绽找出来！");
            a.EmitApprEntry(6201);
            a.EmitGetAppr(0);
            sce.procs[501] = a.BuildProc(501, "zhao_appraisal");
        }
        {
            // proc 502: Meet Tang Xuejian & obtain Demon Sword (`魔剑`)
            SceAssembler a;
            a.EmitDlg("唐雪见：喂！永安当的小伙计，本小姐唐雪见今天跟你一起闯荡江湖！");
            a.EmitInTeam(1, 1);
            a.EmitAddItem(6301, 1);
            a.EmitFavorAdd(1, 15);
            a.EmitAssign(kSceVarMainStory, 10400);
            sce.procs[502] = a.BuildProc(502, "meet_xuejian");
        }
        {
            // proc 503: Yuzhou Bamboo Forest Chonglou 3-round survival battle (`CombatMaxRound`)
            SceAssembler a;
            a.EmitDlg("重楼：飞蓬！这把魔剑归还于你，且接本座三招！新仙界再决胜负！");
            a.EmitCombatMaxRound(3);
            a.EmitCombatBoss({9001, 0, 0, 0, 0, 0});
            a.EmitGetCombat(0);
            a.EmitAssign(kSceVarMainStory, 10600);
            sce.procs[503] = a.BuildProc(503, "chonglou_bamboo");
        }
        {
            // proc 599: Step trigger to Stage 2 Bishan (`m01`)
            SceAssembler a;
            a.EmitLoadScene("m01", "m01");
            sce.procs[599] = a.BuildProc(599, "to_m01");
        }

        suite.q01_cpk = BuildPal3CpkArchive({
            {"q01.scn", SerializeScnFile(scn), CpkCodec::lzo},
            {"q01.sce", SerializeSceFile(sce), CpkCodec::lzo},
            {"yn01/yn01.nav", SerializeNavFile(BuildDualLayerNavMesh(true)), CpkCodec::lzo},
            {"yn01/yn01.pol", SerializePolModel(BuildStagePolModel("yn01_hall")), CpkCodec::lzo},
            {"yn01/_f01.cvd", SerializeCvdModel(BuildMechanismCvdModel("yn01_chest")), CpkCodec::lzo},
        });
    }

    // 2. m01.cpk (Stage 2: 璧山双层机关桥·九龙坡与大渡口龙葵入队)
    {
        ScnFile scn;
        scn.cpk_name = "m01";
        scn.scn_name = "m01";
        scn.scn_base_name = "m01";
        scn.roles = {
            {4, "霹雳堂头目", 180.0f, 0, 0.0f, -20.0f, 602, "c01", 0, {}, 215.0f},
            {5, "龙葵", 90.0f, 0, -30.0f, -35.0f, 603, "c01", 0, {}, 215.0f},
        };
        ScnNode bridge_node{};
        bridge_node.index = 30;
        bridge_node.active = 1;
        bridge_node.name = "_f01.cvd";
        bridge_node.node_type = static_cast<std::uint16_t>(ScnNodeType::mechanism);
        bridge_node.position = {30.0f, 0.0f, 15.0f};
        bridge_node.nav_layer = 0;
        bridge_node.sce_proc_id = 601;
        scn.nodes = {bridge_node};

        SceFile sce;
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 20100);
            a.EmitDlg("唐雪见：前面是璧山机关栈道，快去开启右侧的石桥机关！");
            sce.procs[1] = a.BuildProc(1, "m01_init");
        }
        {
            // proc 601: CVD Mechanism Bridge animation (`ObjectPlay`)
            SceAssembler a;
            a.EmitObjectPlay(30, "bridge_open", 1.0f);
            a.EmitDlg("（咔嚓一声，璧山双层石桥缓缓升起连通九龙坡！）");
            a.EmitAssign(kSceVarMainStory, 20200);
            sce.procs[601] = a.BuildProc(601, "open_bridge");
        }
        {
            // proc 602: Defeat Pilittang Boss
            SceAssembler a;
            a.EmitDlg("霹雳堂头目：识相的把唐家堡毒蒺藜与古董交出来！");
            a.EmitCombatBoss({9002, 0, 0, 0, 0, 0});
            a.EmitGetCombat(0);
            a.EmitAssign(kSceVarMainStory, 20350);
            sce.procs[602] = a.BuildProc(602, "pilittang_boss");
        }
        {
            // proc 603: Longkui joins party from Demon Sword & transition to Stage 3 Penglai (`q03`)
            SceAssembler a;
            a.EmitDlg("龙葵：王兄……千年等待，龙葵终于从魔剑中再见到王兄了！");
            a.EmitInTeam(2, 1);
            a.EmitFavorAdd(2, 20);
            a.EmitAssign(kSceVarMainStory, 20500);
            a.EmitLoadScene("q03", "q03");
            sce.procs[603] = a.BuildProc(603, "longkui_join");
        }

        suite.m01_cpk = BuildPal3CpkArchive({
            {"m01.scn", SerializeScnFile(scn), CpkCodec::lzo},
            {"m01.sce", SerializeSceFile(sce), CpkCodec::lzo},
            {"m01/m01.nav", SerializeNavFile(BuildDualLayerNavMesh(false)), CpkCodec::lzo},
            {"m01/m01.pol", SerializePolModel(BuildStagePolModel("m01_bishan")), CpkCodec::lzo},
            {"m01/_f01.cvd", SerializeCvdModel(BuildMechanismCvdModel("_f01.cvd")), CpkCodec::lzo},
        });
    }

    // 3. q03.cpk (Stage 3: 蓬莱码头·安宁村宿营夜话与古藤林五灵合击战)
    {
        ScnFile scn;
        scn.cpk_name = "q03";
        scn.scn_name = "q03";
        scn.scn_base_name = "q03";
        scn.roles = {
            {6, "紫萱与徐长卿", 180.0f, 0, 25.0f, 10.0f, 701, "c01", 0, {}, 215.0f},
            {7, "古藤仙人", 0.0f, 0, 0.0f, -35.0f, 702, "c01", 0, {}, 215.0f},
        };

        SceFile sce;
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 30100);
            a.EmitDlg("徐长卿：景兄弟，蜀山锁妖塔异动，紫萱与贫道特来相助！");
            sce.procs[1] = a.BuildProc(1, "q03_init");
        }
        {
            // proc 701: Zixuan, Changqing & Huaying join + Anning Village Night Encampment (`ENCAMP_Entry`)
            SceAssembler a;
            a.EmitInTeam(3, 1);
            a.EmitInTeam(4, 1);
            a.EmitInTeam(5, 1);
            a.EmitDlg("紫萱：今夜我们在安宁村宿营休整，明日再入古藤林寻找五灵珠。");
            a.EmitEncampEntry(1);
            a.EmitAssign(kSceVarMainStory, 30300);
            sce.procs[701] = a.BuildProc(701, "anning_encamp");
        }
        {
            // proc 702: Defeat Ancient Vine Immortal (`古藤仙人`) -> obtain Thunder Spirit Pearl -> to Stage 4 `m18`
            SceAssembler a;
            a.EmitDlg("古藤仙人：擅闯古藤林者，休想取走雷灵珠！");
            a.EmitCombatBoss({9003, 0, 0, 0, 0, 0});
            a.EmitGetCombat(0);
            a.EmitAddItem(6401, 1);
            a.EmitAssign(kSceVarMainStory, 30600);
            a.EmitLoadScene("m18", "m18");
            sce.procs[702] = a.BuildProc(702, "guteng_boss");
        }

        suite.q03_cpk = BuildPal3CpkArchive({
            {"q03.scn", SerializeScnFile(scn), CpkCodec::lzo},
            {"q03.sce", SerializeSceFile(sce), CpkCodec::lzo},
            {"q03/q03.nav", SerializeNavFile(BuildDualLayerNavMesh(false)), CpkCodec::lzo},
            {"q03/q03.pol", SerializePolModel(BuildStagePolModel("q03_penglai")), CpkCodec::lzo},
            {"q03/_f01.cvd", SerializeCvdModel(BuildMechanismCvdModel("q03_altar")), CpkCodec::lzo},
        });
    }

    // 4. m18.cpk (Stage 4: 蜀山锁妖塔升降梯·击败天妖皇获镇妖剑·冰风谷滑雪与御剑飞行)
    {
        ScnFile scn;
        scn.cpk_name = "m18";
        scn.scn_name = "m18";
        scn.scn_base_name = "m18";
        scn.roles = {
            {8, "天妖皇", 180.0f, 1, 45.0f, 45.0f, 801, "c01", 0, {}, 215.0f},
        };
        ScnNode elevator{};
        elevator.index = 40;
        elevator.active = 1;
        elevator.name = "suoyaota_chain_elevator";
        elevator.node_type = static_cast<std::uint16_t>(ScnNodeType::elevator);
        elevator.position = {10.0f, 0.0f, 10.0f};
        elevator.nav_layer = 0;
        elevator.ladder_nav_coord1 = {16, 16};
        elevator.ladder_nav_coord2 = {22, 22};
        elevator.ladder_switch_layer = 1;
        scn.nodes = {elevator};

        SceFile sce;
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 50100);
            a.EmitDlg("徐长卿：已入锁妖塔底层，搭乘铁链升降台即可抵达天妖皇封印坛！");
            sce.procs[1] = a.BuildProc(1, "m18_init");
        }
        {
            // proc 801: Defeat Emperor of Sky Demons (`天妖皇`) -> obtain `镇妖剑` -> Skiing (`SKEE_Entry`) -> `YuJian` to `m23`
            SceAssembler a;
            a.EmitDlg("天妖皇：锁妖塔困我数百年，今日便用镇妖剑将尔等碎尸万段！");
            a.EmitCombatBoss({9004, 0, 0, 0, 0, 0});
            a.EmitGetCombat(0);
            a.EmitAddItem(6302, 1);
            a.EmitAssign(kSceVarMainStory, 65000);
            a.EmitDlg("景天：取回【镇妖剑】了！我们先滑过冰风谷雪道，再御剑直奔雷州剑冢！");
            a.EmitSkeeEntry(1);
            a.EmitAssign(kSceVarMainStory, 75000);
            a.EmitYuJian();
            sce.procs[801] = a.BuildProc(801, "suoyaota_boss_ski_yujian");
        }

        suite.m18_cpk = BuildPal3CpkArchive({
            {"m18.scn", SerializeScnFile(scn), CpkCodec::lzo},
            {"m18.sce", SerializeSceFile(sce), CpkCodec::lzo},
            {"m18/m18.nav", SerializeNavFile(BuildDualLayerNavMesh(false)), CpkCodec::lzo},
            {"m18/m18.pol", SerializePolModel(BuildStagePolModel("m18_suoyaota")), CpkCodec::lzo},
            {"m18/_f01.cvd", SerializeCvdModel(BuildMechanismCvdModel("m18_chain")), CpkCodec::lzo},
        });
    }

    // 5. m23.cpk (Stage 5: 雷州剑冢铸剑抉择 `DlgSel` + 邪剑仙/新仙界重楼终极决战)
    {
        ScnFile scn;
        scn.cpk_name = "m23";
        scn.scn_name = "m23";
        scn.scn_base_name = "m23";
        scn.roles = {
            {9, "邪剑仙与魔尊·重楼", 0.0f, 0, 0.0f, -35.0f, 902, "c01", 0, {}, 215.0f},
        };
        ScnNode furnace{};
        furnace.index = 50;
        furnace.active = 1;
        furnace.name = "sword_tomb_furnace";
        furnace.node_type = static_cast<std::uint16_t>(ScnNodeType::aabb_trigger);
        furnace.position = {0.0f, 0.0f, 15.0f};
        furnace.nav_layer = 0;
        furnace.aabb_trigger_coord1 = {-20.0f, -5.0f, 0.0f};
        furnace.aabb_trigger_coord2 = {20.0f, 25.0f, 30.0f};
        furnace.sce_proc_id = 901;
        scn.nodes = {furnace};

        SceFile sce;
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 80100);
            a.EmitDlg("紫萱：此处便是古姜国铸剑炉【剑冢】，需在【魔剑】与【镇妖剑】之间择一重铸！");
            sce.procs[1] = a.BuildProc(1, "m23_init");
        }
        {
            // proc 901: Sword Tomb choice (`DlgSel`) writing choice to -32767
            SceAssembler a;
            a.EmitDlg("铸剑炉烈焰翻腾，请选择要投入五灵神力重铸的古剑：");
            std::vector<std::string> choices = {
                "0: 重铸【魔剑】（凝五灵珠之力铸成【圣灵魔剑】）",
                "1: 重铸【镇妖剑】（凝蜀山清气铸成【圣灵镇妖剑】）",
            };
            a.EmitDlgSel(choices);
            a.EmitAssign(kSceVarMainStory, 85000);
            sce.procs[901] = a.BuildProc(901, "sword_tomb_choice");
        }
        {
            // proc 902: Final Boss Battle against Evil Sword Immortal & Chonglou -> Grand Ending Movie
            SceAssembler a;
            a.EmitDlg("邪剑仙：六界唯我独尊！重楼：飞蓬，随本座入新仙界痛快一战！");
            a.EmitCombatBoss({9005, 9006, 0, 0, 0, 0});
            a.EmitGetCombat(0);
            a.EmitMovie("pal3_end_perfect.bik");
            a.EmitAssign(kSceVarMainStory, 100000);
            sce.procs[902] = a.BuildProc(902, "finale_boss");
        }

        suite.m23_cpk = BuildPal3CpkArchive({
            {"m23.scn", SerializeScnFile(scn), CpkCodec::lzo},
            {"m23.sce", SerializeSceFile(sce), CpkCodec::lzo},
            {"m23/m23.nav", SerializeNavFile(BuildDualLayerNavMesh(false)), CpkCodec::lzo},
            {"m23/m23.pol", SerializePolModel(BuildStagePolModel("m23_jianzhong")), CpkCodec::lzo},
            {"m23/_f01.cvd", SerializeCvdModel(BuildMechanismCvdModel("m23_furnace")), CpkCodec::lzo},
        });
    }

    return suite;
}

// ============================================================================
// ChapterHost Implementation
// ============================================================================

ChapterHost::ChapterHost()
    : rng(),
      sce_vm(std::make_unique<SceVm>(*this)),
      combat(database, rng) {
    rng.Seed(0x50414C33u);
    party = database.CreateInitialRoles();
    favor = database.CreateInitialFavor();
}

ChapterHost::~ChapterHost() = default;

void ChapterHost::InitializeFromCpkStreams(std::istream& basedata_cpk,
                                           std::istream& q01_cpk,
                                           std::istream& m01_cpk,
                                           std::istream& q03_cpk,
                                           std::istream& m18_cpk,
                                           std::istream& m23_cpk) {
    CpkPackageReader base_reader(basedata_cpk);
    sce_vm->SetSystemScript(ParseSceFile(base_reader.Read("init.sce")));
    for (const char* code : {"101", "104", "105", "106", "107", "102", "109"}) {
        const std::string path = std::string("role/") + code + "/c01.mv3";
        if (auto bytes = base_reader.ReadIfPresent(path)) {
            role_mv3_models[code] = ParseMv3Model(*bytes);
        }
    }

    auto load_stage_cpk = [&](std::istream& stream,
                              std::string_view cpk_id,
                              std::string_view scn_id,
                              std::string_view base_id,
                              std::string_view title) {
        CpkPackageReader reader(stream);
        LoadedSceneBundle bundle;
        bundle.title = std::string(title);
        bundle.scn = ParseScnFile(reader.Read(std::string(cpk_id) + ".scn"));
        bundle.sce = ParseSceFile(reader.Read(std::string(cpk_id) + ".sce"));
        bundle.nav = ParseNavFile(
            reader.Read(std::string(base_id) + "/" + std::string(base_id) + ".nav"));
        bundle.pol = ParsePolModel(
            reader.Read(std::string(base_id) + "/" + std::string(base_id) + ".pol"));
        bundle.cvd = ParseCvdModel(reader.Read(std::string(base_id) + "/_f01.cvd"));
        scene_catalog[std::string(cpk_id) + "/" + std::string(scn_id)] = std::move(bundle);
    };

    load_stage_cpk(q01_cpk, "q01", "yn01a", "yn01", "渝州·永安当与竹林初遇");
    load_stage_cpk(m01_cpk, "m01", "m01", "m01", "璧山双层机关栈道·九龙坡与大渡口");
    load_stage_cpk(q03_cpk, "q03", "q03", "q03", "蓬莱码头·安宁村宿营与古藤林");
    load_stage_cpk(m18_cpk, "m18", "m18", "m18", "蜀山锁妖塔·冰风谷滑雪与御剑飞行");
    load_stage_cpk(m23_cpk, "m23", "m23", "m23", "雷州剑冢铸剑抉择·新仙界重楼决战");
}

void ChapterHost::BeginNewGame() {
    party = database.CreateInitialRoles();
    favor = database.CreateInitialFavor();
    inventory.clear();
    inventory[6101] = 3;  // 止血草 x3
    inventory[6102] = 2;  // 鼠儿果 x2
    money = 500;
    demon_sword = {};
    leader_role_id = 0;
    leader_nav_layer = 0;
    game_cleared = false;
    ending_name.clear();
    played_movies.clear();

    SceLoadScene("q01", "yn01a");
    RenderFrameToGles();
}

Pal3RoleState& ChapterHost::LeaderRole() {
    for (auto& r : party) {
        if (r.role_id == leader_role_id) return r;
    }
    return party.front();
}

const Pal3RoleState& ChapterHost::LeaderRole() const {
    for (const auto& r : party) {
        if (r.role_id == leader_role_id) return r;
    }
    return party.front();
}

void ChapterHost::RebuildCollisionAndSceneForLayer(std::uint32_t layer) {
    if (active_nav.layers.empty()) return;
    leader_nav_layer =
        std::min<std::uint32_t>(layer, static_cast<std::uint32_t>(active_nav.layers.size() - 1));
    collision_mesh = ConvertNavFileToCollisionMesh(active_nav, leader_nav_layer);
    scene_collision = std::make_unique<pal4::portable::SceneCollision>();
    if (!collision_mesh.floor.empty()) {
        scene_collision->Add(pal4::portable::CollisionGroup::intri, collision_mesh.floor);
    }
    if (!collision_mesh.wall.empty()) {
        scene_collision->Add(pal4::portable::CollisionGroup::wall, collision_mesh.wall);
    }
    displayed_world_scene = ConvertPolToPortableScene(active_pol);
    orbit_camera.Fit(displayed_world_scene);
    world_renderer.Upload(displayed_world_scene);

    movement_config.speed = 90.0f;
    movement_state.position[1] = SampleNavHeight(active_nav.layers[leader_nav_layer],
                                                 movement_state.position[0],
                                                 movement_state.position[2]);
    movement_state.floor =
        pal4::portable::FindFloor(collision_mesh, movement_state.position, 64.0f, 64.0f);
}

void ChapterHost::StepFrames(std::uint32_t frames, float dt_sec) {
    for (std::uint32_t i = 0; i < frames; ++i) {
        animation_clock_sec += dt_sec;
        total_frames++;
        sce_vm->Step(dt_sec, true);
        if (sce_vm->IsWaitingForChoice() || sce_vm->IsWaitingForCombat() ||
            sce_vm->IsWaitingForMinigame()) {
            break;
        }
    }
    RenderFrameToGles();
}

bool ChapterHost::CheckAndFireStepTriggers() {
    if (active_nav.layers.empty()) return false;
    const auto& cur_layer = active_nav.layers[leader_nav_layer];
    const auto tile = WorldToNavTile(cur_layer, movement_state.position[0], movement_state.position[2]);

    // Check `.scn` tile rect triggers (`node_type == 0` or `14`)
    for (auto& obj : scene_objects) {
        if (!obj.active || obj.times == 0 || obj.sce_proc_id == 0) continue;
        const bool is_tile_trig =
            (obj.node_type == static_cast<std::uint16_t>(ScnNodeType::tile_trigger) &&
             obj.nav_layer == leader_nav_layer) ||
            (obj.node_type == static_cast<std::uint16_t>(ScnNodeType::cross_layer_trigger));
        if (!is_tile_trig) continue;

        if (tile[0] >= obj.nav_trigger_min[0] && tile[0] <= obj.nav_trigger_max[0] &&
            tile[1] >= obj.nav_trigger_min[1] && tile[1] <= obj.nav_trigger_max[1]) {
            if (obj.times < 255) obj.times--;
            const std::uint32_t proc_id = obj.sce_proc_id;
            sce_vm->CallProc(proc_id);
            sce_vm->Step(1.0f / 60.0f, true);
            return true;
        }
    }
    return false;
}

std::string ChapterHost::MoveLeaderTo(float target_x, float target_z,
                                      std::optional<std::uint32_t> target_layer,
                                      std::uint32_t max_steps) {
    if (active_nav.layers.empty() || !scene_collision) {
        return "move_failed_no_nav";
    }
    const std::uint32_t dest_layer = target_layer.value_or(leader_nav_layer);

    // Check if the destination tile on `dest_layer` is an impassable wall/border (`distance_to_border == 0`)
    const auto& dest_nav_layer =
        active_nav.layers[std::min<std::size_t>(dest_layer, active_nav.layers.size() - 1)];
    const auto raw_dest_tile = WorldToNavTile(dest_nav_layer, target_x, target_z);
    const bool dest_is_wall = !IsNavTileWalkable(dest_nav_layer, raw_dest_tile[0], raw_dest_tile[1]);

    // If destination is a wall on the same layer, step directly with OpenPAL4 `StepActorMovement`
    // so wall collision is authentically detected and clamped!
    if (dest_is_wall && dest_layer == leader_nav_layer) {
        std::uint32_t wall_stall_frames = 0;
        for (std::uint32_t s = 0; s < max_steps; ++s) {
            const float dx = target_x - movement_state.position[0];
            const float dz = target_z - movement_state.position[2];
            const float dist = std::hypot(dx, dz);
            if (dist <= 2.0f) break;
            const pal4::portable::Point3 input{dx / dist, 0.0f, dz / dist};
            const auto before = movement_state.position;
            pal4::portable::StepActorMovement(
                movement_state, movement_config, collision_mesh, input, 1.0f / 30.0f);
            if (CheckAndFireStepTriggers()) {
                RenderFrameToGles();
                return "move_triggered_script";
            }
            if (movement_state.last_obstacle == pal4::portable::MovementObstacle::wall ||
                std::hypot(movement_state.position[0] - before[0],
                           movement_state.position[2] - before[2]) < 0.05f) {
                if (++wall_stall_frames >= 2) {
                    RenderFrameToGles();
                    return "move_blocked_by_wall";
                }
            }
        }
        RenderFrameToGles();
        return "move_blocked_by_wall";
    }

    // Otherwise, compute dual-layer A* path (`FindPal3NavPath`)
    std::vector<ScnNode> raw_nodes;
    for (const auto& o : scene_objects) {
        ScnNode n{};
        n.index = static_cast<std::uint8_t>(o.id);
        n.active = o.active ? 1 : 0;
        n.node_type = o.node_type;
        n.nav_layer = o.nav_layer;
        n.ladder_nav_coord1 = o.ladder_coord1;
        n.ladder_nav_coord2 = o.ladder_coord2;
        n.ladder_switch_layer = o.ladder_switch_layer;
        raw_nodes.push_back(n);
    }

    last_nav_path = FindPal3NavPath(active_nav, raw_nodes, leader_nav_layer,
                                    movement_state.position[0], movement_state.position[2],
                                    dest_layer, target_x, target_z);
    if (!last_nav_path.reachable || last_nav_path.waypoints.empty()) {
        return "move_blocked_by_wall";
    }

    const std::string pre_cpk = current_cpk;
    const std::string pre_scn = current_scn;

    for (std::size_t wi = 0; wi < last_nav_path.waypoints.size(); ++wi) {
        const auto& wp = last_nav_path.waypoints[wi];
        if (wp.layer != leader_nav_layer) {
            RebuildCollisionAndSceneForLayer(wp.layer);
        }
        const float wx = (wi + 1 == last_nav_path.waypoints.size()) ? target_x : wp.world_pos[0];
        const float wz = (wi + 1 == last_nav_path.waypoints.size()) ? target_z : wp.world_pos[2];

        for (std::uint32_t s = 0; s < max_steps; ++s) {
            const float dx = wx - movement_state.position[0];
            const float dz = wz - movement_state.position[2];
            const float dist = std::hypot(dx, dz);
            if (dist <= 4.0f) {
                movement_state.position[0] = wx;
                movement_state.position[2] = wz;
                movement_state.position[1] =
                    SampleNavHeight(active_nav.layers[leader_nav_layer], wx, wz);
                break;
            }
            const pal4::portable::Point3 input{dx / dist, 0.0f, dz / dist};
            pal4::portable::StepActorMovement(
                movement_state, movement_config, collision_mesh, input, 1.0f / 30.0f);
            if (CheckAndFireStepTriggers()) {
                RenderFrameToGles();
                return (current_cpk != pre_cpk || current_scn != pre_scn) ? "scene_transitioned"
                                                                          : "move_triggered_script";
            }
        }
        if (CheckAndFireStepTriggers()) {
            RenderFrameToGles();
            return (current_cpk != pre_cpk || current_scn != pre_scn) ? "scene_transitioned"
                                                                      : "move_triggered_script";
        }
    }

    RenderFrameToGles();
    return last_nav_path.crossed_layers ? "move_reached_cross_layer" : "move_reached";
}

std::string ChapterHost::Interact(std::string_view target_kind, std::int32_t target_id) {
    // 1. Check Ladder / Elevator interaction (`node_type == 15 | 40`)
    for (auto& obj : scene_objects) {
        if (!obj.active) continue;
        const bool is_ladder =
            (obj.node_type == static_cast<std::uint16_t>(ScnNodeType::ladder) ||
             obj.node_type == static_cast<std::uint16_t>(ScnNodeType::elevator));
        if (!is_ladder) continue;
        if ((!target_kind.empty() && target_kind != "ladder") ||
            (target_id >= 0 && obj.id != target_id)) {
            continue;
        }
        const float dist = std::hypot(obj.position[0] - movement_state.position[0],
                                      obj.position[2] - movement_state.position[2]);
        if (dist <= 100.0f || target_id == obj.id) {
            const std::uint32_t next_layer =
                static_cast<std::uint32_t>((static_cast<int>(leader_nav_layer) + obj.ladder_switch_layer) & 1);
            RebuildCollisionAndSceneForLayer(next_layer);
            const auto exit_world = NavTileToWorld(
                active_nav.layers[leader_nav_layer], obj.ladder_coord2[0], obj.ladder_coord2[1]);
            movement_state.position = {exit_world[0], exit_world[1], exit_world[2]};
            RenderFrameToGles();
            return "ladder_traversed";
        }
    }

    // 2. Find nearest matching NPC (`ScnRole`) or Object/Mechanism/AABB (`ScnNode`)
    float best_dist = 100.0f;
    std::uint32_t best_proc = 0;
    std::string chosen_label;

    if (target_kind.empty() || target_kind == "npc") {
        for (const auto& npc : scene_npcs) {
            if (!npc.active || npc.sce_proc_id == 0) continue;
            if (target_id >= 0 && npc.id != target_id) continue;
            const float d = std::hypot(npc.position[0] - movement_state.position[0],
                                       npc.position[2] - movement_state.position[2]);
            if (d <= best_dist) {
                best_dist = d;
                best_proc = npc.sce_proc_id;
                chosen_label = "npc:" + npc.name;
            }
        }
    }

    if (target_kind.empty() || target_kind == "object" || target_kind == "aabb") {
        for (auto& obj : scene_objects) {
            if (!obj.active || obj.times == 0 || obj.sce_proc_id == 0) continue;
            if (obj.node_type == static_cast<std::uint16_t>(ScnNodeType::tile_trigger)) continue;
            if (target_id >= 0 && obj.id != target_id) continue;
            const float d = std::hypot(obj.position[0] - movement_state.position[0],
                                       obj.position[2] - movement_state.position[2]);
            if (d <= best_dist) {
                best_dist = d;
                best_proc = obj.sce_proc_id;
                chosen_label = "object:" + obj.name;
                if (obj.times < 255) obj.times--;
            }
        }
    }

    if (best_proc == 0) {
        return "interact_none_in_range";
    }

    dialog.recent_history.clear();
    sce_vm->CallProc(best_proc);
    sce_vm->Step(1.0f / 60.0f, true);
    RenderFrameToGles();
    return "interacted:" + chosen_label;
}

std::string ChapterHost::ChooseDialogOption(std::int32_t choice_index) {
    if (!dialog.choice_pending) {
        return "no_choice_pending";
    }
    dialog.choice_pending = false;
    dialog.choices.clear();
    sce_vm->SubmitDialogSelection(choice_index);

    // Apply Sword Tomb choice effect (`m23` proc 901)
    if (current_cpk == "m23") {
        if (choice_index == 0) {
            demon_sword.equipped_divine_sword = "圣灵魔剑";
            demon_sword.spirit_atk_bonus += 90;
        } else {
            demon_sword.equipped_divine_sword = "圣灵镇妖剑";
            demon_sword.spirit_atk_bonus += 90;
        }
    }

    sce_vm->Step(1.0f / 60.0f, true);
    RenderFrameToGles();
    return "choice_submitted:" + std::to_string(choice_index);
}

std::string ChapterHost::SwitchLeader(std::int32_t role_id) {
    for (const auto& r : party) {
        if (r.role_id == role_id && r.in_team) {
            leader_role_id = role_id;
            RenderFrameToGles();
            return "leader_switched:" + r.name;
        }
    }
    return "leader_switch_failed";
}

std::string ChapterHost::ToggleLongkuiForm() {
    for (auto& r : party) {
        if (r.role_id == 2) {
            r.is_red_longkui = !r.is_red_longkui;
            r.model_code = r.is_red_longkui ? "106" : "105";
            r.element = r.is_red_longkui ? Pal3Element::thunder : Pal3Element::water;
            r.attack += r.is_red_longkui ? 25 : -25;
            RenderFrameToGles();
            return r.is_red_longkui ? "longkui_form:红葵(106/雷)" : "longkui_form:蓝葵(105/水)";
        }
    }
    return "longkui_not_found";
}

std::string ChapterHost::PurifyDemonSword(Pal3Element element) {
    demon_sword.obtained = true;
    demon_sword.purification_level++;
    demon_sword.spirit_atk_bonus += 30;
    demon_sword.infused_elements.push_back(element);
    return "demon_sword_purified:" + std::string(Pal3ElementName(element));
}

CombatTurnOutcome ChapterHost::ExecuteCombatAction(std::string_view action_kind,
                                                   std::uint32_t skill_or_item_id,
                                                   std::uint32_t target_enemy_slot,
                                                   std::string_view slot_name) {
    const auto outcome = combat.ExecuteAction(
        party, demon_sword, inventory, money, action_kind, skill_or_item_id, target_enemy_slot,
        ParseTaijiSlot(slot_name));
    if (outcome.battle_finished) {
        sce_vm->NotifyCombatFinished();
        sce_vm->Step(1.0f / 60.0f, true);
    }
    RenderFrameToGles();
    return outcome;
}

MinigameActionOutcome ChapterHost::ExecuteMinigameAction(std::string_view action,
                                                         std::int32_t option_index,
                                                         std::string_view destination_code) {
    const auto outcome = minigames.SubmitAction(
        action, option_index, destination_code, party, favor, inventory, money);
    if (outcome.completed) {
        sce_vm->NotifyMinigameFinished();
        if (!outcome.trigger_load_cpk.empty()) {
            SceLoadScene(outcome.trigger_load_cpk, outcome.trigger_load_scn);
        } else {
            sce_vm->Step(1.0f / 60.0f, true);
        }
    }
    RenderFrameToGles();
    return outcome;
}

Pal3HostCheckpoint ChapterHost::SaveCheckpoint() const {
    return Pal3HostCheckpoint{
        current_cpk,
        current_scn,
        current_title,
        leader_role_id,
        leader_nav_layer,
        movement_state.position,
        user_input_enabled,
        game_cleared,
        ending_name,
        active_bgm,
        played_movies,
        party,
        favor,
        inventory,
        money,
        demon_sword,
        scene_npcs,
        scene_objects,
        dialog,
        sce_vm->CaptureSnapshot(),
        combat.State(),
        minigames.State(),
    };
}

void ChapterHost::LoadCheckpoint(const Pal3HostCheckpoint& cp) {
    const std::string key = cp.current_cpk + "/" + cp.current_scn;
    auto it = scene_catalog.find(key);
    if (it != scene_catalog.end()) {
        active_nav = it->second.nav;
        active_pol = it->second.pol;
        active_cvd = it->second.cvd;
        sce_vm->SetSceneScript(cp.current_cpk, cp.current_scn, it->second.sce);
    }
    current_cpk = cp.current_cpk;
    current_scn = cp.current_scn;
    current_title = cp.current_title;
    leader_role_id = cp.leader_role_id;
    movement_state.position = cp.leader_position;
    RebuildCollisionAndSceneForLayer(cp.leader_nav_layer);

    user_input_enabled = cp.user_input_enabled;
    game_cleared = cp.game_cleared;
    ending_name = cp.ending_name;
    active_bgm = cp.active_bgm;
    played_movies = cp.played_movies;

    party = cp.party;
    favor = cp.favor;
    inventory = cp.inventory;
    money = cp.money;
    demon_sword = cp.demon_sword;
    scene_npcs = cp.scene_npcs;
    scene_objects = cp.scene_objects;
    dialog = cp.dialog;
    sce_vm->RestoreSnapshot(cp.sce_snapshot);
    combat.RestoreSnapshot(cp.combat_snapshot);
    minigames.RestoreSnapshot(cp.minigame_snapshot);
    RenderFrameToGles();
}

void ChapterHost::RenderFrameToGles(int width, int height) {
    const auto& leader = LeaderRole();
    auto mv3_it = role_mv3_models.find(leader.model_code);
    if (mv3_it != role_mv3_models.end()) {
        leader_mv3_pose = EvaluateMv3Pose(mv3_it->second, animation_clock_sec, false);
        displayed_leader_scene = ConvertMv3ToPortableScene(mv3_it->second, animation_clock_sec, false);
        actor_renderer.Upload(displayed_leader_scene);
    }

    pal4::portable::GlesRenderer::BeginFrame(width, height);
    const float aspect = static_cast<float>(width) / static_cast<float>(std::max(1, height));
    const auto vp = orbit_camera.Matrix(aspect);
    const auto world_model = pal4::portable::InstanceMatrix({0.0f, 0.0f, 0.0f}, 0.0f);
    const auto actor_model =
        pal4::portable::InstanceMatrix(movement_state.position, movement_state.yaw);

    std::uint32_t draws = 0;
    draws += world_renderer.DrawScene(displayed_world_scene, vp, world_model);
    draws += actor_renderer.DrawScene(displayed_leader_scene, vp, actor_model);
    total_draw_calls += draws;
    ++total_frames;
}

// ============================================================================
// ISceEngineServices Implementation
// ============================================================================

void ChapterHost::SceRolePathTo(std::int32_t role_id, std::int32_t nav_x, std::int32_t nav_z) {
    SceRoleSetPos(role_id, nav_x, nav_z);
}

void ChapterHost::SceRoleSetPos(std::int32_t role_id, std::int32_t nav_x, std::int32_t nav_z) {
    if (active_nav.layers.empty()) return;
    const auto world = NavTileToWorld(active_nav.layers[leader_nav_layer], nav_x, nav_z);
    if (role_id < 0 || role_id == leader_role_id) {
        movement_state.position = {world[0], world[1], world[2]};
    } else {
        for (auto& npc : scene_npcs) {
            if (npc.id == role_id) {
                npc.position = world;
            }
        }
    }
}

void ChapterHost::SceRoleSetLayer(std::int32_t role_id, std::int32_t layer) {
    if (role_id < 0 || role_id == leader_role_id) {
        RebuildCollisionAndSceneForLayer(static_cast<std::uint32_t>(std::max(0, layer)));
    }
}

void ChapterHost::SceRoleSetFace(std::int32_t /*role_id*/, std::int32_t /*dir*/) {}

void ChapterHost::SceRoleSetController(std::int32_t role_id) {
    if (role_id >= 0 && role_id < static_cast<std::int32_t>(party.size())) {
        leader_role_id = role_id;
    }
}

float ChapterHost::SceRoleShowAction(std::int32_t /*role_id*/, std::string_view action_name,
                                     std::int32_t /*repeat_mode*/) {
    movement_state.animation = std::string(action_name);
    return 0.25f;
}

void ChapterHost::SceRoleSetVisible(std::int32_t role_id, bool visible) {
    for (auto& npc : scene_npcs) {
        if (npc.id == role_id) npc.visible = visible;
    }
}

void ChapterHost::SceRoleActive(std::int32_t role_id, bool active) {
    for (auto& npc : scene_npcs) {
        if (npc.id == role_id) npc.active = active;
    }
}

void ChapterHost::SceCameraMove(float /*x*/, float /*y*/, float /*z*/, float /*duration_sec*/,
                                std::int32_t /*mode*/) {}

void ChapterHost::SceShowDialog(std::string_view text) {
    std::string speaker = "旁白";
    const auto colon = text.find("：");
    if (colon != std::string_view::npos && colon <= 24) {
        speaker = std::string(text.substr(0, colon));
    }
    dialog.recent_history.push_back({speaker, std::string(text)});
}

void ChapterHost::SceShowDialogSelection(std::span<const std::string> choices) {
    dialog.choice_pending = true;
    dialog.choices.assign(choices.begin(), choices.end());
}

void ChapterHost::SceFavorAdd(std::int32_t role_id, std::int32_t delta) {
    favor[role_id] += delta;
}

void ChapterHost::SceAddItem(std::int32_t item_id, std::int32_t count) {
    inventory[item_id] += count;
    if (item_id == 6301) {
        demon_sword.obtained = true;
        if (demon_sword.equipped_divine_sword == "无") {
            demon_sword.equipped_divine_sword = "魔剑";
        }
    } else if (item_id == 6302) {
        demon_sword.zhenyaojian_obtained = true;
    } else if (item_id == 6401) {
        PurifyDemonSword(Pal3Element::thunder);
    }
}

void ChapterHost::SceRemoveItem(std::int32_t item_id, std::int32_t count) {
    inventory[item_id] = std::max(0, inventory[item_id] - count);
}

bool ChapterHost::SceHaveItem(std::int32_t item_id) const {
    auto it = inventory.find(item_id);
    return it != inventory.end() && it->second > 0;
}

void ChapterHost::SceAddMoney(std::int32_t delta) {
    money = std::max(0, money + delta);
}

void ChapterHost::SceAddSkill(std::int32_t role_id, std::int32_t skill_id) {
    for (auto& r : party) {
        if (r.role_id == role_id) {
            r.learned_skills.push_back(static_cast<std::uint32_t>(skill_id));
        }
    }
}

bool ChapterHost::SceIsInTeam(std::int32_t role_id) const {
    for (const auto& r : party) {
        if (r.role_id == role_id) return r.in_team;
    }
    return false;
}

void ChapterHost::SceSetInTeam(std::int32_t role_id, bool in_team) {
    for (auto& r : party) {
        if (r.role_id == role_id) r.in_team = in_team;
    }
}

void ChapterHost::ScePlayMovie(std::string_view movie_name) {
    played_movies.emplace_back(movie_name);
    if (movie_name.find("pal3_end") != std::string_view::npos) {
        game_cleared = true;
        const int xuejian_fav = favor[1];
        const int longkui_fav = favor[2];
        ending_name = (xuejian_fav >= longkui_fav)
                          ? "完美结局·新仙界决战重楼（雪见相伴渝州）"
                          : "龙葵结局·千年剑灵共归渝州";
    }
}

void ChapterHost::ScePlayMusic(std::string_view bgm_name, std::int32_t /*loop*/) {
    active_bgm = std::string(bgm_name);
}

void ChapterHost::SceLoadScene(std::string_view cpk_name, std::string_view scn_name) {
    const std::string key = std::string(cpk_name) + "/" + std::string(scn_name);
    auto it = scene_catalog.find(key);
    if (it == scene_catalog.end()) {
        throw std::runtime_error("unknown PAL3 scene in catalog: " + key);
    }

    current_cpk = std::string(cpk_name);
    current_scn = std::string(scn_name);
    current_title = it->second.title;
    active_nav = it->second.nav;
    active_pol = it->second.pol;
    active_cvd = it->second.cvd;

    scene_npcs.clear();
    for (const auto& r : it->second.scn.roles) {
        RuntimeRoleEntity npc;
        npc.id = r.index;
        npc.name = r.name;
        const float y = SampleNavHeight(
            active_nav.layers[std::min<std::size_t>(r.nav_layer, active_nav.layers.size() - 1)],
            r.position_x, r.position_z);
        npc.position = {r.position_x, y, r.position_z};
        npc.facing_deg = r.facing_deg;
        npc.nav_layer = r.nav_layer;
        npc.sce_proc_id = r.sce_proc_id;
        scene_npcs.push_back(std::move(npc));
    }

    scene_objects.clear();
    for (const auto& n : it->second.scn.nodes) {
        RuntimeSceneObject obj;
        obj.id = n.index;
        obj.name = n.name;
        obj.node_type = n.node_type;
        obj.position = n.position;
        obj.nav_layer = n.nav_layer;
        obj.active = (n.active != 0);
        obj.times = n.times;
        obj.sce_proc_id = n.sce_proc_id;
        obj.nav_trigger_min = n.nav_trigger_coord_min;
        obj.nav_trigger_max = n.nav_trigger_coord_max;
        obj.ladder_coord1 = n.ladder_nav_coord1;
        obj.ladder_coord2 = n.ladder_nav_coord2;
        obj.ladder_switch_layer = n.ladder_switch_layer;
        obj.aabb_coord1 = n.aabb_trigger_coord1;
        obj.aabb_coord2 = n.aabb_trigger_coord2;
        scene_objects.push_back(std::move(obj));
    }

    movement_state.position = {0.0f, 0.0f, 0.0f};
    RebuildCollisionAndSceneForLayer(0);

    sce_vm->SetSceneScript(cpk_name, scn_name, it->second.sce);
    sce_vm->CallProc(1);
    sce_vm->Step(1.0f / 60.0f, true);
}

void ChapterHost::SceObjectActive(std::int32_t obj_id, bool active) {
    for (auto& obj : scene_objects) {
        if (obj.id == obj_id) obj.active = active;
    }
}

void ChapterHost::SceObjectMove(std::int32_t obj_id, float x, float y, float z,
                                float /*duration_sec*/) {
    for (auto& obj : scene_objects) {
        if (obj.id == obj_id) obj.position = {x, y, z};
    }
}

float ChapterHost::SceObjectPlay(std::int32_t obj_id, std::string_view /*anim_name*/,
                                 float duration_sec) {
    for (auto& obj : scene_objects) {
        if (obj.id == obj_id) {
            obj.cvd_anim_time_sec = std::max(duration_sec, active_cvd.animation_duration_sec);
        }
    }
    displayed_world_scene = ConvertCvdToPortableScene(active_cvd, active_cvd.animation_duration_sec);
    return duration_sec;
}

void ChapterHost::SceConfigureNextCombat(std::optional<std::int32_t> max_round, bool must_fail) {
    combat.ConfigureNextBattle(max_round, must_fail);
}

void ChapterHost::SceStartCombat(std::span<const std::uint32_t> monster_ids, bool is_boss) {
    combat.StartBattle(party, demon_sword, monster_ids, is_boss);
}

void ChapterHost::SceStartAppraisalMinigame(std::uint32_t antique_id) {
    minigames.StartAppraisal(antique_id);
}

void ChapterHost::SceStartEncampmentMinigame(std::uint32_t encamp_id) {
    minigames.StartEncampment(encamp_id);
}

void ChapterHost::SceStartSkiMinigame(std::uint32_t course_id) {
    minigames.StartSkiing(course_id);
}

void ChapterHost::SceOpenYuJianMap() {
    minigames.StartYuJian();
}

void ChapterHost::SceRestAtInn(std::string_view /*prompt*/, std::int32_t cost,
                               std::uint32_t no_money_proc, std::uint32_t after_rest_proc) {
    if (money < cost) {
        if (no_money_proc != 0u) sce_vm->CallProc(no_money_proc);
        return;
    }
    money -= cost;
    for (auto& r : party) {
        r.hp = r.max_hp;
        r.shen = r.max_shen;
        r.qi = r.max_qi;
    }
    if (after_rest_proc != 0u) sce_vm->CallProc(after_rest_proc);
}

}  // namespace pal3::portable
