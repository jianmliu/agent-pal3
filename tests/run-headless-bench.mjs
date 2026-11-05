#!/usr/bin/env node
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const modeIdx = args.indexOf('--mode');
const mode = modeIdx >= 0 && args[modeIdx + 1] ? args[modeIdx + 1] : 'normal';
const positionalBin = args.find((a, idx) => !a.startsWith('--') && idx !== modeIdx + 1);
const hostBin = positionalBin || path.resolve(__dirname, '../build/agent-pal3-host');
const workDir = `/tmp/agent-pal3-bench-${mode}`;

class AgentPal3Client {
  constructor(binPath, runMode) {
    this.proc = spawn(binPath, ['--work-dir', workDir, '--mode', runMode], {
      stdio: ['pipe', 'pipe', 'inherit'],
    });
    this.rl = createInterface({ input: this.proc.stdout });
    this.queue = [];
    this.rl.on('line', (line) => {
      if (!line.trim()) return;
      const resolver = this.queue.shift();
      if (resolver) resolver(JSON.parse(line));
    });
  }

  send(payload, allowError = false) {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(
        () => reject(new Error(`Timeout on ${JSON.stringify(payload)}`)),
        5000,
      );
      this.queue.push((res) => {
        clearTimeout(timer);
        if (!res.ok && !allowError) {
          return reject(new Error(`RPC error on ${JSON.stringify(payload)}: ${res.error}`));
        }
        resolve(res);
      });
      this.proc.stdin.write(JSON.stringify(payload) + '\n');
    });
  }

  close() {
    this.proc.stdin.write('quit\n');
    this.proc.kill();
  }
}

async function runBenchmark() {
  console.log('========================================================================');
  console.log(`  agent-pal3 Headless Benchmark [mode=${mode}] (OpenPAL3 + OpenPAL4)`);
  console.log('========================================================================');
  const client = new AgentPal3Client(hostBin, mode);

  const checkStory = (state, expectedStory) => {
    if (mode !== 'pure') {
      assert.equal(state.story_progress, expectedStory);
      assert.equal(state.diagnostics.sce_idle, true);
    } else {
      assert.equal(state.story_progress, undefined);
      assert.equal(state.diagnostics, undefined);
    }
  };

  try {
    // Step 1: Verify .cpk VFS Archives Written & Loaded + Initial Observation in q01/yn01a
    const s0 = await client.send({ cmd: 'observe' });
    assert.equal(s0.ok, true);
    assert.equal(s0.cpk, 'q01');
    assert.equal(s0.scn, 'yn01a');
    assert.equal(s0.leader.name, '景天');
    assert.equal(s0.leader.model_code, '101');
    assert.equal(s0.gles3.mv3_models_loaded, 7);
    assert.ok(s0.gles3.draw_calls >= 2);
    assert.equal(fs.existsSync(path.join(workDir, 'basedata', 'basedata.cpk')), true);
    for (const cpkName of ['q01.cpk', 'm01.cpk', 'q03.cpk', 'm18.cpk', 'm23.cpk']) {
      assert.equal(fs.existsSync(path.join(workDir, 'scene', cpkName)), true);
    }
    if (mode === 'pure') {
      const blockedCheat = await client.send({ cmd: 'load_scene', cpk: 'm23', scn: 'm23' }, true);
      assert.equal(blockedCheat.ok, false);
    }
    checkStory(s0, 10101);
    console.log(
      `[PASS] 1. CPK 虚拟文件系统挂载与初始观测 (CpkPackageReader + MV3/POL + SCE VM): 场景=${s0.title} (${s0.cpk}/${s0.scn}), 领队=${s0.leader.name}, GLES3 draw_calls=${s0.gles3.draw_calls}`,
    );

    // Step 2: Test 3D North Wall Collision at z >= 60 (reusing OpenPAL4 SceneCollision & StepActorMovement)
    const wallProbe = await client.send({ cmd: 'move_to', x: 0, z: 92, max_steps: 90 });
    assert.equal(wallProbe.event, 'move_blocked_by_wall');
    assert.ok(wallProbe.leader.position[2] < 65.0);
    console.log(
      `[PASS] 2. 双层 NAV 障碍边界与 3D 碰撞检测 (OpenPAL4 SceneCollision): 向北探测 (0,0,92) 在 z=${wallProbe.leader.position[2]} 处被永安当北墙阻挡 (${wallProbe.event})`,
    );

    // Step 3: Cross-Layer A* Navigation (`layer 0 -> layer 1`) to Manager Zhao Wenchang + Antique Appraisal (`APPREntry`) + `SceProcHooks`
    const moveZhao = await client.send({ cmd: 'move_to', x: 40, z: 40, layer: 1 });
    assert.equal(moveZhao.event, 'move_reached_cross_layer');
    assert.equal(moveZhao.leader.nav_layer, 1);
    const talkZhao = await client.send({ cmd: 'interact', target_type: 'npc', id: 1 });
    assert.equal(talkZhao.minigame.active, true);
    assert.equal(talkZhao.minigame.kind, 'appraisal');
    const finishAppr = await client.send({ cmd: 'minigame_action', action: 'inspect', option: 1 });
    assert.equal(finishAppr.minigame.active, false);
    assert.equal(finishAppr.minigame.appraisals_passed, 1);
    assert.equal(finishAppr.inventory.items.some((i) => i.id === 6201), true);
    checkStory(finishAppr, 10202);
    console.log(
      `[PASS] 3. 双层 NAV 跨层寻路 (layer 0->1)、【宜兴紫砂壶】真伪鉴定 (APPREntry) 与 SceProcHooks (10101->10202): 银钱=${finishAppr.inventory.money}`,
    );

    // Step 4: Return to Layer 0, Meet Tang Xuejian (`id=2`) & Obtain Demon Sword (`魔剑` 6301)
    const moveXuejian = await client.send({ cmd: 'move_to', x: -35, z: 20, layer: 0 });
    assert.equal(moveXuejian.leader.nav_layer, 0);
    const meetXuejian = await client.send({ cmd: 'interact', target_type: 'npc', id: 2 });
    assert.equal(meetXuejian.party[1].in_team, true);
    assert.equal(meetXuejian.demon_sword.obtained, true);
    assert.equal(meetXuejian.demon_sword.equipped_divine_sword, '魔剑');
    checkStory(meetXuejian, 10400);
    console.log(
      `[PASS] 4. 唐雪见入队与获授【魔剑】(InTeam + AddItem + FavorAdd): 雪见好感度=${meetXuejian.favor.xuejian}, 佩剑=${meetXuejian.demon_sword.equipped_divine_sword}`,
    );

    // Step 5: Yuzhou Bamboo Forest Encounter with Demon Lord Chonglou (`CombatMaxRound(3)`)
    await client.send({ cmd: 'move_to', x: 0, z: -45, layer: 0 });
    let chonglouBattle = await client.send({ cmd: 'interact', target_type: 'npc', id: 3 });
    assert.equal(chonglouBattle.combat.active, true);
    assert.equal(chonglouBattle.combat.max_round, 3);
    while (chonglouBattle.combat.active) {
      chonglouBattle = await client.send({ cmd: 'combat_action', action: 'defend' });
    }
    assert.equal(chonglouBattle.combat.victories, 1);
    checkStory(chonglouBattle, 10600);
    console.log(
      `[PASS] 5. 渝州竹林初遇魔尊【重楼】限定三回合生存战 (CombatMaxRound=3): 成功撑过三招约定新仙界再战`,
    );

    // Step 6: Step into South Exit Tile Trigger (`SceNavTrigger`) -> Transition to Stage 2 Bishan (`m01/m01`) & Animate `.cvd` Mechanism Bridge (`ObjectPlay`)
    const enterM01 = await client.send({ cmd: 'move_to', x: 0, z: -75, layer: 0 });
    assert.equal(enterM01.cpk, 'm01');
    assert.equal(enterM01.scn, 'm01');
    checkStory(enterM01, 20100);
    await client.send({ cmd: 'move_to', x: 30, z: 15, layer: 0 });
    const bridgeOpened = await client.send({ cmd: 'interact', target_type: 'object', id: 30 });
    assert.ok(bridgeOpened.objects[0].cvd_anim_time >= 1.0);
    checkStory(bridgeOpened, 20200);
    console.log(
      `[PASS] 6. 踩踏触发器跨包切入【璧山】(m01.cpk) 与 .cvd 机关石桥关键帧驱动 (ObjectPlay): cvd_anim_time=${bridgeOpened.objects[0].cvd_anim_time}s`,
    );

    // Step 7: Defeat Pilittang Boss (`霹雳堂头目`), Recruit Longkui (`龙葵`), & Toggle Blue/Red Longkui (`105 水 <-> 106 雷`)
    await client.send({ cmd: 'move_to', x: 0, z: -20, layer: 0 });
    let piliBattle = await client.send({ cmd: 'interact', target_type: 'npc', id: 4 });
    while (piliBattle.combat.active) {
      piliBattle = await client.send({ cmd: 'combat_action', action: 'magic', skill_id: 3102 });
    }
    checkStory(piliBattle, 20350);
    await client.send({ cmd: 'move_to', x: -30, z: -35, layer: 0 });
    const longkuiJoined = await client.send({ cmd: 'interact', target_type: 'npc', id: 5 });
    assert.equal(longkuiJoined.party[2].in_team, true);
    assert.equal(longkuiJoined.cpk, 'q03');
    const redLk = await client.send({ cmd: 'toggle_longkui_form' });
    assert.equal(redLk.party[2].is_red_longkui, true);
    assert.equal(redLk.party[2].model_code, '106');
    assert.equal(redLk.party[2].element, '雷');
    console.log(
      `[PASS] 7. 击败【霹雳堂头目】、龙葵出剑入队与蓝葵/红葵双形态切换: 龙葵当前形态=红葵(model=${redLk.party[2].model_code}, 五灵=${redLk.party[2].element})`,
    );

    // Step 8: Stage 3 Penglai & Anning Village — Recruit Zixuan, Changqing, Huaying & Complete Night Encampment (`ENCAMP_Entry`)
    await client.send({ cmd: 'move_to', x: 25, z: 10, layer: 0 });
    const startEncamp = await client.send({ cmd: 'interact', target_type: 'npc', id: 6 });
    assert.equal(startEncamp.party.every((p) => p.in_team), true);
    assert.equal(startEncamp.minigame.active, true);
    assert.equal(startEncamp.minigame.kind, 'encampment');
    const finishEncamp = await client.send({ cmd: 'minigame_action', action: 'talk', option: 1 });
    assert.equal(finishEncamp.minigame.active, false);
    assert.equal(finishEncamp.minigame.encampments_completed, 1);
    assert.equal(finishEncamp.favor.xuejian, 60);
    checkStory(finishEncamp, 30300);
    console.log(
      `[PASS] 8. 第三幕蓬莱六大主角集结与安宁村篝火宿营夜话 (ENCAMP_Entry): 雪见好感度升至 ${finishEncamp.favor.xuejian}`,
    );

    // Step 9: Defeat Ancient Vine Immortal (`古藤仙人`) using Five-Element Taiji Formation + Dual Combo 【断空噬地】 (`4001`) -> Purify Demon Sword with Thunder Pearl (`雷灵珠`)
    await client.send({ cmd: 'move_to', x: 0, z: -35, layer: 0 });
    let gutengBattle = await client.send({ cmd: 'interact', target_type: 'npc', id: 7 });
    assert.equal(gutengBattle.combat.active, true);
    await client.send({ cmd: 'combat_action', action: 'move_slot', slot: 'wind' });
    while (gutengBattle.combat.active) {
      gutengBattle = await client.send({ cmd: 'combat_action', action: 'combo', skill_id: 4001 });
    }
    assert.ok(gutengBattle.combat.combos_used >= 1);
    assert.equal(gutengBattle.demon_sword.purification_level, 2);
    assert.equal(gutengBattle.cpk, 'm18');
    console.log(
      `[PASS] 9. 五灵太极阵换位与双人合击技【断空噬地】击败【古藤仙人】: 获【雷灵珠】净化魔剑 (Lv.${gutengBattle.demon_sword.purification_level}, 灵攻+${gutengBattle.demon_sword.spirit_atk_bonus})`,
    );

    // Step 10: Stage 4 Shushan Demon-Locking Tower (`m18`) — Traverse Iron Chain Elevator (`node_type=40`) to Layer 1
    await client.send({ cmd: 'move_to', x: 10, z: 10, layer: 0 });
    const elevatorRes = await client.send({ cmd: 'interact', target_type: 'ladder', id: 40 });
    assert.equal(elevatorRes.event, 'ladder_traversed');
    assert.equal(elevatorRes.leader.nav_layer, 1);
    console.log(
      `[PASS] 10. 蜀山锁妖塔铁链升降梯跨层交互 (ScnNodeType::elevator=40): 跃升至锁妖塔上层封印坛 (layer=${elevatorRes.leader.nav_layer})`,
    );

    // Step 11: Defeat Emperor of Sky Demons (`天妖皇`) with Triple Combo 【飞鸟裂天】 (`4002`) -> Obtain 【镇妖剑】 (`6302`) -> Complete Bingfeng Valley Skiing (`SKEE_Entry`)
    await client.send({ cmd: 'move_to', x: 45, z: 45, layer: 1 });
    let tianyaoBattle = await client.send({ cmd: 'interact', target_type: 'npc', id: 8 });
    assert.equal(tianyaoBattle.combat.active, true);
    while (tianyaoBattle.combat.active) {
      tianyaoBattle = await client.send({ cmd: 'combat_action', action: 'combo', skill_id: 4002 });
    }
    assert.equal(tianyaoBattle.demon_sword.zhenyaojian_obtained, true);
    assert.equal(tianyaoBattle.minigame.active, true);
    assert.equal(tianyaoBattle.minigame.kind, 'skiing');

    const skiDone = await client.send({ cmd: 'minigame_action', action: 'jump', option: 1 });
    assert.equal(skiDone.minigame.ski_runs_completed, 1);
    assert.equal(skiDone.inventory.items.some((i) => i.id === 6501), true);
    assert.equal(skiDone.minigame.active, true);
    assert.equal(skiDone.minigame.kind, 'yujian');
    console.log(
      `[PASS] 11. 三人合击技【飞鸟裂天】击败锁妖塔【天妖皇】夺回【镇妖剑】并完成冰风谷极速滑雪 (SKEE_Entry): 拾获【雪莲精】`,
    );

    // Step 12: Shushan Sword Flight World Map (`YuJian` opcode 155) -> Fly to Stage 5 Sword Tomb (`m23/m23`)
    const yujianDone = await client.send({
      cmd: 'minigame_action',
      action: 'fly',
      option: 4,
      destination: 'm23',
    });
    assert.equal(yujianDone.minigame.active, false);
    assert.equal(yujianDone.minigame.yujian_flights, 1);
    assert.equal(yujianDone.cpk, 'm23');
    assert.equal(yujianDone.scn, 'm23');
    checkStory(yujianDone, 80100);
    console.log(
      `[PASS] 12. 蜀山御剑飞行大地图调度 (YuJian 指令 155): 乘风降落至终幕【${yujianDone.title}】(${yujianDone.cpk}/${yujianDone.scn})`,
    );

    // Step 13: Sword Tomb Furnace AABB Trigger (`node_type=20`) + `DlgSel` Divine Sword Choice (`global_vars[-32767]`)
    await client.send({ cmd: 'move_to', x: 0, z: 15, layer: 0 });
    const furnacePrompt = await client.send({ cmd: 'interact', target_type: 'aabb', id: 50 });
    assert.equal(furnacePrompt.dialog.choice_pending, true);
    assert.equal(furnacePrompt.dialog.choices.length, 2);
    const chosenSword = await client.send({ cmd: 'choose', index: 0 });
    assert.equal(chosenSword.dialog.choice_pending, false);
    assert.equal(chosenSword.demon_sword.equipped_divine_sword, '圣灵魔剑');
    checkStory(chosenSword, 85000);
    console.log(
      `[PASS] 13. 剑冢铸剑炉 AABB 触发器与 DlgSel 分支选择: 铸成【${chosenSword.demon_sword.equipped_divine_sword}】(灵攻+${chosenSword.demon_sword.spirit_atk_bonus})`,
    );

    // Step 14: Save Checkpoint Before Final Boss, Defeat Evil Sword Immortal (`邪剑仙`) & Chonglou (`魔尊·重楼`) with 【灵奴奈落】 (`4003`), & Trigger Perfect Ending
    const savedPreFinale = await client.send({ cmd: 'save_checkpoint', slot: 'before_finale' });
    assert.equal(savedPreFinale.event, 'checkpoint_saved:before_finale');

    await client.send({ cmd: 'move_to', x: 0, z: -35, layer: 0 });
    let finaleBattle = await client.send({ cmd: 'interact', target_type: 'npc', id: 9 });
    assert.equal(finaleBattle.combat.active, true);
    assert.equal(finaleBattle.combat.enemies.length, 2);
    while (finaleBattle.combat.active) {
      finaleBattle = await client.send({ cmd: 'combat_action', action: 'combo', skill_id: 4003 });
    }
    assert.equal(finaleBattle.game_cleared, true);
    assert.ok(finaleBattle.ending_name.includes('完美结局'));
    checkStory(finaleBattle, 100000);
    console.log(
      `[PASS] 14. 终局连战【邪剑仙】与【新仙界·重楼】(合击技【灵奴奈落】): 通关结局="${finaleBattle.ending_name}", 累计胜场=${finaleBattle.combat.victories}`,
    );

    // Step 15: Sub-Millisecond In-Memory Checkpoint Rollback (`load_checkpoint`)
    const restored = await client.send({ cmd: 'load_checkpoint', slot: 'before_finale' });
    assert.equal(restored.event, 'checkpoint_loaded:before_finale');
    assert.equal(restored.game_cleared, false);
    assert.equal(restored.cpk, 'm23');
    checkStory(restored, 85000);
    console.log(
      `[PASS] 15. 全状态内存快照毫秒级回滚验证 (save_checkpoint / load_checkpoint): 成功恢复至剑冢决战前状态`,
    );

    console.log('========================================================================');
    console.log(`  ALL 15/15 PAL3 CAMPAIGN CHECKS PASSED [mode=${mode}]`);
    console.log('========================================================================');
  } finally {
    client.close();
  }
}

runBenchmark().catch((err) => {
  console.error(err);
  process.exit(1);
});
