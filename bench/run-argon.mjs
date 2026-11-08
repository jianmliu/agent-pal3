#!/usr/bin/env node
/**
 * run-argon.mjs — 使用 agy-cli 驱动 agent-pal3-host (仙剑奇侠传三 OpenPAL3 + OpenPAL4 C++20 无头引擎) 进行 24 章全流程评测
 */
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import readline from 'node:readline';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT_DIR = path.resolve(__dirname, '..');
const HOST_BIN = process.env.AGENT_PAL3_HOST_BIN || path.join(ROOT_DIR, 'build', 'agent-pal3-host');
const AGY_BIN = process.env.BENCH_AGY_BIN || path.join(os.homedir(), '.local', 'bin', 'agy');

function parseCliModel() {
  const idx = process.argv.indexOf('--model');
  if (idx >= 0 && process.argv[idx + 1]) {
    return process.argv[idx + 1];
  }
  return process.env.BENCH_MODEL || 'claude-opus-5-5-high';
}
const MODEL = parseCliModel();

function parseCliMode() {
  const idx = process.argv.indexOf('--mode');
  if (idx >= 0 && process.argv[idx + 1]) {
    return process.argv[idx + 1].toLowerCase() === 'pure' ? 'pure' : 'normal';
  }
  return (process.env.BENCH_MODE || 'normal').toLowerCase() === 'pure' ? 'pure' : 'normal';
}
const MODE = parseCliMode();

class HostBridge {
  constructor(binPath, workDir, mode) {
    const args = ['--work-dir', workDir, '--mode', mode, '--full-campaign'];
    if (!process.argv.includes('--no-multi-scn-dungeon')) {
      args.push('--multi-scn-dungeon');
    }
    if (!process.argv.includes('--no-complete-116')) {
      args.push('--complete-116');
    }
    this.proc = spawn(binPath, args, {
      stdio: ['pipe', 'pipe', 'inherit'],
    });
    this.rl = readline.createInterface({ input: this.proc.stdout });
    this.queue = [];
    this.rl.on('line', (line) => {
      if (!line.trim()) return;
      const resolver = this.queue.shift();
      if (resolver) resolver(JSON.parse(line));
    });
  }

  rpc(payload) {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(
        () => reject(new Error(`Host RPC timeout: ${JSON.stringify(payload)}`)),
        30000
      );
      this.queue.push((res) => {
        clearTimeout(timer);
        resolve(res);
      });
      this.proc.stdin.write(JSON.stringify(payload) + '\n');
    });
  }

  close() {
    try {
      this.proc.stdin.write('quit\n');
      this.proc.kill();
    } catch {}
  }
}

const TOOLS = [
  {
    name: 'observe',
    description:
      'Observe current 3D scene (cpk, scn, title), leader (name, nav_layer, position), party, favor, demon_sword, inventory, dialog, npcs, objects, combat, and minigame state.',
    inputSchema: { type: 'object', properties: {} },
  },
  {
    name: 'move_to',
    description:
      'Navigate leader in dual-layer 3D world space toward (x, z) on optional nav `layer` (0 or 1) using A* pathfinding and 3D collision detection. Stepping onto a tile_trigger at the destination automatically fires its scene transition.',
    inputSchema: {
      type: 'object',
      properties: {
        x: { type: 'number', description: 'Target X coordinate' },
        z: { type: 'number', description: 'Target Z coordinate' },
        layer: { type: 'number', description: 'Optional target navigation layer (0 or 1)' },
        max_steps: { type: 'number', description: 'Max simulation frames (default 160)' },
      },
      required: ['x', 'z'],
    },
  },
  {
    name: 'interact',
    description:
      'Interact with an NPC or scene object/mechanism/ladder/elevator/aabb within 18 units on the same nav_layer. Always `move_to` the target position and layer first.',
    inputSchema: {
      type: 'object',
      properties: {
        target_type: {
          type: 'string',
          description: '"npc", "object", "ladder", "elevator", or "aabb"',
        },
        id: { type: 'number', description: 'Target entity ID from `npcs` or `objects`' },
      },
      required: ['id'],
    },
  },
  {
    name: 'choose',
    description:
      'Submit a dialog branch selection when `dialog.choice_pending` is true (e.g. index 0 or 1 at the Sword Tomb furnace).',
    inputSchema: {
      type: 'object',
      properties: {
        index: { type: 'number', description: '0-based choice index' },
      },
      required: ['index'],
    },
  },
  {
    name: 'combat_action',
    description:
      'Execute a combat turn when `combat.active` is true. `action` can be "combo" (Five-Element combined skill, e.g. skill_id 4001/4002/4003), "magic" (five-element spell, e.g. skill_id 3001/3102), "attack", "defend" (recommended for survival battles with max_round > 0), or "move_slot" (reposition on Taiji formation: slot="wind"|"earth"|"thunder"|"water"|"fire"|"center").',
    inputSchema: {
      type: 'object',
      properties: {
        action: {
          type: 'string',
          description: '"combo", "magic", "attack", "defend", "move_slot", or "item"',
        },
        skill_id: {
          type: 'number',
          description: 'Skill ID from `combat.available_skills` (e.g. 3102, 4001, 4002, 4003)',
        },
        slot: {
          type: 'string',
          description: 'Taiji slot name when action="move_slot" ("wind", "earth", "thunder", "water", "fire", "center")',
        },
      },
      required: ['action'],
    },
  },
  {
    name: 'minigame_action',
    description:
      'Execute an action when `minigame.active` is true (`appraisal`, `pawnshop`, `encampment`, `skiing`, or `yujian`). Inspect `minigame.prompt` and `minigame.options` to pick the appropriate `action`, `option`, or `destination`.',
    inputSchema: {
      type: 'object',
      properties: {
        action: {
          type: 'string',
          description: '"inspect" (appraisal), "deal" (pawnshop), "talk" (encampment), "jump" (skiing), or "fly" (yujian)',
        },
        option: {
          type: 'number',
          description: 'Numeric option index from `minigame.options` (e.g. 1 or 2)',
        },
        destination: {
          type: 'string',
          description: 'Destination scene code when `minigame.kind === "yujian"` (e.g. "m23/jianzhong_outer")',
        },
      },
      required: ['action'],
    },
  },
  {
    name: 'toggle_longkui_form',
    description:
      'Toggle Longkui between Blue Longkui (105 Water) and Red Longkui (106 Thunder, +25 ATK) once Longkui is in the party.',
    inputSchema: { type: 'object', properties: {} },
  },
  {
    name: 'switch_leader',
    description: 'Switch the controlled party leader by `role_id` (0=景天, 1=唐雪见, 2=龙葵, 3=紫萱, 4=徐长卿, 5=花楹).',
    inputSchema: {
      type: 'object',
      properties: {
        role_id: { type: 'number', description: 'Role ID (0..5)' },
      },
      required: ['role_id'],
    },
  },
  {
    name: 'save_checkpoint',
    description: 'Save an instant in-memory checkpoint of the current scene, party, inventory, and SCE VM state.',
    inputSchema: {
      type: 'object',
      properties: {
        slot: { type: 'string', description: 'Checkpoint slot name' },
      },
      required: ['slot'],
    },
  },
  {
    name: 'load_checkpoint',
    description: 'Restore a previously saved in-memory checkpoint by slot name.',
    inputSchema: {
      type: 'object',
      properties: {
        slot: { type: 'string', description: 'Checkpoint slot name' },
      },
      required: ['slot'],
    },
  },
];

async function main() {
  if (!fs.existsSync(HOST_BIN)) {
    throw new Error(`Missing host binary: ${HOST_BIN}`);
  }
  if (!fs.existsSync(AGY_BIN)) {
    throw new Error(`Missing agy CLI binary: ${AGY_BIN}`);
  }

  const stamp = new Date().toISOString().replace(/[-:]/g, '').replace(/\..*/, '');
  const prefix = MODEL.includes('opus')
    ? 'opus'
    : MODEL.includes('sonnet')
      ? 'sonnet'
      : MODEL.includes('flash')
        ? 'flash'
        : 'argon';
  const outDir = path.join(ROOT_DIR, 'bench', 'out', `${prefix}-${MODE}-${stamp}`);
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'agent-pal3-run-'));
  fs.mkdirSync(outDir, { recursive: true });

  const host = new HostBridge(HOST_BIN, path.join(tmpDir, 'host-work'), MODE);
  const initialObs = await host.rpc({ cmd: 'observe' });

  const traceFile = path.join(outDir, 'episode.jsonl');
  const clientLogFile = path.join(outDir, 'client.jsonl');
  const resultFile = path.join(outDir, 'result.json');
  const traceStream = fs.createWriteStream(traceFile, { flags: 'a' });
  traceStream.write(
    JSON.stringify({
      type: 'start',
      timestamp: new Date().toISOString(),
      model: `agy-cli:${MODEL}`,
      mode: MODE,
      fullCampaign: true,
      initial: initialObs,
    }) + '\n'
  );

  let toolCalls = 0;
  let callsInCurrentTurn = 0;
  const MAX_PURE_CALLS_PER_TURN = 3;
  let clearedState = null;
  const visitedScenes = new Set([`${initialObs.cpk}/${initialObs.scn}`]);

  const purgeUnauthorizedScripts = () => {
    if (MODE !== 'pure') return [];
    const removed = [];
    try {
      for (const ent of fs.readdirSync(tmpDir, { withFileTypes: true })) {
        if (ent.name === 'pal3bench.mjs' || ent.name === 'host-work') continue;
        if (ent.isFile() && /\.(mjs|cjs|js|sh|py|rb|pl)$/i.test(ent.name)) {
          fs.unlinkSync(path.join(tmpDir, ent.name));
          removed.push(ent.name);
        }
      }
    } catch {}
    return removed;
  };

  const server = http.createServer(async (req, res) => {
    let body = '';
    for await (const chunk of req) body += chunk;
    const msg = JSON.parse(body || '{}');
    if (msg.method === 'tools/list') {
      res.writeHead(200, { 'content-type': 'application/json' });
      res.end(JSON.stringify({ jsonrpc: '2.0', id: msg.id, result: { tools: TOOLS } }));
      return;
    }
    if (msg.method === 'tools/call') {
      const { name, arguments: args = {} } = msg.params || {};
      const removedScripts = purgeUnauthorizedScripts();
      if (MODE === 'pure' && removedScripts.length > 0) {
        res.writeHead(200, { 'content-type': 'application/json' });
        res.end(
          JSON.stringify({
            jsonrpc: '2.0',
            id: msg.id,
            result: {
              content: [
                {
                  type: 'text',
                  text: JSON.stringify({
                    ok: false,
                    error: `pure_mode_violation: external automation scripts (${removedScripts.join(', ')}) are forbidden in pure mode and have been removed. Issue commands directly via pal3bench.mjs step-by-step.`,
                  }),
                },
              ],
            },
          })
        );
        return;
      }
      if (MODE === 'pure' && name !== 'observe') {
        callsInCurrentTurn++;
        if (callsInCurrentTurn > MAX_PURE_CALLS_PER_TURN) {
          res.writeHead(200, { 'content-type': 'application/json' });
          res.end(
            JSON.stringify({
              jsonrpc: '2.0',
              id: msg.id,
              result: {
                content: [
                  {
                    type: 'text',
                    text: JSON.stringify({
                      ok: false,
                      error: `pure_mode_turn_limit: at most ${MAX_PURE_CALLS_PER_TURN} action commands are allowed per agent reasoning turn in pure mode. Do not batch or loop commands; inspect the current observation and decide your next step in the next turn.`,
                    }),
                  },
                ],
              },
            })
          );
          return;
        }
      }
      toolCalls++;
      const payload = { cmd: name, ...args };
      const state = await host.rpc(payload);
      if (state.cpk && state.scn) {
        visitedScenes.add(`${state.cpk}/${state.scn}`);
      }
      traceStream.write(JSON.stringify({ type: 'call', step: toolCalls, tool: name, args, state }) + '\n');
      console.log(
        `[Step ${toolCalls}] ${name}(${JSON.stringify(args)}) -> scene=${state.cpk}/${state.scn} (${visitedScenes.size}/116) event=${state.event} leader=${state.leader?.name}@L${state.leader?.nav_layer}${JSON.stringify(state.leader?.position)} sword=${state.demon_sword?.equipped_divine_sword}(Lv.${state.demon_sword?.purification_level}) combat=${state.combat?.active ? `round_${state.combat.round}` : `idle(v=${state.combat?.victories ?? 0})`} minigame=${state.minigame?.active ? state.minigame.kind : 'none'} cleared=${state.game_cleared}`
      );
      if (state.game_cleared && !clearedState) {
        clearedState = state;
      }
      res.writeHead(200, { 'content-type': 'application/json' });
      res.end(
        JSON.stringify({
          jsonrpc: '2.0',
          id: msg.id,
          result: {
            content: [{ type: 'text', text: JSON.stringify(state, null, 2) }],
          },
        })
      );
      return;
    }
    res.writeHead(400);
    res.end();
  });

  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const { port } = server.address();
  const url = `http://127.0.0.1:${port}`;

  const helperFile = path.join(tmpDir, 'pal3bench.mjs');
  fs.writeFileSync(
    helperFile,
    `#!/usr/bin/env node
const [,, name, rawArgs] = process.argv;
const args = rawArgs ? JSON.parse(rawArgs) : {};
const r = await fetch(${JSON.stringify(url)}, {
  method: 'POST',
  headers: { 'content-type': 'application/json' },
  body: JSON.stringify({
    jsonrpc: '2.0',
    id: 1,
    method: name === '--list' ? 'tools/list' : 'tools/call',
    params: name === '--list' ? {} : { name, arguments: args }
  })
});
const j = await r.json();
if (name === '--list') console.log(JSON.stringify(j.result?.tools || [], null, 2));
else console.log((j.result?.content || []).map(c => c.text || '').join('\\n'));
`,
    { mode: 0o755 }
  );

  const toolsDoc = TOOLS.map(
    (t) =>
      `- \`node ${helperFile} ${t.name} '${JSON.stringify(
        Object.fromEntries(Object.keys(t.inputSchema.properties || {}).map((k) => [k, `<${k}>`]))
      )}'\`: ${t.description}`
  ).join('\n');

  const normalGuide = [
    `You are playing Chinese Paladin 3 (仙剑奇侠传三 - OpenPAL3 + OpenPAL4 C++20 Headless Runtime) in NORMAL (Play-and-Fix) mode across the complete 1:1 116-Scene Campaign (17 retail Towns Q01-Q17 + 22 retail Dungeons M01-M26 with all 99 sub-floors):`,
    `- IMPORTANT MULTI-FLOOR TRANSIT RULE: Whenever \`dialog.history\` states \`前方尚有 N 层连续迷宫通道，直接 move_to (0, 42) 即可自动跨层\`, each \`move_to\` to \`{"x":0,"z":42}\` automatically steps onto the exit \`tile_trigger\` and advances 1 floor (no \`interact\` needed). You can chain up to \`min(N, 6)\` sequential \`node ${helperFile} move_to '{"x":0,"z":42}'\` calls in a single shell command to traverse those N corridor floors rapidly!`,
    `1. Ch.1 Yuzhou (q01/yn01a): Move to (40, 40, layer: 1) & interact with 赵文昌 (id=1), complete appraisal (\`{"action":"inspect","option":1}\`). Move to (-35, 20, layer: 0) & interact with 唐雪见 (id=2) to recruit her & obtain 【魔剑】. Move to (0, -45, layer: 0) & interact with 魔尊·重楼 (id=3), use \`combat_action\` (\`{"action":"defend"}\`) for 3 rounds until combat ends. Then move to south exit (0, -75, layer: 0) to enter Ch.2 Bishan (m01/m01).`,
    `2. Ch.2 Bishan (m01/m01 -> m01_2 -> m01_3 -> m02): Move to (30, 15, layer: 0) & interact with mechanism bridge \`_f01.cvd\` (target_type="object", id=30). Move to (0, -20, layer: 0) & interact with 霹雳堂头目 (id=4), defeat him with \`combat_action\` (\`{"action":"combo","skill_id":4001}\`). Move to (35, -35, layer: 0) & interact with 九龙坡栈道引路人 (id=15), then traverse 2 transit floors (\`move_to(0, 42)\` × 2) to reach Ch.3 Jiulongpo (m01/m02).`,
    `3. Ch.3-5 (m01/m02 -> [3 transit floors] -> m01/m03 -> [2 transit floors] -> q01/binhua -> [8 transit floors] -> q03/q03): In m01/m02 move to (0, -25) & defeat 霹雳堂坛主 (id=16), then \`move_to(0, 42)\` × 3 to m01/m03. In m01/m03 move to (0, -25) & interact with 剑灵龙葵 (id=17), then \`move_to(0, 42)\` × 2 to q01/binhua. In q01/binhua move to (20, 15) & interact with 宾化永安当掌柜 (id=11), complete pawnshop (\`{"action":"deal","option":1}\`), then move to (-25, -20) & interact with 徐长卿与紫萱 (id=12), then \`move_to(0, 42)\` × 8 (Q17 ship + M05 sea 1-3 + M06 Penglai 1-4) to reach Ch.6 Penglai (q03/q03).`,
    `4. Ch.6-13 (q03/q03 -> [1 transit] -> q01/tangjiabao -> [3 transit] -> m01/m06 -> q03/anning -> [Guteng 5-SCN: guteng_1 -> guteng_2 -> [2 transit] -> guteng] -> [2 transit] -> q03/shushan -> [4 transit] -> m01/m10 -> q01/leizhou): Defeat 邪剑仙幻影 (id=20 at -25, 10), \`move_to(0, 42)\` × 1 (Zhenjiang) to q01/tangjiabao. Defeat 唐泰 (id=13 at 0, -25), \`move_to(0, 42)\` × 3 (Deyang + Pilittang 1-2) to m01/m06. Defeat 罗如烈 (id=18 at 0, -25 for 土灵珠) to q03/anning; complete Anning Encampment (id=21 at 0, -20 with \`{"action":"talk","option":1}\`) to q03/guteng_1. In guteng_1 align wind stele (id=61 at -25, 15) then take portal (id=63 at 30, -25) to guteng_2; defeat 精精 (id=64 at -20, 10) then open vine barrier (id=65 at 25, -30), then \`move_to(0, 42)\` × 2 (guteng_3, guteng_4) to q03/guteng. Defeat 古藤仙人 (id=22 at 0, -25), \`move_to(0, 42)\` × 2 (Gudao 1-2) to q03/shushan. Talk to 清微道长 (id=23 at 0, -20), \`move_to(0, 42)\` × 4 (Jiuding 1a-3) to m01/m10; defeat 人面兽 (id=19 at 0, -25) to q01/leizhou; defeat 云霆 (id=14 at 0, -25 for 雷灵珠).`,
    `5. Ch.14-21 ([Shenmo 10-SCN: shenmo_a <-> shenmo_a1 -> [7 transit] -> shenmo] -> [6 transit Shenshu] -> m18/shenjie -> [1 transit Fengdu] -> m18/guijie -> [13 transit Huangquan 1-9 + Rongyan 1-4] -> m18/rongyan -> [Suoyaota 10-Floor: [4 transit 10F-7F] -> suoyaota_upper -> [2 transit 5F-4F] -> suoyaota_mid -> [1 transit 2F] -> m18/m18] -> q03/manzhou -> [12 transit Caohai/Lingshan/Gucheng/Anxi/Haidicheng 1-4] -> m18/haidicheng): In shenmo_a flip to reverse side (id=71 at -25, 15), in shenmo_a1 activate reverse Taiji switch (id=73 at 0, -25) & flip back (id=74 at -25, 15), in shenmo_a open Taiji seal gate (id=72 at 25, -30), then \`move_to(0, 42)\` × 7 to m18/shenmo & defeat 神魔守卫 (id=26 at 0, -25). Then \`move_to(0, 42)\` × 6 (Shenshu 1-6) to m18/shenjie; talk to 夕瑶 (id=27 at 0, -20 for 风灵珠), \`move_to(0, 42)\` × 1 (Fengdu) to m18/guijie; defeat 赵无延 (id=28 at 0, -25), \`move_to(0, 42)\` × 13 (Huangquan 1-9 + Rongyan 1-4) to m18/rongyan; defeat 火鬼王 (id=29 at 0, -25 for 火灵珠). Then \`move_to(0, 42)\` × 4 (Suoyaota 10F-7F) to suoyaota_upper; activate floating bridge (id=81 at -25, 15) & take portal (id=82 at 25, -30), \`move_to(0, 42)\` × 2 (5F-4F) to suoyaota_mid; talk to 冰晶女 (id=83 at -20, 10) & take array (id=84 at 25, -30), \`move_to(0, 42)\` × 1 (2F) to m18/m18; take elevator (id=40 at 10, 10) to layer 1 & defeat 天妖皇真身 (id=30 at -35, 45 for 镇妖剑) to q03/manzhou. Complete Manzhou appraisal (id=24 at 20, 10) & 银眉妖狐 (id=25 at -20, -25), \`move_to(0, 42)\` × 12 to m18/haidicheng, and defeat 帝江神兽 (id=31 at 0, -25 for 水灵珠).`,
    `6. Ch.22-24 ([6 transit Xueling + Bingfeng 1-5] -> m23/bingfeng -> m23/jianzhong_outer -> [3 transit Jianzhong 2-4] -> m23/jianzhong_hall -> m23/m23 -> [3 transit Xingsen + Xinxianjie 1-2] -> m23/xinxianjie): From haidicheng victory, \`move_to(0, 42)\` × 6 to m23/bingfeng; interact with 向导 (id=32 at 0, -20), complete skiing (\`{"action":"jump","option":1}\`) and YuJian flight (\`{"action":"fly","destination":"m23/jianzhong_outer"}\`). In jianzhong_outer defeat 魔化罗如烈 (id=33 at 0, -25), \`move_to(0, 42)\` × 3 to jianzhong_hall. In jianzhong_hall climb to layer 1 (move_to 40, 40, layer: 1) & activate Dragon Vein switch (id=91), return to layer 0 (move_to 0, -30, layer: 0) & cross lava drawbridge (id=92) into m23/m23. In m23/m23 interact with \`sword_tomb_furnace\` (target_type="aabb", id=50 at 0, 15), call \`choose\` (\`{"index":0}\`), defeat 邪剑仙真身 (id=34 at 30, -35), \`move_to(0, 42)\` × 3 (Xingsen + Xinxianjie 1-2) to m23/xinxianjie, and defeat 新仙界·魔尊重楼 (id=35 at 0, -25) until \`"game_cleared": true\`.`,
  ];

  const pureGuide = [
    `You are playing Chinese Paladin 3 (仙剑奇侠传三 - OpenPAL3 + OpenPAL4 C++20 Headless Runtime) in PURE (Zero-Prior / Zero-Walkthrough) mode across the complete 116-scene campaign.`,
    `No external walkthrough, internal story progress IDs, or debug scene warps are available. You must explore and clear the game purely from in-game observations:`,
    `- Inspect \`title\`, \`dialog.history\`, \`npcs\`, \`objects\`, \`party\`, \`demon_sword\`, \`inventory\`, \`combat\`, and \`minigame\` in each scene.`,
    `- Always navigate to within 18 units of an NPC or object on the same \`layer\` using \`move_to\` (\`{"x": <x>, "z": <z>, "layer": <layer>}\`) before calling \`interact\` (\`{"target_type": "<npc|object|ladder|elevator|aabb>", "id": <id>}\`). Calling \`interact\` from > 18 units away or from a different \`nav_layer\` will fail.`,
    `- Stepping onto a \`tile_trigger\` object via \`move_to\` automatically triggers its scene transition once the current scene's story objectives are complete.`,
    `- MULTI-FLOOR CORRIDOR TRANSIT: When \`dialog.history\` states \`前方尚有 N 层连续迷宫通道，直接 move_to (0, 42) 即可自动跨层\`, stepping to \`{"x": 0, "z": 42}\` via \`move_to\` automatically transitions to the next floor without \`interact\`. You may chain up to \`min(N, ${MAX_PURE_CALLS_PER_TURN})\` sequential \`move_to '{"x":0,"z":42}'\` calls in a single turn (using \`&&\`) to traverse consecutive corridor floors efficiently while respecting the ${MAX_PURE_CALLS_PER_TURN}-call-per-turn limit.`,
    `- When \`minigame.active\` is true, read \`minigame.prompt\` and \`minigame.options\` and call \`minigame_action\` with the matching \`action\`, \`option\`, or \`destination\`.`,
    `- When \`dialog.choice_pending\` is true, read \`dialog.choices\` and call \`choose\` with \`{"index": <0|1>}\`.`,
    `- When \`combat.active\` is true, inspect \`combat.enemies\`, \`combat.max_round\`, and \`combat.available_skills\`, and call \`combat_action\` (e.g., \`{"action": "defend"}\` for survival duels where \`max_round > 0\`, or \`{"action": "combo", "skill_id": <id>}\` / \`{"action": "magic", "skill_id": <id>}\`) until \`combat.active\` becomes false.`,
    `- STRICT PURE MODE FAIRNESS RULES: Do NOT write any helper/driver scripts (\`.mjs\`, \`.js\`, \`.sh\`, \`.py\`) or shell loops. At most ${MAX_PURE_CALLS_PER_TURN} action commands are permitted per agent reasoning turn.`,
  ];

  console.log('================================================================');
  console.log(`  Launching agy-cli:${MODEL} [mode=${MODE}] on Agent-PAL3 24-Chapter Campaign`);
  console.log(`  Output Directory: ${outDir}`);
  console.log('================================================================');

  const clientLog = fs.createWriteStream(clientLogFile, { flags: 'a' });
  let agentTurns = 0;
  let finalResult = null;
  const t0 = Date.now();
  const maxSessions = MODE === 'pure' ? 6 : 3;

  for (let session = 1; session <= maxSessions && !clearedState; session++) {
    callsInCurrentTurn = 0;
    const curObs = session === 1 ? initialObs : await host.rpc({ cmd: 'observe' });
    if (curObs.game_cleared) {
      clearedState = curObs;
      break;
    }
    const prompt = [
      ...(MODE === 'pure' ? pureGuide : normalGuide),
      ``,
      `### Tool Execution Interface`,
      `Call game tools ONLY by executing \`node ${helperFile} <tool_name> '<json_args>'\` via your shell/command tool. Do NOT read or access any files outside \`${tmpDir}\`, and do NOT create any script files in \`${tmpDir}\`.`,
      MODE === 'pure'
        ? `Strict Turn Policy: Call at most ${MAX_PURE_CALLS_PER_TURN} tools per turn (e.g. chaining up to ${MAX_PURE_CALLS_PER_TURN} \`move_to '{"x":0,"z":42}'\` calls when traversing multi-floor corridors, or \`move_to\` + \`interact\`), inspect the JSON observation, and reason step-by-step.`
        : `Tip: You can chain 3-6 sequential tool calls in a single shell command (using &&) when traversing multi-floor corridors or executing known sequences.`,
      `Available tools:`,
      toolsDoc,
      ``,
      `### Current Observation (Session ${session}, Step ${toolCalls})`,
      '```json',
      JSON.stringify(curObs, null, 2),
      '```',
      ``,
      `Continue playing from the current state until \`"game_cleared": true\` is returned, then summarize your playthrough in 3-4 sentences.`,
    ].join('\n');

    const agyLog = path.join(outDir, `agy-s${session}.log`);
    const agyArgs = [
      '-p',
      prompt,
      '--output-format',
      'stream-json',
      '--disable-slash-commands',
      '--dangerously-skip-permissions',
      '--print-timeout',
      '600s',
      '--model',
      MODEL,
      '--log-file',
      agyLog,
    ];

    console.log(`--- Starting Agent Session ${session}/${maxSessions} (steps so far: ${toolCalls}) ---`);
    const agy = spawn(AGY_BIN, agyArgs, {
      cwd: tmpDir,
      env: {
        ...process.env,
        PATH: `/opt/homebrew/bin:/Users/jianmingliu/.local/bin:${process.env.PATH || ''}`,
      },
      stdio: ['ignore', 'pipe', 'inherit'],
    });

    const rl = readline.createInterface({ input: agy.stdout });
    rl.on('line', (line) => {
      clientLog.write(line + '\n');
      try {
        const ev = JSON.parse(line);
        if (
          ev.event === 'step_update' &&
          ev.step_update?.state === 'DONE' &&
          ev.step_update?.step_type === 'agent_response'
        ) {
          agentTurns++;
          callsInCurrentTurn = 0;
        }
        if (ev.event === 'result' && ev.result) {
          finalResult = ev.result;
        }
      } catch {}
    });

    await new Promise((resolve) => agy.on('close', resolve));
  }
  const durationMs = Date.now() - t0;

  const finalObs = await host.rpc({ cmd: 'observe' });
  const summary = {
    ok: Boolean(clearedState || finalObs.game_cleared),
    model: `agy-cli:${MODEL}`,
    mode: MODE,
    fullCampaign: true,
    multiScnDungeon: Boolean(finalObs.gles3?.multi_scn_dungeon),
    complete116Campaign: Boolean(finalObs.gles3?.complete_116_campaign),
    toolCalls,
    agentTurns: agentTurns || finalResult?.num_turns || 0,
    durationMs,
    scenesVisited: visitedScenes.size,
    cpk: finalObs.cpk,
    scn: finalObs.scn,
    title: finalObs.title,
    gameCleared: finalObs.game_cleared,
    endingName: finalObs.ending_name,
    demonSword: finalObs.demon_sword,
    favor: finalObs.favor,
    combat: finalObs.combat,
    minigame: finalObs.minigame,
    leader: finalObs.leader,
    party: finalObs.party,
    inventory: finalObs.inventory,
    gles3: finalObs.gles3,
    usage: finalResult?.usage || null,
  };

  fs.writeFileSync(resultFile, JSON.stringify(summary, null, 2));
  traceStream.write(JSON.stringify({ type: 'end', summary }) + '\n');
  traceStream.end();
  clientLog.end();
  host.close();
  server.close();

  console.log('================================================================');
  console.log('  Agent-PAL3 Playthrough Summary:');
  console.log(JSON.stringify(summary, null, 2));
  console.log('================================================================');

  if (!summary.gameCleared) {
    process.exit(1);
  }
}

main().catch((err) => {
  console.error('[FATAL]', err);
  process.exit(1);
});
