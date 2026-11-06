#include "pal3/portable/chapter_host.h"
#include "GLES3/gl3.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace pal3::portable {
namespace {

std::string ToLowerAscii(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::optional<std::filesystem::path> FindFileCaseInsensitive(const std::filesystem::path& dir,
                                                             std::string_view filename) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
        return std::nullopt;
    }
    const std::string want = ToLowerAscii(filename);
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.is_regular_file(ec) &&
            ToLowerAscii(entry.path().filename().string()) == want) {
            return entry.path();
        }
    }
    return std::nullopt;
}

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
                const std::uint32_t border_dist =
                    std::min({x, z, layer.width - 1 - x, layer.height - 1 - z});
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

ScnFile MakeSimpleScn(std::string_view cpk, std::string_view scn, std::string_view base,
                      std::vector<ScnRole> roles, std::vector<ScnNode> nodes = {}) {
    ScnFile f;
    f.cpk_name = std::string(cpk);
    f.scn_name = std::string(scn);
    f.scn_base_name = std::string(base);
    f.roles = std::move(roles);
    f.nodes = std::move(nodes);
    return f;
}

}  // namespace

// ============================================================================
// Complete 24-Chapter PAL3 Campaign Archive Builder (`BuildFullPal3CampaignCpkSuite`)
// ============================================================================

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

    // ========================================================================
    // 1. q01.cpk (Act I Towns: Ch.1 渝州永安当 `yn01a`, Ch.5 宾化当铺 `binhua`,
    //                          Ch.7 唐家堡演武堂 `tangjiabao`, Ch.13 雷州刺史府 `leizhou`)
    // ========================================================================
    {
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

        const auto scn_yn01a = MakeSimpleScn(
            "q01", "yn01a", "yn01",
            {
                {1, "赵文昌", 180.0f, 1, 40.0f, 40.0f, 501, "c01", 0, {}, 215.0f},
                {2, "唐雪见", 90.0f, 0, -35.0f, 20.0f, 502, "c01", 0, {}, 215.0f},
                {3, "魔尊·重楼", 0.0f, 0, 0.0f, -45.0f, 503, "c01", 0, {}, 215.0f},
            },
            {ladder, exit_trigger});

        const auto scn_binhua = MakeSimpleScn(
            "q01", "binhua", "yn01",
            {
                {11, "宾化永安当掌柜", 180.0f, 0, 20.0f, 15.0f, 511, "c01", 0, {}, 215.0f},
                {12, "徐长卿与紫萱", 90.0f, 0, -25.0f, -20.0f, 512, "c01", 0, {}, 215.0f},
            });

        const auto scn_tangjiabao = MakeSimpleScn(
            "q01", "tangjiabao", "yn01",
            {
                {13, "唐门长老·唐泰", 180.0f, 0, 0.0f, -25.0f, 513, "c01", 0, {}, 215.0f},
            });

        const auto scn_leizhou = MakeSimpleScn(
            "q01", "leizhou", "yn01",
            {
                {14, "雷州刺史少爷·云霆", 180.0f, 0, 0.0f, -25.0f, 514, "c01", 0, {}, 215.0f},
            });

        SceFile sce;
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 10101);
            a.EmitMusic("pi01.mp3", 1);
            a.EmitDlg("景天：天亮了，赵管事又在永安当二楼账房催着鉴宝了。");
            sce.procs[1] = a.BuildProc(1, "yn01a_init");
        }
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 25100);
            a.EmitDlg("景天：到了宾化城永安当分号，先经营当铺筹些盘缠，再去客栈会合长卿大侠！");
            sce.procs[11] = a.BuildProc(11, "binhua_init");
        }
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 31100);
            a.EmitDlg("唐雪见：爷爷仙逝，唐泰叔父竟勾结霹雳堂夺位，今日定要在唐家堡演武堂讨回公道！");
            sce.procs[12] = a.BuildProc(12, "tangjiabao_init");
        }
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 42100);
            a.EmitDlg("徐长卿：雷州城夜夜雷暴，刺史府云霆公子体内似藏有五灵珠之一的【雷灵珠】！");
            sce.procs[13] = a.BuildProc(13, "leizhou_init");
        }
        {
            // proc 501: Zhao Wenchang antique appraisal tutorial + SceProcHooks (10101 -> 10202)
            SceAssembler a;
            a.EmitDlg("赵文昌：景天！快把这把【宜兴紫砂壶】的赝品破绽找出来！");
            a.EmitApprEntry(1);
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
            // proc 511: Ch.5 Binhua Pawnshop Management (`PawnshopEntry`)
            SceAssembler a;
            a.EmitDlg("宾化当铺掌柜：景天小哥，分号柜台交给你打理，看看这件死当唐三彩如何估价！");
            a.EmitPawnshopEntry(1);
            a.EmitAssign(kSceVarMainStory, 25500);
            sce.procs[511] = a.BuildProc(511, "binhua_pawnshop");
        }
        {
            // proc 512: Ch.5 Meet Xu Changqing & Zixuan in Binhua -> Sail to Ch.6 Penglai (`q03/q03`)
            SceAssembler a;
            a.EmitDlg("徐长卿：贫道蜀山徐长卿，这位是紫萱姑娘。蓬莱仙岛告急，我们一同渡海前往！");
            a.EmitInTeam(3, 1);
            a.EmitInTeam(4, 1);
            a.EmitInTeam(5, 1);
            a.EmitAssign(kSceVarMainStory, 26000);
            a.EmitLoadScene("q03", "q03");
            sce.procs[512] = a.BuildProc(512, "binhua_meet_changqing");
        }
        {
            // proc 513: Ch.7 Tangjiabao Tournament vs Elder Tang Tai (`9007`) -> to Ch.8 Pilittang Dungeon (`m01/m06`)
            SceAssembler a;
            a.EmitDlg("唐泰：雪见丫头，赢过老夫的五毒暗器才有资格过问唐门掌门令牌！");
            a.EmitCombatBoss({9007, 0, 0, 0, 0, 0});
            a.EmitGetCombat(0);
            a.EmitAssign(kSceVarMainStory, 32000);
            a.EmitLoadScene("m01", "m06");
            sce.procs[513] = a.BuildProc(513, "tangjiabao_boss");
        }
        {
            // proc 514: Ch.13 Leizhou Yunting (`9008`) -> Obtain Thunder Spirit Pearl (`6401`) -> to Ch.14 Shenmo Well (`m18/shenmo`)
            SceAssembler a;
            a.EmitDlg("云霆：若景兄弟能承受我体内雷灵之力而不倒，云某便将【雷灵珠】双手奉上！");
            a.EmitCombatBoss({9008, 0, 0, 0, 0, 0});
            a.EmitGetCombat(0);
            a.EmitAddItem(6401, 1);
            a.EmitAssign(kSceVarMainStory, 43000);
            a.EmitLoadScene("m18", "shenmo");
            sce.procs[514] = a.BuildProc(514, "leizhou_yunting");
        }
        {
            // proc 599: Step trigger to Stage 2 Bishan (`m01`)
            SceAssembler a;
            a.EmitLoadScene("m01", "m01");
            sce.procs[599] = a.BuildProc(599, "to_m01");
        }

        suite.q01_cpk = BuildPal3CpkArchive({
            {"q01.scn", SerializeScnFile(scn_yn01a), CpkCodec::lzo},
            {"yn01a.scn", SerializeScnFile(scn_yn01a), CpkCodec::lzo},
            {"binhua.scn", SerializeScnFile(scn_binhua), CpkCodec::lzo},
            {"tangjiabao.scn", SerializeScnFile(scn_tangjiabao), CpkCodec::lzo},
            {"leizhou.scn", SerializeScnFile(scn_leizhou), CpkCodec::lzo},
            {"q01.sce", SerializeSceFile(sce), CpkCodec::lzo},
            {"yn01/yn01.nav", SerializeNavFile(BuildDualLayerNavMesh(true)), CpkCodec::lzo},
            {"yn01/yn01.pol", SerializePolModel(BuildStagePolModel("yn01_hall")), CpkCodec::lzo},
            {"yn01/_f01.cvd", SerializeCvdModel(BuildMechanismCvdModel("yn01_chest")), CpkCodec::lzo},
        });
    }

    // ========================================================================
    // 2. m01.cpk (Act II Dungeons: Ch.2 璧山 `m01`, Ch.3 九龙坡 `m02`, Ch.4 大渡口 `m03`,
    //                              Ch.8 德阳霹雳堂地牢 `m06`, Ch.12 九顶山 `m10`)
    // ========================================================================
    {
        ScnNode bridge_node{};
        bridge_node.index = 30;
        bridge_node.active = 1;
        bridge_node.name = "_f01.cvd";
        bridge_node.node_type = static_cast<std::uint16_t>(ScnNodeType::mechanism);
        bridge_node.position = {30.0f, 0.0f, 15.0f};
        bridge_node.nav_layer = 0;
        bridge_node.sce_proc_id = 601;

        const auto scn_m01 = MakeSimpleScn(
            "m01", "m01", "m01",
            {
                {4, "霹雳堂头目", 180.0f, 0, 0.0f, -20.0f, 602, "c01", 0, {}, 215.0f},
                {5, "龙葵", 90.0f, 0, -30.0f, -35.0f, 603, "c01", 0, {}, 215.0f},
                {15, "九龙坡栈道引路人", 0.0f, 0, 35.0f, -35.0f, 604, "c01", 0, {}, 215.0f},
            },
            {bridge_node});

        const auto scn_m02 = MakeSimpleScn(
            "m01", "m02", "m01",
            {
                {16, "九龙坡·霹雳堂坛主", 180.0f, 0, 0.0f, -25.0f, 611, "c01", 0, {}, 215.0f},
            });

        const auto scn_m03 = MakeSimpleScn(
            "m01", "m03", "m01",
            {
                {17, "大渡口·剑灵龙葵", 180.0f, 0, 0.0f, -25.0f, 612, "c01", 0, {}, 215.0f},
            });

        const auto scn_m06 = MakeSimpleScn(
            "m01", "m06", "m01",
            {
                {18, "霹雳堂堂主·罗如烈", 180.0f, 0, 0.0f, -25.0f, 613, "c01", 0, {}, 215.0f},
            });

        const auto scn_m10 = MakeSimpleScn(
            "m01", "m10", "m01",
            {
                {19, "九顶山·人面兽", 180.0f, 0, 0.0f, -25.0f, 614, "c01", 0, {}, 215.0f},
            });

        SceFile sce;
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 20100);
            a.EmitDlg("唐雪见：前面是璧山机关栈道，快去开启右侧的石桥机关！");
            sce.procs[1] = a.BuildProc(1, "m01_init");
        }
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 21100);
            a.EmitDlg("景天：穿过璧山就到九龙坡了，小心前面林子里的霹雳堂暗哨！");
            sce.procs[11] = a.BuildProc(11, "m02_init");
        }
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 22100);
            a.EmitDlg("唐雪见：大渡口古墓里的魔剑气场越来越强，好像有人要从剑里出来了！");
            sce.procs[12] = a.BuildProc(12, "m03_init");
        }
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 33100);
            a.EmitDlg("景天：这里就是德阳霹雳堂总舵水牢，打倒罗如烈救出雪见！");
            sce.procs[13] = a.BuildProc(13, "m06_init");
        }
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 40100);
            a.EmitDlg("紫萱：九顶山定仙峰寒气逼人，上古异兽【人面兽】就在前方高台。");
            sce.procs[14] = a.BuildProc(14, "m10_init");
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
            // proc 603: Fast-path Longkui joins & transition to Stage 3 Penglai (`q03`)
            SceAssembler a;
            a.EmitDlg("龙葵：王兄……千年等待，龙葵终于从魔剑中再见到王兄了！");
            a.EmitInTeam(2, 1);
            a.EmitFavorAdd(2, 20);
            a.EmitAssign(kSceVarMainStory, 20500);
            a.EmitLoadScene("q03", "q03");
            sce.procs[603] = a.BuildProc(603, "longkui_join");
        }
        {
            // proc 604: Full 24-chapter path from Ch.2 Bishan (`m01/m01`) -> Ch.3 Jiulongpo (`m01/m02`)
            SceAssembler a;
            a.EmitLoadScene("m01", "m02");
            sce.procs[604] = a.BuildProc(604, "to_m02_jiulongpo");
        }
        {
            // proc 611: Ch.3 Jiulongpo Boss -> Ch.4 Dadukou (`m01/m03`)
            SceAssembler a;
            a.EmitDlg("霹雳堂坛主：九龙坡已被霹雳堂封锁，休想过去！");
            a.EmitCombatBoss({9002, 0, 0, 0, 0, 0});
            a.EmitGetCombat(0);
            a.EmitAssign(kSceVarMainStory, 21500);
            a.EmitLoadScene("m01", "m03");
            sce.procs[611] = a.BuildProc(611, "m02_boss");
        }
        {
            // proc 612: Ch.4 Dadukou Longkui awakens -> Ch.5 Binhua (`q01/binhua`)
            SceAssembler a;
            a.EmitDlg("龙葵：王兄，龙葵护驾来迟！我们随船前往宾化城吧！");
            a.EmitInTeam(2, 1);
            a.EmitFavorAdd(2, 20);
            a.EmitAssign(kSceVarMainStory, 23000);
            a.EmitLoadScene("q01", "binhua");
            sce.procs[612] = a.BuildProc(612, "m03_longkui_to_binhua");
        }
        {
            // proc 613: Ch.8 Pilittang Dungeon Boss Luo Rulie (`9009`) -> Obtain Earth Spirit Pearl (`6404`) -> Ch.9 Anning (`q03/anning`)
            SceAssembler a;
            a.EmitDlg("罗如烈：本堂主已炼成五毒金身，连同这颗【土灵珠】一起成为我的养料吧！");
            a.EmitCombatBoss({9009, 0, 0, 0, 0, 0});
            a.EmitGetCombat(0);
            a.EmitAddItem(6404, 1);
            a.EmitAssign(kSceVarMainStory, 34000);
            a.EmitLoadScene("q03", "anning");
            sce.procs[613] = a.BuildProc(613, "m06_luorulie_earth_pearl");
        }
        {
            // proc 614: Ch.12 Jiudingshan Boss Renmianshou (`9010`) -> Ch.13 Leizhou (`q01/leizhou`)
            SceAssembler a;
            a.EmitDlg("人面兽：凡人竟敢踏入九顶山定仙峰，受死！");
            a.EmitCombatBoss({9010, 0, 0, 0, 0, 0});
            a.EmitGetCombat(0);
            a.EmitAssign(kSceVarMainStory, 41000);
            a.EmitLoadScene("q01", "leizhou");
            sce.procs[614] = a.BuildProc(614, "m10_renmianshou");
        }

        suite.m01_cpk = BuildPal3CpkArchive({
            {"m01.scn", SerializeScnFile(scn_m01), CpkCodec::lzo},
            {"m02.scn", SerializeScnFile(scn_m02), CpkCodec::lzo},
            {"m03.scn", SerializeScnFile(scn_m03), CpkCodec::lzo},
            {"m06.scn", SerializeScnFile(scn_m06), CpkCodec::lzo},
            {"m10.scn", SerializeScnFile(scn_m10), CpkCodec::lzo},
            {"m01.sce", SerializeSceFile(sce), CpkCodec::lzo},
            {"m01/m01.nav", SerializeNavFile(BuildDualLayerNavMesh(false)), CpkCodec::lzo},
            {"m01/m01.pol", SerializePolModel(BuildStagePolModel("m01_bishan")), CpkCodec::lzo},
            {"m01/_f01.cvd", SerializeCvdModel(BuildMechanismCvdModel("_f01.cvd")), CpkCodec::lzo},
        });
    }

    // ========================================================================
    // 3. q03.cpk (Act III: Ch.6 蓬莱御剑堂 `q03`, Ch.9 安宁村 `anning`, Ch.10 古藤林 `guteng`,
    //                      Ch.11 蜀山无极阁 `shushan`, Ch.19-20 蛮州女娲庙与灵山 `manzhou`)
    // ========================================================================
    {
        const auto scn_q03 = MakeSimpleScn(
            "q03", "q03", "q03",
            {
                {6, "紫萱与徐长卿", 180.0f, 0, 25.0f, 10.0f, 701, "c01", 0, {}, 215.0f},
                {7, "古藤仙人", 0.0f, 0, 0.0f, -35.0f, 702, "c01", 0, {}, 215.0f},
                {20, "蓬莱·邪剑仙幻影", 90.0f, 0, -25.0f, 10.0f, 703, "c01", 0, {}, 215.0f},
            });

        const auto scn_anning = MakeSimpleScn(
            "q03", "anning", "q03",
            {
                {21, "安宁村篝火守夜人", 180.0f, 0, 0.0f, -20.0f, 711, "c01", 0, {}, 215.0f},
            });

        const auto scn_guteng = MakeSimpleScn(
            "q03", "guteng", "q03",
            {
                {22, "古藤林·古藤仙人", 180.0f, 0, 0.0f, -25.0f, 712, "c01", 0, {}, 215.0f},
            });

        const auto scn_shushan = MakeSimpleScn(
            "q03", "shushan", "q03",
            {
                {23, "蜀山掌门·清微道长", 180.0f, 0, 0.0f, -20.0f, 713, "c01", 0, {}, 215.0f},
            });

        const auto scn_manzhou = MakeSimpleScn(
            "q03", "manzhou", "q03",
            {
                {24, "蛮州古玩商", 180.0f, 0, 20.0f, 10.0f, 714, "c01", 0, {}, 215.0f},
                {25, "灵山仙人洞·银眉妖狐", 0.0f, 0, -20.0f, -25.0f, 715, "c01", 0, {}, 215.0f},
            });

        SceFile sce;
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 30100);
            a.EmitDlg("徐长卿：景兄弟，蜀山锁妖塔异动，紫萱与贫道特来相助！");
            sce.procs[1] = a.BuildProc(1, "q03_init");
        }
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 35100);
            a.EmitDlg("紫萱：安宁村夜色宁静，今夜大家围坐篝火谈心休整，明日再入古藤林。");
            sce.procs[11] = a.BuildProc(11, "anning_init");
        }
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 36100);
            a.EmitDlg("花楹：主人小心！古藤林深处的千年树妖【古藤仙人】正窥视着我们！");
            sce.procs[12] = a.BuildProc(12, "guteng_init");
        }
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 38100);
            a.EmitDlg("清微道长：锁妖塔五灵封印松动，长卿、景天，速往九顶山与雷州寻回其余灵珠！");
            sce.procs[13] = a.BuildProc(13, "shushan_init");
        }
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 66100);
            a.EmitDlg("紫萱：重返苗疆蛮州女娲庙，三世情缘恍若回魂仙梦……前面灵山仙人洞便是通往海底城之路。");
            sce.procs[14] = a.BuildProc(14, "manzhou_init");
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
            // proc 702: Fast-path Defeat Ancient Vine Immortal (`古藤仙人`) -> obtain Thunder Spirit Pearl -> to Stage 4 `m18`
            SceAssembler a;
            a.EmitDlg("古藤仙人：擅闯古藤林者，休想取走雷灵珠！");
            a.EmitCombatBoss({9003, 0, 0, 0, 0, 0});
            a.EmitGetCombat(0);
            a.EmitAddItem(6401, 1);
            a.EmitAssign(kSceVarMainStory, 30600);
            a.EmitLoadScene("m18", "m18");
            sce.procs[702] = a.BuildProc(702, "guteng_boss");
        }
        {
            // proc 703: Ch.6 Penglai Evil Sword Phantom (`9011`) -> Ch.7 Tangjiabao (`q01/tangjiabao`)
            SceAssembler a;
            a.EmitDlg("邪剑仙幻影：哈哈哈哈！蓬莱御剑堂已被本座屠尽，尔等也来送死？");
            a.EmitCombatBoss({9011, 0, 0, 0, 0, 0});
            a.EmitGetCombat(0);
            a.EmitAssign(kSceVarMainStory, 30800);
            a.EmitLoadScene("q01", "tangjiabao");
            sce.procs[703] = a.BuildProc(703, "penglai_phantom_boss");
        }
        {
            // proc 711: Ch.9 Anning Encampment (`ENCAMP_Entry`) -> Ch.10 Guteng Forest (`q03/guteng`)
            SceAssembler a;
            a.EmitEncampEntry(1);
            a.EmitAssign(kSceVarMainStory, 35500);
            a.EmitLoadScene("q03", "guteng");
            sce.procs[711] = a.BuildProc(711, "anning_to_guteng");
        }
        {
            // proc 712: Ch.10 Guteng Boss (`9003`) -> Ch.11 Shushan (`q03/shushan`)
            SceAssembler a;
            a.EmitDlg("古藤仙人：老夫盘踞古藤林千年，岂容尔等放肆！");
            a.EmitCombatBoss({9003, 0, 0, 0, 0, 0});
            a.EmitGetCombat(0);
            a.EmitAssign(kSceVarMainStory, 37000);
            a.EmitLoadScene("q03", "shushan");
            sce.procs[712] = a.BuildProc(712, "guteng_to_shushan");
        }
        {
            // proc 713: Ch.11 Shushan Elder Qingwei -> Ch.12 Jiudingshan (`m01/m10`)
            SceAssembler a;
            a.EmitDlg("清微道长：五长老已为你等加持真元，快去九顶山与雷州吧！");
            a.EmitAssign(kSceVarMainStory, 39000);
            a.EmitLoadScene("m01", "m10");
            sce.procs[713] = a.BuildProc(713, "shushan_to_jiudingshan");
        }
        {
            // proc 714: Ch.19-20 Manzhou Celadon Bowl Appraisal (`APPREntry(2)`)
            SceAssembler a;
            a.EmitDlg("蛮州古玩商：客官既然懂行，不妨鉴定一下这只【越窑秘色青瓷莲花碗】！");
            a.EmitApprEntry(2);
            a.EmitGetAppr(0);
            a.EmitAssign(kSceVarMainStory, 67000);
            sce.procs[714] = a.BuildProc(714, "manzhou_appraisal");
        }
        {
            // proc 715: Ch.20 Lingshan Silver-Brow Fox (`9012`) -> Ch.21 Undersea City (`m18/haidicheng`)
            SceAssembler a;
            a.EmitDlg("银眉妖狐：想借道灵山仙人洞前往东海海底城，先问过本仙姑！");
            a.EmitCombatBoss({9012, 0, 0, 0, 0, 0});
            a.EmitGetCombat(0);
            a.EmitAssign(kSceVarMainStory, 68000);
            a.EmitLoadScene("m18", "haidicheng");
            sce.procs[715] = a.BuildProc(715, "lingshan_fox_to_haidicheng");
        }

        suite.q03_cpk = BuildPal3CpkArchive({
            {"q03.scn", SerializeScnFile(scn_q03), CpkCodec::lzo},
            {"anning.scn", SerializeScnFile(scn_anning), CpkCodec::lzo},
            {"guteng.scn", SerializeScnFile(scn_guteng), CpkCodec::lzo},
            {"shushan.scn", SerializeScnFile(scn_shushan), CpkCodec::lzo},
            {"manzhou.scn", SerializeScnFile(scn_manzhou), CpkCodec::lzo},
            {"q03.sce", SerializeSceFile(sce), CpkCodec::lzo},
            {"q03/q03.nav", SerializeNavFile(BuildDualLayerNavMesh(false)), CpkCodec::lzo},
            {"q03/q03.pol", SerializePolModel(BuildStagePolModel("q03_penglai")), CpkCodec::lzo},
            {"q03/_f01.cvd", SerializeCvdModel(BuildMechanismCvdModel("q03_altar")), CpkCodec::lzo},
        });
    }

    // ========================================================================
    // 4. m18.cpk (Act IV: Ch.14 神魔之井 `shenmo`, Ch.15 神界天庭 `shenjie`,
    //                     Ch.16 鬼界黄泉路 `guijie`, Ch.17 鬼界熔岩地狱 `rongyan`,
    //                     Ch.18 蜀山锁妖塔 `m18`, Ch.21 东海海底城 `haidicheng`)
    // ========================================================================
    {
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

        const auto scn_shenmo = MakeSimpleScn(
            "m18", "shenmo", "m18",
            {
                {26, "神魔之井·神魔守卫", 180.0f, 0, 0.0f, -25.0f, 812, "c01", 0, {}, 215.0f},
            });

        const auto scn_shenjie = MakeSimpleScn(
            "m18", "shenjie", "m18",
            {
                {27, "神界天池·夕瑶", 180.0f, 0, 0.0f, -20.0f, 813, "c01", 0, {}, 215.0f},
            });

        const auto scn_guijie = MakeSimpleScn(
            "m18", "guijie", "m18",
            {
                {28, "鬼界无常殿·赵无延", 180.0f, 0, 0.0f, -25.0f, 814, "c01", 0, {}, 215.0f},
            });

        const auto scn_rongyan = MakeSimpleScn(
            "m18", "rongyan", "m18",
            {
                {29, "熔岩地狱·火鬼王", 180.0f, 0, 0.0f, -25.0f, 815, "c01", 0, {}, 215.0f},
            });

        const auto scn_m18 = MakeSimpleScn(
            "m18", "m18", "m18",
            {
                {8, "天妖皇", 180.0f, 1, 45.0f, 45.0f, 801, "c01", 0, {}, 215.0f},
                {30, "锁妖塔底层·天妖皇真身", 180.0f, 1, -35.0f, 45.0f, 816, "c01", 0, {}, 215.0f},
            },
            {elevator});

        const auto scn_haidicheng = MakeSimpleScn(
            "m18", "haidicheng", "m18",
            {
                {31, "海底城·帝江神兽", 180.0f, 0, 0.0f, -25.0f, 817, "c01", 0, {}, 215.0f},
            });

        SceFile sce;
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 50100);
            a.EmitDlg("徐长卿：已入锁妖塔底层，搭乘铁链升降台即可抵达天妖皇封印坛！");
            sce.procs[1] = a.BuildProc(1, "m18_init");
        }
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 44100);
            a.EmitDlg("重楼：此处乃连通神魔两界的【神魔之井】，闯过守卫即可登上神界天庭！");
            sce.procs[11] = a.BuildProc(11, "shenmo_init");
        }
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 46100);
            a.EmitDlg("夕瑶：飞蓬……千年已过，神树旁的【风灵珠】今日终于等到你来取走。");
            sce.procs[12] = a.BuildProc(12, "shenjie_init");
        }
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 48100);
            a.EmitDlg("景天：已入鬼界黄泉路，必须闯过无常殿赵无延，去熔岩地狱救回雪见魂魄！");
            sce.procs[13] = a.BuildProc(13, "guijie_init");
        }
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 49100);
            a.EmitDlg("火鬼王：想救那丫头还魂，还得看你有没有本事从本王手中夺走【火灵珠】！");
            sce.procs[14] = a.BuildProc(14, "rongyan_init");
        }
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 70100);
            a.EmitDlg("紫萱：东海海底城深处镇压着最后一颗五灵珠【水灵珠】，击败帝江神兽便可集齐五灵！");
            sce.procs[15] = a.BuildProc(15, "haidicheng_init");
        }
        {
            // proc 801: Fast-path Defeat Emperor of Sky Demons (`天妖皇`) -> obtain `镇妖剑` -> Skiing (`SKEE_Entry`) -> `YuJian`
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
        {
            // proc 812: Ch.14 Shenmo Well Boss (`9013`) -> Ch.15 Divine Realm (`m18/shenjie`)
            SceAssembler a;
            a.EmitDlg("神魔守卫：擅闯神魔之井者，格杀勿论！");
            a.EmitCombatBoss({9013, 0, 0, 0, 0, 0});
            a.EmitGetCombat(0);
            a.EmitAssign(kSceVarMainStory, 45000);
            a.EmitLoadScene("m18", "shenjie");
            sce.procs[812] = a.BuildProc(812, "shenmo_boss");
        }
        {
            // proc 813: Ch.15 Xiyao gifts Wind Spirit Pearl (`6402`) -> Ch.16 Ghost Realm (`m18/guijie`)
            SceAssembler a;
            a.EmitDlg("夕瑶：愿这颗【风灵珠】护佑你与雪见平安归来。");
            a.EmitAddItem(6402, 1);
            a.EmitFavorAdd(1, 15);
            a.EmitAssign(kSceVarMainStory, 47000);
            a.EmitLoadScene("m18", "guijie");
            sce.procs[813] = a.BuildProc(813, "shenjie_xiyao_wind_pearl");
        }
        {
            // proc 814: Ch.16 Ghost Realm Zhao Wuyan (`9014`) -> Ch.17 Melt Hell (`m18/rongyan`)
            SceAssembler a;
            a.EmitDlg("赵无延：无常殿轮回盘岂是活人能碰的？看招！");
            a.EmitCombatBoss({9014, 0, 0, 0, 0, 0});
            a.EmitGetCombat(0);
            a.EmitAssign(kSceVarMainStory, 48500);
            a.EmitLoadScene("m18", "rongyan");
            sce.procs[814] = a.BuildProc(814, "guijie_zhaowuyan");
        }
        {
            // proc 815: Ch.17 Fire Ghost King (`9015`) -> Obtain Fire Spirit Pearl (`6405`) -> Ch.18 Demon-Locking Tower (`m18/m18`)
            SceAssembler a;
            a.EmitDlg("火鬼王：可恶！我的【火灵珠】竟被尔等夺去！");
            a.EmitCombatBoss({9015, 0, 0, 0, 0, 0});
            a.EmitGetCombat(0);
            a.EmitAddItem(6405, 1);
            a.EmitAssign(kSceVarMainStory, 49800);
            a.EmitLoadScene("m18", "m18");
            sce.procs[815] = a.BuildProc(815, "rongyan_fire_pearl");
        }
        {
            // proc 816: Ch.18 Full-campaign Suoyaota Boss (`9004`) -> Obtain `镇妖剑` (`6302`) -> Ch.19 Manzhou (`q03/manzhou`)
            SceAssembler a;
            a.EmitDlg("天妖皇：锁妖塔底层封印已破，接招吧！");
            a.EmitCombatBoss({9004, 0, 0, 0, 0, 0});
            a.EmitGetCombat(0);
            a.EmitAddItem(6302, 1);
            a.EmitAssign(kSceVarMainStory, 65500);
            a.EmitLoadScene("q03", "manzhou");
            sce.procs[816] = a.BuildProc(816, "suoyaota_to_manzhou");
        }
        {
            // proc 817: Ch.21 Undersea City Dijiang Beast (`9016`) -> Obtain Water Spirit Pearl (`6406`) -> Ch.22 Bingfeng (`m23/bingfeng`)
            SceAssembler a;
            a.EmitDlg("帝江神兽：吼——！（击败帝江神兽，取得第五颗灵珠【水灵珠】，五灵珠齐聚！）");
            a.EmitCombatBoss({9016, 0, 0, 0, 0, 0});
            a.EmitGetCombat(0);
            a.EmitAddItem(6406, 1);
            a.EmitAssign(kSceVarMainStory, 72000);
            a.EmitLoadScene("m23", "bingfeng");
            sce.procs[817] = a.BuildProc(817, "haidicheng_water_pearl");
        }

        suite.m18_cpk = BuildPal3CpkArchive({
            {"m18.scn", SerializeScnFile(scn_m18), CpkCodec::lzo},
            {"shenmo.scn", SerializeScnFile(scn_shenmo), CpkCodec::lzo},
            {"shenjie.scn", SerializeScnFile(scn_shenjie), CpkCodec::lzo},
            {"guijie.scn", SerializeScnFile(scn_guijie), CpkCodec::lzo},
            {"rongyan.scn", SerializeScnFile(scn_rongyan), CpkCodec::lzo},
            {"haidicheng.scn", SerializeScnFile(scn_haidicheng), CpkCodec::lzo},
            {"m18.sce", SerializeSceFile(sce), CpkCodec::lzo},
            {"m18/m18.nav", SerializeNavFile(BuildDualLayerNavMesh(false)), CpkCodec::lzo},
            {"m18/m18.pol", SerializePolModel(BuildStagePolModel("m18_suoyaota")), CpkCodec::lzo},
            {"m18/_f01.cvd", SerializeCvdModel(BuildMechanismCvdModel("m18_chain")), CpkCodec::lzo},
        });
    }

    // ========================================================================
    // 5. m23.cpk (Act V: Ch.22 冰风谷滑雪与御剑 `bingfeng`, Ch.23上 剑冢外围 `jianzhong_outer`,
    //                    Ch.23下 剑冢铸剑厅 `m23`, Ch.24 新仙界重楼决战与五大结局 `xinxianjie`)
    // ========================================================================
    {
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

        const auto scn_bingfeng = MakeSimpleScn(
            "m23", "bingfeng", "m23",
            {
                {32, "冰风谷雪道向导", 180.0f, 0, 0.0f, -20.0f, 911, "c01", 0, {}, 215.0f},
            });

        const auto scn_jianzhong_outer = MakeSimpleScn(
            "m23", "jianzhong_outer", "m23",
            {
                {33, "剑冢外围·魔化罗如烈", 180.0f, 0, 0.0f, -25.0f, 912, "c01", 0, {}, 215.0f},
            });

        const auto scn_m23 = MakeSimpleScn(
            "m23", "m23", "m23",
            {
                {9, "邪剑仙与魔尊·重楼", 0.0f, 0, 0.0f, -35.0f, 902, "c01", 0, {}, 215.0f},
                {34, "剑冢铸剑厅·邪剑仙真身", 0.0f, 0, 30.0f, -35.0f, 913, "c01", 0, {}, 215.0f},
            },
            {furnace});

        const auto scn_xinxianjie = MakeSimpleScn(
            "m23", "xinxianjie", "m23",
            {
                {35, "新仙界·魔尊重楼", 180.0f, 0, 0.0f, -25.0f, 914, "c01", 0, {}, 215.0f},
            });

        SceFile sce;
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 80100);
            a.EmitDlg("紫萱：此处便是古姜国铸剑炉【剑冢】，需在【魔剑】与【镇妖剑】之间择一重铸！");
            sce.procs[1] = a.BuildProc(1, "m23_init");
        }
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 74100);
            a.EmitDlg("徐长卿：翻越雪岭镇冰风谷雪道，便可御剑飞抵雷州剑冢！");
            sce.procs[11] = a.BuildProc(11, "bingfeng_init");
        }
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 78100);
            a.EmitDlg("景天：魔化罗如烈正堵在剑冢外围入口，一鼓作气突破进去！");
            sce.procs[12] = a.BuildProc(12, "jianzhong_outer_init");
        }
        {
            SceAssembler a;
            a.EmitAssign(kSceVarMainStory, 92000);
            a.EmitDlg("重楼：飞蓬！邪剑仙既除，锁妖塔已封，来这【新仙界】与本座尽情一战吧！");
            sce.procs[13] = a.BuildProc(13, "xinxianjie_init");
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
            // proc 902: Fast-path Final Boss Battle against Evil Sword Immortal & Chonglou -> Grand Ending Movie
            SceAssembler a;
            a.EmitDlg("邪剑仙：六界唯我独尊！重楼：飞蓬，随本座入新仙界痛快一战！");
            a.EmitCombatBoss({9005, 9006, 0, 0, 0, 0});
            a.EmitGetCombat(0);
            a.EmitMovie("pal3_end_perfect.bik");
            a.EmitAssign(kSceVarMainStory, 100000);
            sce.procs[902] = a.BuildProc(902, "finale_boss");
        }
        {
            // proc 911: Ch.22 Bingfeng Skiing (`SKEE_Entry`) + `YuJian` flight to Ch.23a `m23/jianzhong_outer`
            SceAssembler a;
            a.EmitSkeeEntry(1);
            a.EmitAssign(kSceVarMainStory, 76000);
            a.EmitYuJian();
            sce.procs[911] = a.BuildProc(911, "bingfeng_ski_yujian");
        }
        {
            // proc 912: Ch.23a Sword Tomb Outer Boss (`9017`) -> Ch.23b `m23/m23`
            SceAssembler a;
            a.EmitDlg("魔化罗如烈：吼——！谁也别想进入剑冢核心！");
            a.EmitCombatBoss({9017, 0, 0, 0, 0, 0});
            a.EmitGetCombat(0);
            a.EmitAssign(kSceVarMainStory, 79500);
            a.EmitLoadScene("m23", "m23");
            sce.procs[912] = a.BuildProc(912, "jianzhong_outer_boss");
        }
        {
            // proc 913: Ch.23b Defeat Evil Sword Immortal (`9005`) -> Obtain Holy Spirit Pearl (`6403`) -> Ch.24 `m23/xinxianjie`
            SceAssembler a;
            a.EmitDlg("邪剑仙：不可能！你的圣灵神剑怎能斩碎本座的六界邪体！");
            a.EmitCombatBoss({9005, 0, 0, 0, 0, 0});
            a.EmitGetCombat(0);
            a.EmitAddItem(6403, 1);
            a.EmitAssign(kSceVarMainStory, 90000);
            a.EmitLoadScene("m23", "xinxianjie");
            sce.procs[913] = a.BuildProc(913, "jianzhong_xiejianxian_to_xinxianjie");
        }
        {
            // proc 914: Ch.24 New Immortal Realm Chonglou Final Duel (`9006`) -> Multi-Ending Movie
            SceAssembler a;
            a.EmitDlg("魔尊·重楼：痛快！这才是飞蓬真正的剑威！");
            a.EmitCombatBoss({9006, 0, 0, 0, 0, 0});
            a.EmitGetCombat(0);
            a.EmitMovie("pal3_end_campaign.bik");
            a.EmitAssign(kSceVarMainStory, 100000);
            sce.procs[914] = a.BuildProc(914, "xinxianjie_chonglou_finale");
        }

        suite.m23_cpk = BuildPal3CpkArchive({
            {"m23.scn", SerializeScnFile(scn_m23), CpkCodec::lzo},
            {"bingfeng.scn", SerializeScnFile(scn_bingfeng), CpkCodec::lzo},
            {"jianzhong_outer.scn", SerializeScnFile(scn_jianzhong_outer), CpkCodec::lzo},
            {"xinxianjie.scn", SerializeScnFile(scn_xinxianjie), CpkCodec::lzo},
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

    struct SubSceneSpec {
        std::string_view scn_id;
        std::string_view scn_file;
        std::uint32_t init_proc_id;
        std::string_view title;
    };

    auto load_multi_scene_cpk = [&](std::istream& stream,
                                    std::string_view cpk_id,
                                    std::string_view base_id,
                                    std::span<const SubSceneSpec> specs) {
        CpkPackageReader reader(stream);
        const auto sce = ParseSceFile(reader.Read(std::string(cpk_id) + ".sce"));
        const auto nav = ParseNavFile(
            reader.Read(std::string(base_id) + "/" + std::string(base_id) + ".nav"));
        const auto pol = ParsePolModel(
            reader.Read(std::string(base_id) + "/" + std::string(base_id) + ".pol"));
        const auto cvd = ParseCvdModel(reader.Read(std::string(base_id) + "/_f01.cvd"));

        for (const auto& sp : specs) {
            LoadedSceneBundle bundle;
            bundle.title = std::string(sp.title);
            bundle.init_proc_id = sp.init_proc_id;
            bundle.scn = ParseScnFile(reader.Read(sp.scn_file));
            bundle.sce = sce;
            bundle.nav = nav;
            bundle.pol = pol;
            bundle.cvd = cvd;
            scene_catalog[std::string(cpk_id) + "/" + std::string(sp.scn_id)] = std::move(bundle);
        }
    };

    const SubSceneSpec q01_specs[] = {
        {"yn01a", "q01.scn", 1, "渝州·永安当与竹林初遇"},
        {"binhua", "binhua.scn", 11, "第05章·宾化城永安当分号与长卿紫萱聚首"},
        {"tangjiabao", "tangjiabao.scn", 12, "第07章·唐家堡演武堂夺帅与雪见身世"},
        {"leizhou", "leizhou.scn", 13, "第13章·雷州刺史府平定云霆获雷灵珠"},
    };
    load_multi_scene_cpk(q01_cpk, "q01", "yn01", q01_specs);

    const SubSceneSpec m01_specs[] = {
        {"m01", "m01.scn", 1, "璧山双层机关栈道·九龙坡与大渡口"},
        {"m02", "m02.scn", 11, "第03章·九龙坡霹雳堂暗哨与小茅屋"},
        {"m03", "m03.scn", 12, "第04章·大渡口古墓龙葵破封入队"},
        {"m06", "m06.scn", 13, "第08章·德阳霹雳堂地牢诛罗如烈夺土灵珠"},
        {"m10", "m10.scn", 14, "第12章·九顶山定仙峰决战人面兽"},
    };
    load_multi_scene_cpk(m01_cpk, "m01", "m01", m01_specs);

    const SubSceneSpec q03_specs[] = {
        {"q03", "q03.scn", 1, "蓬莱码头·安宁村宿营与古藤林"},
        {"anning", "anning.scn", 11, "第09章·安宁村万仞孤峰篝火宿营夜话"},
        {"guteng", "guteng.scn", 12, "第10章·古藤林精精洞府合击破古藤仙人"},
        {"shushan", "shushan.scn", 13, "第11章·蜀山派无极阁五长老托付五灵"},
        {"manzhou", "manzhou.scn", 14, "第19-20章·蛮州女娲庙回魂仙梦与灵山青瓷鉴宝"},
    };
    load_multi_scene_cpk(q03_cpk, "q03", "q03", q03_specs);

    const SubSceneSpec m18_specs[] = {
        {"m18", "m18.scn", 1, "蜀山锁妖塔·冰风谷滑雪与御剑飞行"},
        {"shenmo", "shenmo.scn", 11, "第14章·神魔之井正反双层六界裂隙"},
        {"shenjie", "shenjie.scn", 12, "第15章·神界天庭夕瑶神树赐风灵珠"},
        {"guijie", "guijie.scn", 13, "第16章·鬼界黄泉路无常殿智破赵无延"},
        {"rongyan", "rongyan.scn", 14, "第17章·鬼界熔岩地狱诛火鬼王夺火灵珠"},
        {"haidicheng", "haidicheng.scn", 15, "第21章·东海海底城诛帝江神兽集齐水灵珠"},
    };
    load_multi_scene_cpk(m18_cpk, "m18", "m18", m18_specs);

    const SubSceneSpec m23_specs[] = {
        {"m23", "m23.scn", 1, "雷州剑冢铸剑抉择·新仙界重楼决战"},
        {"bingfeng", "bingfeng.scn", 11, "第22章·雪岭镇冰风谷极速滑雪与蜀山御剑"},
        {"jianzhong_outer", "jianzhong_outer.scn", 12, "第23章上·雷州剑冢外围诛魔化罗如烈"},
        {"xinxianjie", "xinxianjie.scn", 13, "第24章·新仙界重楼终极决战与五大结局"},
    };
    load_multi_scene_cpk(m23_cpk, "m23", "m23", m23_specs);
}

bool ChapterHost::MountOriginalGameDirectory(const std::filesystem::path& game_dir) {
    std::error_code ec;
    if (!std::filesystem::exists(game_dir, ec)) {
        return false;
    }
    original_game_dir = game_dir;

    // 1. Mount `basedata/basedata.cpk` (`init.sce` + real protagonist `ROLE/<code>/C01.MV3` morph models)
    std::optional<std::filesystem::path> base_cpk =
        FindFileCaseInsensitive(game_dir / "basedata", "basedata.cpk");
    if (!base_cpk) {
        base_cpk = FindFileCaseInsensitive(game_dir / "Basedata", "basedata.cpk");
    }
    if (!base_cpk) {
        base_cpk = FindFileCaseInsensitive(game_dir, "basedata.cpk");
    }
    if (base_cpk) {
        std::ifstream ifs(*base_cpk, std::ios::binary);
        if (ifs) {
            CpkPackageReader base_reader(ifs);
            if (auto init_bytes = base_reader.ReadIfPresent("init.sce")) {
                sce_vm->SetSystemScript(ParseSceFile(*init_bytes));
            }
            for (const char* code : {"101", "104", "105", "106", "107", "102", "109"}) {
                const std::string path = std::string("role/") + code + "/c01.mv3";
                if (auto bytes = base_reader.ReadIfPresent(path)) {
                    role_mv3_models[code] = ParseMv3Model(*bytes);
                }
            }
        }
    }

    // 2. Upgrade 3D .pol / .cvd models in pre-mounted campaign scenes from real `scene/*.cpk` archives
    struct RealArchiveModelMap {
        std::string_view cpk_prefix;
        std::string_view cpk_file;
        std::string_view pol_entry;
        std::string_view cvd_entry;
    };
    constexpr RealArchiveModelMap kArchiveModels[] = {
        {"q01/", "Q01.cpk", "yn01/yn01.pol", "yn01/_01.cvd"},
        {"m01/", "M01.cpk", "1/1.pol", "1/1.cvd"},
        {"q03/", "Q03.cpk", "q03/q03.pol", "q03/_01.cvd"},
        {"m18/", "M18.cpk", "1/1.pol", ""},
        {"m23/", "M23.cpk", "1/1.pol", ""},
    };
    for (const auto& spec : kArchiveModels) {
        auto cpk_path = FindFileCaseInsensitive(game_dir / "scene", spec.cpk_file);
        if (!cpk_path) cpk_path = FindFileCaseInsensitive(game_dir / "Scene", spec.cpk_file);
        if (!cpk_path) continue;
        std::ifstream ifs(*cpk_path, std::ios::binary);
        if (!ifs) continue;
        CpkPackageReader reader(ifs);
        std::optional<PolModel> real_pol;
        std::optional<CvdModel> real_cvd;
        if (!spec.pol_entry.empty()) {
            if (auto bytes = reader.ReadIfPresent(spec.pol_entry)) {
                real_pol = ParsePolModel(*bytes);
            }
        }
        if (!spec.cvd_entry.empty()) {
            if (auto bytes = reader.ReadIfPresent(spec.cvd_entry)) {
                real_cvd = ParseCvdModel(*bytes);
            }
        }
        for (auto& [key, bundle] : scene_catalog) {
            if (key.rfind(spec.cpk_prefix, 0) == 0) {
                if (real_pol) bundle.pol = *real_pol;
                if (real_cvd) bundle.cvd = *real_cvd;
            }
        }
    }

    // Refresh active scene geometry & GLES3 draw buffers if a scene is already loaded
    const std::string active_key = current_cpk + "/" + current_scn;
    auto it = scene_catalog.find(active_key);
    if (it != scene_catalog.end()) {
        active_pol = it->second.pol;
        active_cvd = it->second.cvd;
        displayed_world_scene = ConvertPolToPortableScene(active_pol);
        orbit_camera.Fit(displayed_world_scene);
        world_renderer.Upload(displayed_world_scene);
        RenderFrameToGles();
    }
    return true;
}

bool ChapterHost::TryLoadSceneFromOriginalGameDir(std::string_view cpk_name,
                                                  std::string_view scn_name) {
    if (original_game_dir.empty()) return false;
    const std::string cpk_filename = std::string(cpk_name) + ".cpk";
    std::optional<std::filesystem::path> cpk_path =
        FindFileCaseInsensitive(original_game_dir / "scene", cpk_filename);
    if (!cpk_path) {
        cpk_path = FindFileCaseInsensitive(original_game_dir / "Scene", cpk_filename);
    }
    if (!cpk_path) {
        cpk_path = FindFileCaseInsensitive(original_game_dir, cpk_filename);
    }
    if (!cpk_path) return false;

    std::ifstream ifs(*cpk_path, std::ios::binary);
    if (!ifs) return false;

    CpkPackageReader reader(ifs);
    auto scn_bytes = reader.ReadIfPresent(std::string(scn_name) + ".scn");
    if (!scn_bytes) {
        scn_bytes = reader.ReadIfPresent(std::string(cpk_name) + ".scn");
    }
    auto sce_bytes = reader.ReadIfPresent(std::string(cpk_name) + ".sce");
    if (!scn_bytes || !sce_bytes) return false;

    LoadedSceneBundle bundle;
    bundle.scn = ParseScnFile(*scn_bytes);
    bundle.sce = ParseSceFile(*sce_bytes);
    bundle.init_proc_id = bundle.sce.procs.count(1u) ? 1u : 0u;
    const std::string base =
        bundle.scn.scn_base_name.empty() ? std::string(scn_name) : bundle.scn.scn_base_name;
    bundle.title = std::string(cpk_name) + "/" + std::string(scn_name);

    if (auto nav_bytes = reader.ReadIfPresent(base + "/" + base + ".nav")) {
        bundle.nav = ParseNavFile(*nav_bytes);
    } else {
        bundle.nav = BuildDualLayerNavMesh(false);
    }
    if (auto pol_bytes = reader.ReadIfPresent(base + "/" + base + ".pol")) {
        bundle.pol = ParsePolModel(*pol_bytes);
    } else {
        bundle.pol = BuildStagePolModel(base);
    }
    std::optional<std::vector<std::uint8_t>> cvd_bytes = reader.ReadIfPresent(base + "/_f01.cvd");
    if (!cvd_bytes) cvd_bytes = reader.ReadIfPresent(base + "/_01.cvd");
    if (!cvd_bytes) cvd_bytes = reader.ReadIfPresent(base + "/" + base + ".cvd");
    if (!cvd_bytes) {
        for (const auto& node : bundle.scn.nodes) {
            if (node.name.size() > 4 &&
                (node.name.rfind(".cvd") != std::string::npos ||
                 node.name.rfind(".CVD") != std::string::npos)) {
                cvd_bytes = reader.ReadIfPresent(base + "/" + node.name);
                if (cvd_bytes) break;
            }
        }
    }
    if (cvd_bytes) {
        bundle.cvd = ParseCvdModel(*cvd_bytes);
    } else {
        bundle.cvd = BuildMechanismCvdModel(base + "_cvd");
    }

    scene_catalog[std::string(cpk_name) + "/" + std::string(scn_name)] = std::move(bundle);
    return true;
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
    if (std::find(demon_sword.infused_elements.begin(),
                  demon_sword.infused_elements.end(),
                  element) == demon_sword.infused_elements.end()) {
        demon_sword.purification_level++;
        demon_sword.spirit_atk_bonus += 30;
        demon_sword.infused_elements.push_back(element);
    }
    return "demon_sword_purified:" + std::string(Pal3ElementName(element));
}

std::string ChapterHost::EvaluateEndingName() const {
    const int xuejian_fav = favor.count(1) ? favor.at(1) : 0;
    const int longkui_fav = favor.count(2) ? favor.at(2) : 0;
    const int zixuan_fav = favor.count(3) ? favor.at(3) : 0;
    const int huaying_fav = favor.count(5) ? favor.at(5) : 0;

    if (zixuan_fav > xuejian_fav && zixuan_fav > longkui_fav && zixuan_fav > huaying_fav) {
        return "紫萱结局·忘情湖畔（重楼倾力相助，长卿紫萱相守）";
    }
    if (huaying_fav > xuejian_fav && huaying_fav > longkui_fav && huaying_fav > zixuan_fav) {
        return "花楹结局·五毒仙兽伴行九州";
    }
    if (demon_sword.equipped_divine_sword == "圣灵镇妖剑") {
        if (longkui_fav > xuejian_fav) {
            return "龙葵结局·千年剑灵共归渝州";
        }
        return "雪见结局·神树凝果江湖偕老";
    }
    if (longkui_fav > xuejian_fav) {
        return "龙葵结局·千年剑灵共归渝州";
    }
    return "完美结局·新仙界决战重楼（雪见相伴渝州）";
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
    } else if (item_id == 6402) {
        PurifyDemonSword(Pal3Element::wind);
    } else if (item_id == 6404) {
        PurifyDemonSword(Pal3Element::earth);
    } else if (item_id == 6405) {
        PurifyDemonSword(Pal3Element::fire);
    } else if (item_id == 6406) {
        PurifyDemonSword(Pal3Element::water);
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
        ending_name = EvaluateEndingName();
    }
}

void ChapterHost::ScePlayMusic(std::string_view bgm_name, std::int32_t /*loop*/) {
    active_bgm = std::string(bgm_name);
}

void ChapterHost::SceLoadScene(std::string_view cpk_name, std::string_view scn_name) {
    const std::string lower_cpk = ToLowerAscii(cpk_name);
    const std::string lower_scn = ToLowerAscii(scn_name);
    const std::string key = lower_cpk + "/" + lower_scn;
    auto it = scene_catalog.find(key);
    if (it == scene_catalog.end()) {
        if (TryLoadSceneFromOriginalGameDir(lower_cpk, lower_scn)) {
            it = scene_catalog.find(key);
        }
    }
    if (it == scene_catalog.end()) {
        throw std::runtime_error("unknown PAL3 scene in catalog: " + key);
    }

    current_cpk = lower_cpk;
    current_scn = lower_scn;
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

    sce_vm->SetSceneScript(lower_cpk, lower_scn, it->second.sce);
    sce_vm->CallProc(it->second.init_proc_id);
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

void ChapterHost::SceStartPawnshopMinigame(std::uint32_t shop_id) {
    minigames.StartPawnshop(shop_id);
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
