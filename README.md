# `agent-pal3` — 《仙剑奇侠传三》可移植 C++20 运行时与无头智能体评测宿主

`agent-pal3` 基于 [`OpenPAL3`](../OpenPAL3) 的《仙剑奇侠传三》二进制资产规范与 [`OpenPAL4`](../OpenPAL4) 的共享可移植运行时底座（`pal4_package_lzo`、`pal4_portable_gles`、`pal4_portable_scene`、`pal4_portable_movement`、`pal4::RandomNumberGenerator`），提供对标 [`agent-pal4`](../agent-pal4) 与 [`OpenGuJian`](../OpenGuJian) 的 **Stdin/Stdout JSON-Lines 无头智能体评测宿主 (`agent-pal3-host`)**。

---

## 核心模块架构

| 模块目标 | 头文件 / 源文件 | 功能说明 |
| :--- | :--- | :--- |
| `pal3_portable_cpk` | [`include/pal3/portable/cpk_package.h`](include/pal3/portable/cpk_package.h)<br>[`src/cpk_package.cpp`](src/cpk_package.cpp) | 实现 `gbEngine` `.cpk` 虚拟文件系统：128 字节 `RST\x1A` 头部、28 字节未加密 `CpkTableEntry`、转小写 CRC32 层级目录树（`father_crc`）、末尾 `extra_info` 原始文件名及 `raw`/`LZO1X` 解压缩。 |
| `pal3_portable_scn_nav_model` | [`include/pal3/portable/scn_nav_model.h`](include/pal3/portable/scn_nav_model.h)<br>[`src/scn_nav_model.cpp`](src/scn_nav_model.cpp) | 实现 `.scn`（820B `ScnRole` + 620B `ScnNode` 触发器/梯子/机关/AABB）、`.nav`（双层网格高度与 `distance_to_border` 权重 + 双层 A* 寻路器 `FindPal3NavPath`）、以及 `.pol`、`.cvd`、`.mv3` 模型解析与 `OpenPAL4` `Scene`/`CollisionMesh` 桥接。 |
| `pal3_portable_sce_vm` | [`include/pal3/portable/sce_vm.h`](include/pal3/portable/sce_vm.h)<br>[`src/sce_vm.cpp`](src/sce_vm.cpp) | 实现 `.sce`（`SCE\0`）字节码汇编器 `SceAssembler` 与虚拟机 `SceVm`，完整注册 `OpenPAL3` 全部 **112 条指令**、支持 GBK/UTF-8 自动转码（`libiconv`）、全局变量（`-32768` 主线进度 `g_story`、`-32767` 分支选择结果）、`FOP` 布尔累加器、串行/并行 `run_mode (1\|2)` 与 `SceProcHooks`（含 `501` 及 `1415` 钩子）。 |
| `pal3_portable_database_combat` | [`include/pal3/portable/database_combat.h`](include/pal3/portable/database_combat.h)<br>[`src/database_combat.cpp`](src/database_combat.cpp) | 实现六大主角（景天、唐雪见、龙葵[蓝/红双形态]、紫萱、徐长卿、花楹）、好感度系统、**五灵珠（雷/风/土/火/水）+ 圣灵珠全收集与魔剑六阶五灵净化**、半即时 ATB 行动槽 + **五灵太极阵法（6 阵位）**、合击技（【断空噬地】【飞鸟裂天】【灵奴奈落】）及全 17 位原作 Boss。 |
| `pal3_portable_minigames` | [`include/pal3/portable/minigames.h`](include/pal3/portable/minigames.h)<br>[`src/minigames.cpp`](src/minigames.cpp) | 实现《仙剑三》五大特色子系统：**古董真伪鉴定 (`APPREntry` 宜兴紫砂壶/越窑青瓷莲花碗)**、**永安当铺经营 (`PawnshopEntry`)**、**安宁村宿营夜话 (`ENCAMP_Entry`)**、**冰风谷极速滑雪 (`SKEE_Entry`)**、**蜀山御剑飞行大地图 (`YuJian`)**。 |
| `pal3_portable_chapter_host` | [`include/pal3/portable/chapter_host.h`](include/pal3/portable/chapter_host.h)<br>[`src/chapter_host.cpp`](src/chapter_host.cpp) | 统一宿主 `pal3::portable::ChapterHost`，内置 **原作完整 24 章（24 个子场景）全主线 `.cpk` 套件构建器** `BuildFullPal3CampaignCpkSuite()`、**原著五大结局判定**（完美结局/雪见结局/龙葵结局/紫萱结局/花楹结局）以及原作目录动态挂载器 `MountOriginalGameDirectory()`。 |
| `agent-pal3-host` | [`src/agent_pal3_host.cpp`](src/agent_pal3_host.cpp) | Stdin/Stdout JSON-Lines RPC 无头宿主，支持 `--mode normal`、`--mode pure`（防作弊盲测模式）、`--game-dir <path>`（或 `mount_game_dir` RPC 挂载原版光盘/Steam `.cpk` 目录）与毫秒级内存快照 `save_checkpoint` / `load_checkpoint`。 |

---

## 构建与测试验证

```bash
cmake -S . -B build
cmake --build build -j8
ctest --test-dir build --output-on-failure
npm run test:headless
```
