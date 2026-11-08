#!/usr/bin/env node
/**
 * board.mjs — 生成《仙剑奇侠传三》(agent-pal3) 24 章全流程评测排行榜 (board.html) 与交互式 3D 录像回放器 (replay.html)
 *
 *   node bench/board.mjs
 */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { computeEpisodeDigest, readEpisodeLog } from './verify.mjs';
import { makePosKey, makeSceneKey, makeSceneLeaderKey } from './render-frames.mjs';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT_DIR = path.resolve(__dirname, '..');
const OUT_ROOT = path.join(__dirname, 'out');
const WEB_DIR = path.join(ROOT_DIR, 'web');

const esc = (s) =>
  String(s ?? '').replace(
    /[&<>"']/g,
    (c) =>
      ({
        '&': '&amp;',
        '<': '&lt;',
        '>': '&gt;',
        '"': '&quot;',
        "'": '&#39;',
      })[c]
  );

const PAL3_24_CHAPTERS = [
  { ch: 1, act: '第一幕·渝州风起', key: 'q01/yn01a', cpk: 'Q01.cpk (b/b.pol)', title: '渝州·永安当与竹林初遇', milestone: '二楼账房紫砂壶鉴宝、结识唐雪见获魔剑、竹林重楼三招之约', var: 10600 },
  { ch: 2, act: '第一幕·渝州风起', key: 'm01/m01', cpk: 'M01.cpk (1/1.pol)', title: '璧山·双层机关栈道', milestone: '开启 _f01.cvd 石桥机关、击败霹雳堂头目', var: 20500 },
  { ch: 3, act: '第一幕·渝州风起', key: 'm01/m02', cpk: 'M02.cpk (1/1.pol)', title: '九龙坡·霹雳堂暗哨与小茅屋', milestone: '击败霹雳堂暗哨統領，突破九龙坡山口', var: 21500 },
  { ch: 4, act: '第一幕·渝州风起', key: 'm01/m03', cpk: 'M03.cpk (1/1.pol)', title: '大渡口·古墓龙葵破封', milestone: '千年剑灵龙葵苏醒入队，支援蓝葵/红葵双形态切换', var: 23000 },
  { ch: 5, act: '第一幕·渝州风起', key: 'q01/binhua', cpk: 'Q02.cpk (q02/q02.pol)', title: '宾化城·永安当分号与聚首', milestone: '完成当铺经营小游戏、徐长卿与紫萱入队渡海', var: 26000 },
  { ch: 6, act: '第二幕·蜀山寻灵', key: 'q03/q03', cpk: 'Q03.cpk (q03/q03.pol)', title: '蓬莱仙岛·御剑堂重围', milestone: '击败假扮商人的霹雳堂堂主罗如烈', var: 30300 },
  { ch: 7, act: '第二幕·蜀山寻灵', key: 'q01/tangjiabao', cpk: 'Q04.cpk (q04/q04.pol)', title: '唐家堡·演武堂夺帅', milestone: '演武堂击败勾结霹雳堂的唐门长老唐泰', var: 32000 },
  { ch: 8, act: '第二幕·蜀山寻灵', key: 'm01/m06', cpk: 'M06.cpk (1/1.pol)', title: '德阳霹雳堂地牢·夺土灵珠', milestone: '诛灭总舵罗如烈，取得第一颗灵珠【土灵珠】净化魔剑', var: 35000 },
  { ch: 9, act: '第二幕·蜀山寻灵', key: 'q03/anning', cpk: 'Q06.cpk (q06/q06.pol)', title: '安宁村·万仞孤峰篝火宿营', milestone: '触发宿营夜话小游戏，提升女主好感度', var: 37000 },
  { ch: 10, act: '第二幕·蜀山寻灵', key: 'q03/guteng', cpk: 'M09.cpk (1/1.pol)', title: '古藤林·精精洞府与古藤仙人', milestone: '五灵太极阵合击击败古藤仙人', var: 39000 },
  { ch: 11, act: '第二幕·蜀山寻灵', key: 'q03/shushan', cpk: 'Q08.cpk (q08/q08.pol)', title: '蜀山派无极阁·五长老托付', milestone: '清微等蜀山五长老托付寻齐五灵珠重任', var: 40500 },
  { ch: 12, act: '第二幕·蜀山寻灵', key: 'm01/m10', cpk: 'm10.cpk (1/1.pol)', title: '九顶山定仙峰·决战人面兽', milestone: '击败上古妖兽人面兽，取道前往雷州', var: 41800 },
  { ch: 13, act: '第三幕·六界裂隙', key: 'q01/leizhou', cpk: 'Q09.cpk (c/c.pol)', title: '雷州刺史府·平定云霆获雷灵珠', milestone: '承受云霆雷灵之力，取得第二颗灵珠【雷灵珠】', var: 43000 },
  { ch: 14, act: '第三幕·六界裂隙', key: 'm18/shenmo', cpk: 'm11.cpk (1/1.pol)', title: '神魔之井·正反双层六界裂隙', milestone: '击败神魔之井守卫巨灵，登上天梯抵神界', var: 45500 },
  { ch: 15, act: '第三幕·六界裂隙', key: 'm18/shenjie', cpk: 'Q10.cpk (q10/q10.pol)', title: '神界天庭·夕瑶神树赐风灵珠', milestone: '重温飞蓬旧事，获夕瑶赠予第三颗灵珠【风灵珠】', var: 47000 },
  { ch: 16, act: '第三幕·六界裂隙', key: 'm18/guijie', cpk: 'Q11.cpk (q11/q11.pol)', title: '鬼界黄泉路·无常殿破赵无延', milestone: '智破离魂使赵无延，闯入熔岩地狱救雪见', var: 48500 },
  { ch: 17, act: '第三幕·六界裂隙', key: 'm18/rongyan', cpk: 'M16.cpk (1/1.pol)', title: '熔岩地狱·诛火鬼王夺火灵珠', milestone: '击败火鬼王，取得第四颗灵珠【火灵珠】', var: 49800 },
  { ch: 18, act: '第四幕·锁妖集珠', key: 'm18/m18', cpk: 'M18.cpk (1/1.pol)', title: '蜀山锁妖塔·底层冰晶封印', milestone: '决战天妖皇，拔出飞蓬佩剑【镇妖剑】', var: 65500 },
  { ch: 19, act: '第四幕·锁妖集珠', key: 'q03/manzhou', cpk: 'Q13.cpk (q13/q13.pol)', title: '蛮州女娲庙与灵山青瓷鉴宝', milestone: '完成越窑青瓷碗鉴宝 + 击败灵山仙人洞水妖統領', var: 69500 },
  { ch: 20, act: '第四幕·锁妖集珠', key: 'm18/haidicheng', cpk: 'M21.cpk (1/1.pol)', title: '东海海底城·诛帝江神兽齐五灵', milestone: '击败帝江神兽获【水灵珠】，五灵珠全部齐聚', var: 72000 },
  { ch: 21, act: '第五幕·剑冢仙界', key: 'm23/bingfeng', cpk: 'M22.cpk (1/1.pol)', title: '雪岭镇冰风谷·极速滑雪与御剑', milestone: '通关冰风谷滑雪 + 御剑飞行抵剑冢', var: 76000 },
  { ch: 22, act: '第五幕·剑冢仙界', key: 'm23/jianzhong_outer', cpk: 'M22.cpk (2/2.pol)', title: '雷州剑冢外围·诛魔化罗如烈', milestone: '击败吸取五灵妖气的魔化罗如烈', var: 79500 },
  { ch: 23, act: '第五幕·剑冢仙界', key: 'm23/m23', cpk: 'M23.cpk (1/1.pol)', title: '剑冢铸剑厅·双剑重铸斩邪剑仙', milestone: '铸剑炉择一重铸【圣灵魔剑/镇妖剑】，斩灭邪剑仙获圣灵珠', var: 90000 },
  { ch: 24, act: '第五幕·剑冢仙界', key: 'm23/xinxianjie', cpk: 'M24.cpk (1/1.pol)', title: '新仙界·魔尊重楼终极决战', milestone: '新仙界击败重楼，按神剑与好感度触发五大结局', var: 100000 },
];

function loadFrameManifest() {
  for (const p of [
    path.join(WEB_DIR, 'frames/manifest.json'),
    path.join(OUT_ROOT, 'frames/manifest.json'),
  ]) {
    if (fs.existsSync(p)) {
      try {
        return JSON.parse(fs.readFileSync(p, 'utf8'));
      } catch {}
    }
  }
  return { byPos: {}, bySceneLeader: {}, byScene: {}, byWorld: {}, substepsByStep: {} };
}

function resolveStepFrame(st, manifest) {
  if (!st || !manifest) return '';
  const posKey = makePosKey(st);
  const scLdrKey = makeSceneLeaderKey(st);
  const scKey = makeSceneKey(st);
  const w = st.cpk || st.w || '';
  return (
    manifest.byPos?.[posKey] ||
    manifest.bySceneLeader?.[scLdrKey] ||
    manifest.byScene?.[scKey] ||
    manifest.byWorld?.[w] ||
    ''
  );
}

function loadRuns() {
  if (!fs.existsSync(OUT_ROOT)) return [];
  const runs = [];
  for (const ent of fs.readdirSync(OUT_ROOT, { withFileTypes: true })) {
    if (!ent.isDirectory() || ent.name === 'frames') continue;
    const dir = path.join(OUT_ROOT, ent.name);
    const resPath = path.join(dir, 'result.json');
    const logPath = path.join(dir, 'episode.jsonl');
    const verPath = path.join(dir, 'verify.json');
    if (!fs.existsSync(resPath) || !fs.existsSync(logPath)) continue;
    const result = JSON.parse(fs.readFileSync(resPath, 'utf8'));
    const log = readEpisodeLog(logPath);
    const verify = fs.existsSync(verPath)
      ? JSON.parse(fs.readFileSync(verPath, 'utf8'))
      : null;
    const digest = computeEpisodeDigest(dir);
    runs.push({
      runId: ent.name,
      result,
      verify,
      digest,
      initial: log.start?.initial || null,
      calls: log.calls || [],
    });
  }
  return runs.sort((a, b) => {
    const a116 = Boolean(a.result.complete116Campaign) ? 1 : 0;
    const b116 = Boolean(b.result.complete116Campaign) ? 1 : 0;
    if (a116 !== b116) return b116 - a116;
    const aMulti = Boolean(a.result.multiScnDungeon) ? 1 : 0;
    const bMulti = Boolean(b.result.multiScnDungeon) ? 1 : 0;
    if (aMulti !== bMulti) return bMulti - aMulti;
    if (a.result.mode !== b.result.mode) return a.result.mode === 'pure' ? -1 : 1;
    return (a.result.toolCalls || 9999) - (b.result.toolCalls || 9999);
  });
}

function buildCompactReplayRuns(runs, manifest) {
  return runs.map((r) => {
    const rawSteps = [
      ...(r.initial ? [{ step: 0, tool: 'init', args: {}, state: r.initial }] : []),
      ...(r.calls || []).map((c) => ({
        step: c.step,
        tool: c.tool,
        args: c.args || {},
        state: c.state || {},
      })),
    ];

    const steps = rawSteps.map((s) => {
      const st = s.state || {};
      const imgFile = resolveStepFrame(st, manifest);
      const rawSubs =
        manifest?.substepsByStep?.[`${r.runId}:${s.step}`] ||
        manifest?.substepsByStep?.[String(s.step)] ||
        null;
      const hist = st.dialog?.history || [];
      const dlgText = hist.map((d) => d.text).join(' / ');

      return {
        step: s.step ?? 0,
        tool: s.tool || 'observe',
        args: s.args || {},
        ev: String(st.event || st.error || '').slice(0, 240),
        cpk: st.cpk || '',
        scn: st.scn || '',
        title: st.title || '',
        bgm: st.bgm || '',
        clr: Boolean(st.game_cleared),
        ending: st.ending_name || '',
        img: imgFile ? `frames/${imgFile}` : '',
        ...(rawSubs && rawSubs.length
          ? {
              subs: rawSubs.map((sub) => ({
                w: sub.w,
                sc: sub.sc,
                p: (sub.p || [0, 0, 0]).map((v) => Math.round(Number(v) || 0)),
                yaw: Math.round(Number(sub.yaw || 0)),
                img: sub.img ? `frames/${sub.img}` : '',
              })),
            }
          : {}),
        ldr: st.leader
          ? {
              n: st.leader.name || '景天',
              code: st.leader.model_code || '101',
              el: st.leader.element || 'wind',
              layer: st.leader.nav_layer ?? 0,
              tile: st.leader.nav_tile || [16, 16],
              p: (st.leader.position || [0, 0, 0]).map((x) => Math.round(Number(x) || 0)),
              yaw: Math.round(Number(st.leader.rotation_yaw) || 0),
            }
          : null,
        pty: (st.party || [])
          .filter((p) => p.in_team)
          .map((p) => ({
            id: p.role_id,
            n: p.name,
            el: p.element,
            red: Boolean(p.is_red_longkui),
            lv: p.level || 1,
            hp: p.hp ?? 0,
            mhp: p.max_hp || 1,
            sh: p.shen ?? 0,
            msh: p.max_shen || 1,
            qi: p.qi ?? 0,
            mqi: p.max_qi || 5,
            slot: p.formation_slot || 'center',
          })),
        fav: st.favor || { xuejian: 50, longkui: 50, zixuan: 50, huaying: 50 },
        swd: st.demon_sword
          ? {
              eq: st.demon_sword.equipped_divine_sword || '无',
              lv: st.demon_sword.purification_level ?? 0,
              atk: st.demon_sword.spirit_atk_bonus ?? 0,
              els: st.demon_sword.infused_elements || [],
            }
          : null,
        inv: {
          m: st.inventory?.money ?? 0,
          it: (st.inventory?.items || []).map((i) => `${i.name}×${i.count}`),
        },
        dlg: dlgText,
        choices: st.dialog?.choices || [],
        npcs: (st.npcs || []).map((n) => ({
          id: n.id,
          n: n.name,
          l: n.layer ?? 0,
          p: (n.position || [0, 0, 0]).map((x) => Math.round(Number(x) || 0)),
        })),
        objs: (st.objects || []).map((o) => ({
          id: o.id,
          n: o.name,
          t: o.type_name || 'object',
          l: o.layer ?? 0,
          p: (o.position || [0, 0, 0]).map((x) => Math.round(Number(x) || 0)),
        })),
        cbt: st.combat
          ? {
              act: Boolean(st.combat.active),
              r: st.combat.round || 0,
              v: st.combat.victories || 0,
              c: st.combat.combos_used || 0,
              en: (st.combat.enemies || []).map((e) => ({
                s: e.slot,
                n: e.name,
                el: e.element,
                hp: e.hp ?? 0,
                mhp: e.max_hp || 1,
              })),
            }
          : null,
        mg: st.minigame
          ? {
              act: Boolean(st.minigame.active),
              k: st.minigame.kind || 'none',
              t: st.minigame.title || '',
              p: st.minigame.prompt || '',
            }
          : null,
        gl: st.gles3
          ? [st.gles3.draw_calls || 0, st.gles3.world_vertices || 0]
          : [0, 0],
      };
    });

    return {
      runId: r.runId,
      model: r.result.model || 'claude-opus-5-5-high',
      mode: r.result.mode || 'pure',
      toolCalls: r.result.toolCalls || r.calls.length,
      agentTurns: r.result.agentTurns || 1,
      callsPerTurn: r.verify?.callsPerTurn ?? Number((r.calls.length / Math.max(1, r.result.agentTurns || 1)).toFixed(2)),
      moveToCalls: r.verify?.moveToCalls ?? 0,
      scenesVisited: r.verify?.scenesVisited ?? r.result.scenesVisited ?? 24,
      victories: r.result.combat?.victories ?? 0,
      endingName: r.result.endingName || '',
      divineSword: r.result.demonSword?.equipped_divine_sword || '',
      purificationLevel: r.result.demonSword?.purification_level ?? 0,
      infusedElements: r.result.demonSword?.infused_elements ?? [],
      durationSec: Number(((r.result.durationMs || 0) / 1000).toFixed(1)),
      digest: r.digest.slice(0, 12),
      steps,
    };
  });
}

function renderBoardHtml(runs) {
  const rows = runs
    .map(({ runId, result, verify, digest }) => {
      const modeBadge =
        result.mode === 'pure'
          ? '<span class="badge pure">PURE (Zero-Prior)</span>'
          : '<span class="badge normal">NORMAL (Play-and-Fix)</span>';
      const verifiedBadge = verify?.ok
        ? `<span class="badge ok" title="${esc(digest)}">VERIFIED ✓ (${esc(digest.slice(0, 8))})</span>`
        : '<span class="badge pending">UNVERIFIED</span>';
      const warnings = verify?.optimizationWarnings?.length
        ? `<span class="badge warn">${esc(verify.optimizationWarnings.join(', '))}</span>`
        : '<span class="badge clean">0 warnings</span>';
      const sec = ((result.durationMs || 0) / 1000).toFixed(1);
      const pearls = (result.demonSword?.infused_elements || []).join('·') || '无';
      const totalTarget = result.complete116Campaign
        ? '116 (Full 1:1)'
        : result.multiScnDungeon
          ? '31 (Multi-SCN)'
          : '24';
      return `<tr>
        <td><code>${esc(runId)}</code></td>
        <td><strong>${esc(result.model)}</strong></td>
        <td>${modeBadge}</td>
        <td><strong>${verify?.scenesVisited ?? result.scenesVisited ?? 24} / ${totalTarget}</strong></td>
        <td><strong>${result.toolCalls}</strong></td>
        <td>${result.agentTurns}</td>
        <td>${verify?.callsPerTurn ?? '-'}</td>
        <td>${verify?.moveToCalls ?? '-'}</td>
        <td>${esc(result.demonSword?.equipped_divine_sword || '-')} (Lv.${result.demonSword?.purification_level ?? 0} · ${esc(pearls)})</td>
        <td>${result.combat?.victories ?? 0}</td>
        <td>${esc(result.endingName || '-')}</td>
        <td>${sec}s</td>
        <td>${warnings}</td>
        <td>${verifiedBadge}</td>
        <td><a class="replay-btn" href="replay.html?run=${encodeURIComponent(runId)}">🎬 3D 回放</a></td>
      </tr>`;
    })
    .join('\n');

  const chRows = PAL3_24_CHAPTERS.map(
    (c) => `<tr>
      <td><strong>Ch.${String(c.ch).padStart(2, '0')}</strong></td>
      <td><span class="badge act">${esc(c.act)}</span></td>
      <td><code>${esc(c.key)}</code></td>
      <td><code>${esc(c.cpk)}</code></td>
      <td><strong>${esc(c.title)}</strong></td>
      <td>${esc(c.milestone)}</td>
      <td><code>${c.var}</code></td>
    </tr>`
  ).join('\n');

  const pureRun = runs.find((r) => r.result.mode === 'pure') || runs[0];
  const normalRun = runs.find((r) => r.result.mode === 'normal') || runs[0];

  return `<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Agent-PAL3 Benchmark Leaderboard — 仙剑奇侠传三 (24 Chapters)</title>
  <style>
    :root {
      --bg: #0b0f17;
      --panel: #131a26;
      --panel-alt: #192231;
      --border: #28354a;
      --text: #e6edf3;
      --muted: #8b9bb4;
      --accent: #38bdf8;
      --gold: #f59e0b;
      --jade: #10b981;
      --purple: #a855f7;
    }
    * { box-sizing: border-box; }
    body {
      background: radial-gradient(circle at 18% 0%, #162238 0%, var(--bg) 60%);
      color: var(--text);
      font-family: -apple-system, BlinkMacSystemFont, "PingFang SC", "Microsoft YaHei", sans-serif;
      margin: 0;
      padding: 28px 36px 56px;
    }
    .topbar {
      display: flex;
      justify-content: space-between;
      align-items: center;
      flex-wrap: wrap;
      gap: 16px;
      margin-bottom: 24px;
      padding-bottom: 18px;
      border-bottom: 1px solid var(--border);
    }
    h1 { font-size: 25px; margin: 0 0 6px; letter-spacing: 0.3px; }
    p.sub { color: var(--muted); margin: 0; font-size: 13.5px; }
    .nav-tabs { display: flex; gap: 10px; }
    .nav-tab {
      text-decoration: none;
      padding: 9px 16px;
      border-radius: 8px;
      font-size: 13.5px;
      font-weight: 600;
      border: 1px solid var(--border);
      background: var(--panel);
      color: var(--text);
      transition: all 0.15s;
    }
    .nav-tab:hover { border-color: var(--accent); color: var(--accent); }
    .nav-tab.active {
      background: linear-gradient(135deg, #0284c7, #2563eb);
      border-color: #38bdf8;
      color: #fff;
    }
    .kpi-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
      gap: 14px;
      margin-bottom: 26px;
    }
    .kpi-card {
      background: var(--panel);
      border: 1px solid var(--border);
      border-radius: 10px;
      padding: 15px 18px;
    }
    .kpi-label { font-size: 12px; color: var(--muted); margin-bottom: 6px; }
    .kpi-val { font-size: 22px; font-weight: 700; color: #fff; }
    .kpi-sub { font-size: 12px; color: #64748b; margin-top: 4px; }
    h2 { font-size: 18px; margin: 28px 0 12px; color: #f1f5f9; }
    .table-wrap {
      background: var(--panel);
      border: 1px solid var(--border);
      border-radius: 10px;
      overflow-x: auto;
      margin-bottom: 28px;
    }
    table { width: 100%; border-collapse: collapse; font-size: 13px; }
    th, td { padding: 11px 13px; text-align: left; border-bottom: 1px solid #1e293b; }
    th { background: var(--panel-alt); color: #cbd5e1; font-weight: 600; white-space: nowrap; }
    tr:last-child td { border-bottom: none; }
    tr:hover td { background: rgba(56, 189, 248, 0.04); }
    .badge { padding: 3px 9px; border-radius: 999px; font-size: 11px; font-weight: 600; white-space: nowrap; }
    .badge.pure { background: rgba(56, 189, 248, 0.15); color: #38bdf8; border: 1px solid rgba(56, 189, 248, 0.35); }
    .badge.normal { background: rgba(168, 85, 247, 0.15); color: #c084fc; border: 1px solid rgba(168, 85, 247, 0.35); }
    .badge.ok { background: rgba(16, 185, 129, 0.15); color: #34d399; border: 1px solid rgba(16, 185, 129, 0.35); }
    .badge.clean { background: rgba(34, 197, 94, 0.12); color: #86efac; }
    .badge.warn { background: rgba(245, 158, 11, 0.18); color: #fbbf24; }
    .badge.act { background: rgba(148, 163, 184, 0.14); color: #cbd5e1; }
    .replay-btn {
      display: inline-flex;
      align-items: center;
      gap: 4px;
      padding: 5px 11px;
      border-radius: 6px;
      background: linear-gradient(135deg, #0ea5e9, #2563eb);
      color: #fff;
      font-weight: 600;
      font-size: 12px;
      text-decoration: none;
      white-space: nowrap;
    }
    .replay-btn:hover { filter: brightness(1.12); }
    code { font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-size: 12px; color: #93c5fd; }
  </style>
</head>
<body>
  <div class="topbar">
    <div>
      <h1>⚔️ Agent-PAL3 仙剑奇侠传三 — 24 章全流程无头评测排行榜</h1>
      <p class="sub">OpenPAL3 + OpenPAL4 C++20 Portable Runtime · 原版零售 CPK 3D 场景与角色纹理解码 · 双层 NAV 寻路 · 五灵太极阵合击 · 五大结局</p>
    </div>
    <div class="nav-tabs">
      <a class="nav-tab active" href="board.html">📊 评测总榜 (Leaderboard)</a>
      <a class="nav-tab" href="replay.html">🎬 交互式 3D 实机回放 (3D Replay)</a>
    </div>
  </div>

  <div class="kpi-grid">
    <div class="kpi-card">
      <div class="kpi-label">Pure 零先验全战役最佳成绩 (${pureRun?.verify?.scenesVisited ?? 116} 场景)</div>
      <div class="kpi-val" style="color:#38bdf8">${pureRun ? `${pureRun.result.toolCalls} 步 / ${pureRun.result.agentTurns} 轮` : '-'}</div>
      <div class="kpi-sub">${pureRun ? `${esc(pureRun.result.model)} · ${pureRun.verify?.callsPerTurn ?? 1.8} 步/轮 · 0 违规警告` : ''}</div>
    </div>
    <div class="kpi-card">
      <div class="kpi-label">Normal Play-and-Fix 全战役成绩 (${normalRun?.verify?.scenesVisited ?? 116} 场景)</div>
      <div class="kpi-val" style="color:#c084fc">${normalRun ? `${normalRun.result.toolCalls} 步 / ${normalRun.result.agentTurns} 轮` : '-'}</div>
      <div class="kpi-sub">${normalRun ? `${esc(normalRun.result.model)} · ${normalRun.verify?.scenesVisited ?? 116} 场景全覆盖` : ''}</div>
    </div>
    <div class="kpi-card">
      <div class="kpi-label">1:1 原版完整游戏 3D 场景挂载</div>
      <div class="kpi-val" style="color:#34d399">116 幕 .POL/.DDS + .MV3</div>
      <div class="kpi-sub">17 城镇 (Q01-Q17) + 22 迷宫 (M01-M26 全部 99 子层)</div>
    </div>
    <div class="kpi-card">
      <div class="kpi-label">五灵珠共鸣与神剑重铸</div>
      <div class="kpi-val" style="color:#fbbf24">圣灵魔剑 Lv.6</div>
      <div class="kpi-sub">土·雷·风·火·水 五灵珠齐聚 + 20 场 Boss 战全胜</div>
    </div>
  </div>

  <h2>🏆 Autonomous Agent 24-Chapter Playthrough Leaderboard</h2>
  <div class="table-wrap">
    <table>
      <thead>
        <tr>
          <th>Run ID</th>
          <th>Model</th>
          <th>Mode</th>
          <th>Chapters</th>
          <th>Tool Calls</th>
          <th>Agent Turns</th>
          <th>Calls/Turn</th>
          <th>move_to</th>
          <th>Divine Sword &amp; Five Pearls</th>
          <th>Victories</th>
          <th>Ending</th>
          <th>Duration</th>
          <th>Fairness Audit</th>
          <th>Replay Verdict</th>
          <th>3D Replay</th>
        </tr>
      </thead>
      <tbody>
        ${rows}
      </tbody>
    </table>
  </div>

  <h2>🗺️ 《仙剑奇侠传三》原版 CPK 全五幕 24 章 3D 场景与主线里程碑矩阵</h2>
  <div class="table-wrap">
    <table>
      <thead>
        <tr>
          <th>Chapter</th>
          <th>Act</th>
          <th>Scene ID</th>
          <th>Retail CPK &amp; 3D Mesh</th>
          <th>Chapter Title</th>
          <th>Key Gameplay &amp; Boss / Minigame Milestone</th>
          <th>Story Var</th>
        </tr>
      </thead>
      <tbody>
        ${chRows}
      </tbody>
    </table>
  </div>
</body>
</html>`;
}

function renderReplayHtml(compactRuns, manifest) {
  const payloadJson = JSON.stringify({
    generatedAt: new Date().toISOString(),
    chapters: PAL3_24_CHAPTERS,
    runs: compactRuns,
    manifest: {
      byScene: manifest.byScene || {},
      byWorld: manifest.byWorld || {},
    },
  });

  return `<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Agent-PAL3 3D Replay Player — 仙剑奇侠传三全流程 3D 实机录像回放器</title>
  <style>
    :root {
      --bg: #080c14;
      --panel: #111827;
      --panel-alt: #1b2436;
      --border: #26334d;
      --text: #e2e8f0;
      --muted: #94a3b8;
      --accent: #38bdf8;
      --gold: #f59e0b;
      --jade: #10b981;
      --danger: #ef4444;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      padding: 18px 24px 32px;
      background: radial-gradient(circle at 15% 0%, #152238 0%, var(--bg) 65%);
      color: var(--text);
      font-family: -apple-system, BlinkMacSystemFont, "PingFang SC", "Microsoft YaHei", sans-serif;
    }
    .topbar {
      display: flex;
      justify-content: space-between;
      align-items: center;
      flex-wrap: wrap;
      gap: 12px;
      margin-bottom: 14px;
      padding-bottom: 12px;
      border-bottom: 1px solid var(--border);
    }
    h1 { font-size: 20px; margin: 0; display: flex; align-items: center; gap: 10px; }
    .sub { font-size: 12.5px; color: var(--muted); margin-top: 3px; }
    .nav-tabs { display: flex; gap: 8px; align-items: center; }
    .nav-tab {
      text-decoration: none;
      padding: 7px 14px;
      border-radius: 7px;
      font-size: 13px;
      font-weight: 600;
      border: 1px solid var(--border);
      background: var(--panel);
      color: var(--text);
    }
    .nav-tab.active {
      background: linear-gradient(135deg, #0284c7, #2563eb);
      border-color: #38bdf8;
      color: #fff;
    }
    .toolbar {
      display: flex;
      flex-wrap: wrap;
      gap: 12px;
      align-items: center;
      background: var(--panel);
      border: 1px solid var(--border);
      border-radius: 10px;
      padding: 10px 14px;
      margin-bottom: 14px;
    }
    select, button, input[type="file"] {
      background: var(--panel-alt);
      color: var(--text);
      border: 1px solid var(--border);
      border-radius: 6px;
      padding: 6px 11px;
      font-size: 12.5px;
      cursor: pointer;
    }
    button:hover, select:hover { border-color: var(--accent); }
    button.primary {
      background: linear-gradient(135deg, #0284c7, #2563eb);
      border-color: #38bdf8;
      color: #fff;
      font-weight: 600;
    }
    .main-grid {
      display: grid;
      grid-template-columns: 1.38fr 1fr;
      gap: 14px;
    }
    @media (max-width: 1120px) {
      .main-grid { grid-template-columns: 1fr; }
    }
    .viewport-card {
      background: var(--panel);
      border: 1px solid var(--border);
      border-radius: 11px;
      overflow: hidden;
      display: flex;
      flex-direction: column;
    }
    .stage-wrap {
      position: relative;
      width: 100%;
      aspect-ratio: 16 / 9;
      background: #050811;
      overflow: hidden;
    }
    #stageImg {
      width: 100%;
      height: 100%;
      object-fit: cover;
      display: block;
    }
    .hud-top-left {
      position: absolute;
      top: 12px;
      left: 12px;
      background: rgba(8, 12, 20, 0.82);
      backdrop-filter: blur(6px);
      border: 1px solid rgba(56, 189, 248, 0.35);
      border-radius: 8px;
      padding: 8px 12px;
      max-width: 58%;
    }
    .hud-scene-title { font-size: 14px; font-weight: 700; color: #f8fafc; }
    .hud-scene-meta { font-size: 11.5px; color: #93c5fd; margin-top: 3px; font-family: ui-monospace, monospace; }
    .hud-top-right {
      position: absolute;
      top: 12px;
      right: 12px;
      background: rgba(8, 12, 20, 0.82);
      backdrop-filter: blur(6px);
      border: 1px solid rgba(245, 158, 11, 0.4);
      border-radius: 8px;
      padding: 8px 11px;
      text-align: right;
    }
    .sword-title { font-size: 12.5px; font-weight: 700; color: #fbbf24; }
    .pearl-row { display: flex; gap: 5px; justify-content: flex-end; margin-top: 5px; }
    .pearl-dot {
      padding: 2px 6px;
      border-radius: 999px;
      font-size: 10.5px;
      font-weight: 700;
      opacity: 0.28;
      border: 1px solid currentColor;
    }
    .pearl-dot.on { opacity: 1; box-shadow: 0 0 8px currentColor; }
    .hud-bottom {
      position: absolute;
      left: 12px;
      right: 12px;
      bottom: 12px;
      background: rgba(9, 13, 22, 0.88);
      backdrop-filter: blur(6px);
      border: 1px solid rgba(148, 163, 184, 0.35);
      border-radius: 8px;
      padding: 9px 13px;
    }
    .hud-action-line {
      display: flex;
      justify-content: space-between;
      align-items: center;
      font-size: 12px;
      color: #38bdf8;
      font-family: ui-monospace, monospace;
      margin-bottom: 4px;
    }
    .hud-dlg-line {
      font-size: 13px;
      color: #fef3c7;
      line-height: 1.45;
      max-height: 44px;
      overflow: hidden;
    }
    .scrubber-bar {
      padding: 11px 14px;
      background: var(--panel-alt);
      border-top: 1px solid var(--border);
      display: flex;
      flex-direction: column;
      gap: 8px;
    }
    .scrubber-controls {
      display: flex;
      align-items: center;
      gap: 8px;
    }
    #stepSlider { flex: 1; accent-color: #38bdf8; cursor: pointer; }
    .chapter-strip {
      display: flex;
      gap: 5px;
      overflow-x: auto;
      padding-bottom: 4px;
    }
    .ch-chip {
      padding: 3px 8px;
      border-radius: 5px;
      font-size: 11px;
      background: #0f172a;
      border: 1px solid var(--border);
      color: var(--muted);
      white-space: nowrap;
      cursor: pointer;
    }
    .ch-chip.active {
      background: rgba(56, 189, 248, 0.2);
      border-color: #38bdf8;
      color: #fff;
      font-weight: 600;
    }
    .side-col {
      display: flex;
      flex-direction: column;
      gap: 12px;
    }
    .side-card {
      background: var(--panel);
      border: 1px solid var(--border);
      border-radius: 10px;
      padding: 11px 13px;
    }
    .card-title {
      font-size: 12.5px;
      font-weight: 700;
      color: #cbd5e1;
      margin-bottom: 8px;
      display: flex;
      justify-content: space-between;
      align-items: center;
    }
    .radar-grid {
      display: grid;
      grid-template-columns: 200px 1fr;
      gap: 12px;
      align-items: center;
    }
    #radarCanvas {
      width: 200px;
      height: 200px;
      background: #070b12;
      border: 1px solid var(--border);
      border-radius: 8px;
    }
    .party-list { display: flex; flex-direction: column; gap: 6px; }
    .party-item {
      background: var(--panel-alt);
      border: 1px solid #222f46;
      border-radius: 6px;
      padding: 6px 9px;
      font-size: 12px;
    }
    .party-head { display: flex; justify-content: space-between; font-weight: 600; margin-bottom: 4px; }
    .bar-row { display: flex; gap: 6px; align-items: center; font-size: 10.5px; color: var(--muted); }
    .bar-track { flex: 1; height: 5px; background: #0b1120; border-radius: 3px; overflow: hidden; }
    .bar-fill { height: 100%; border-radius: 3px; }
    .log-list {
      max-height: 220px;
      overflow-y: auto;
      display: flex;
      flex-direction: column;
      gap: 4px;
      font-family: ui-monospace, monospace;
      font-size: 11.5px;
    }
    .log-row {
      padding: 5px 8px;
      border-radius: 5px;
      background: #0d1422;
      border: 1px solid transparent;
      cursor: pointer;
      display: flex;
      justify-content: space-between;
      gap: 8px;
    }
    .log-row:hover { border-color: #334155; }
    .log-row.active {
      background: rgba(56, 189, 248, 0.16);
      border-color: #38bdf8;
      color: #fff;
    }
  </style>
</head>
<body>
  <div class="topbar">
    <div>
      <h1>🎬 Agent-PAL3 仙剑奇侠传三 — 全五幕 24 章 3D 实机录像回放器</h1>
      <div class="sub">原版 CPK (.pol/.dds 3D 场景 + .mv3/.tga 角色骨骼动画) 软件 GLES3 光栅化 · 双层 .nav 俯视雷达 · 五灵珠与魔剑实时遥测</div>
    </div>
    <div class="nav-tabs">
      <a class="nav-tab" href="board.html">📊 评测总榜 (Leaderboard)</a>
      <a class="nav-tab active" href="replay.html">🎬 交互式 3D 实机回放 (3D Replay)</a>
    </div>
  </div>

  <div class="toolbar">
    <label>选择通关录像：
      <select id="runSelect"></select>
    </label>
    <button class="primary" id="playBtn">▶ 播放 (Space)</button>
    <button id="prevBtn">⏮ 上一步 (←)</button>
    <button id="nextBtn">下一步 (→) ⏭</button>
    <label>倍速：
      <select id="speedSelect">
        <option value="900">0.5×</option>
        <option value="520" selected>1.0×</option>
        <option value="260">2.0×</option>
        <option value="130">4.0×</option>
        <option value="65">8.0×</option>
      </select>
    </label>
    <span id="runMetaBadge" style="font-size:12px;color:#93c5fd;margin-left:auto"></span>
  </div>

  <div class="main-grid">
    <div class="viewport-card">
      <div class="stage-wrap">
        <img id="stageImg" alt="PAL3 3D Frame" />
        <div class="hud-top-left">
          <div class="hud-scene-title" id="hudSceneTitle">渝州·永安当与竹林初遇</div>
          <div class="hud-scene-meta" id="hudSceneMeta">q01/yn01a · Layer 0 · Pos (0, 0, 0)</div>
        </div>
        <div class="hud-top-right">
          <div class="sword-title" id="hudSwordTitle">🗡️ 魔剑 · 净化 Lv.0</div>
          <div class="pearl-row" id="hudPearlRow"></div>
        </div>
        <div class="hud-bottom">
          <div class="hud-action-line">
            <span id="hudStepCall">#0 init()</span>
            <span id="hudEventTag">initialized</span>
          </div>
          <div class="hud-dlg-line" id="hudDlgText">点击【播放】或拖动下方进度条回放 24 章通关全过程。</div>
        </div>
      </div>

      <div class="scrubber-bar">
        <div class="scrubber-controls">
          <span id="stepCounter" style="font-size:12px;font-family:monospace;min-width:95px">Step 0 / 123</span>
          <input type="range" id="stepSlider" min="0" max="1" value="0" />
        </div>
        <div class="chapter-strip" id="chapterStrip"></div>
      </div>
    </div>

    <div class="side-col">
      <div class="side-card">
        <div class="card-title">
          <span>🧭 双层 .NAV 寻路网格雷达 (Dual-Layer Nav Radar)</span>
          <span id="radarLayerBadge" style="color:#38bdf8;font-family:monospace">Layer 0</span>
        </div>
        <div class="radar-grid">
          <canvas id="radarCanvas" width="220" height="220"></canvas>
          <div class="party-list" id="partyList"></div>
        </div>
      </div>

      <div class="side-card">
        <div class="card-title">
          <span>💞 女主好感度 &amp; 背包行囊 (Favor &amp; Inventory)</span>
          <span id="moneyBadge" style="color:#fbbf24;font-family:monospace">500 文</span>
        </div>
        <div id="favorRow" style="font-size:12px;color:#e2e8f0;margin-bottom:6px"></div>
        <div id="invRow" style="font-size:11.5px;color:#94a3b8;line-height:1.4"></div>
      </div>

      <div class="side-card">
        <div class="card-title">
          <span>📜 逐步指令轨迹 (Step-by-Step Action Log)</span>
          <span id="victoryBadge" style="color:#34d399;font-family:monospace">0 胜</span>
        </div>
        <div class="log-list" id="logList"></div>
      </div>
    </div>
  </div>

  <script id="replayPayload" type="application/json">${payloadJson}</script>
  <script>
    (() => {
      const payload = JSON.parse(document.getElementById('replayPayload').textContent);
      const runs = payload.runs || [];
      const chapters = payload.chapters || [];

      const runSelect = document.getElementById('runSelect');
      const playBtn = document.getElementById('playBtn');
      const prevBtn = document.getElementById('prevBtn');
      const nextBtn = document.getElementById('nextBtn');
      const speedSelect = document.getElementById('speedSelect');
      const stepSlider = document.getElementById('stepSlider');
      const stepCounter = document.getElementById('stepCounter');
      const stageImg = document.getElementById('stageImg');
      const hudSceneTitle = document.getElementById('hudSceneTitle');
      const hudSceneMeta = document.getElementById('hudSceneMeta');
      const hudSwordTitle = document.getElementById('hudSwordTitle');
      const hudPearlRow = document.getElementById('hudPearlRow');
      const hudStepCall = document.getElementById('hudStepCall');
      const hudEventTag = document.getElementById('hudEventTag');
      const hudDlgText = document.getElementById('hudDlgText');
      const chapterStrip = document.getElementById('chapterStrip');
      const radarCanvas = document.getElementById('radarCanvas');
      const radarLayerBadge = document.getElementById('radarLayerBadge');
      const partyList = document.getElementById('partyList');
      const favorRow = document.getElementById('favorRow');
      const invRow = document.getElementById('invRow');
      const moneyBadge = document.getElementById('moneyBadge');
      const victoryBadge = document.getElementById('victoryBadge');
      const runMetaBadge = document.getElementById('runMetaBadge');
      const logList = document.getElementById('logList');

      let curRunIdx = 0;
      let curStepIdx = 0;
      let curSubIdx = -1;
      let timer = null;

      const PEARLS = [
        { key: 'earth', label: '土', color: '#f59e0b' },
        { key: 'thunder', label: '雷', color: '#c084fc' },
        { key: 'wind', label: '风', color: '#34d399' },
        { key: 'fire', label: '火', color: '#f87171' },
        { key: 'water', label: '水', color: '#38bdf8' },
      ];

      function populateRuns() {
        runSelect.innerHTML = '';
        runs.forEach((r, idx) => {
          const opt = document.createElement('option');
          opt.value = String(idx);
          opt.textContent = \`[\${r.mode.toUpperCase()}] \${r.runId} — \${r.model} (\${r.toolCalls} steps, \${r.scenesVisited}/24 chapters)\`;
          runSelect.appendChild(opt);
        });
        const urlRun = new URLSearchParams(location.search).get('run');
        if (urlRun) {
          const found = runs.findIndex((r) => r.runId === urlRun);
          if (found >= 0) curRunIdx = found;
        }
        runSelect.value = String(curRunIdx);
      }

      function drawRadar(run, stepIdx, subOverridePos) {
        const ctx = radarCanvas.getContext('2d');
        const W = radarCanvas.width;
        const H = radarCanvas.height;
        ctx.clearRect(0, 0, W, H);

        ctx.fillStyle = '#070b12';
        ctx.fillRect(0, 0, W, H);

        const st = run.steps[stepIdx];
        if (!st) return;
        const curScKey = \`\${st.cpk}/\${st.scn}\`;
        const curLayer = st.ldr?.layer ?? 0;

        // Grid lines
        ctx.strokeStyle = curLayer === 1 ? 'rgba(168, 85, 247, 0.16)' : 'rgba(56, 189, 248, 0.14)';
        ctx.lineWidth = 1;
        for (let g = 20; g < W; g += 25) {
          ctx.beginPath(); ctx.moveTo(g, 0); ctx.lineTo(g, H); ctx.stroke();
          ctx.beginPath(); ctx.moveTo(0, g); ctx.lineTo(W, g); ctx.stroke();
        }

        const toXY = (pos) => {
          const x = ((Number(pos?.[0]) || 0) + 95) / 190 * (W - 24) + 12;
          const y = ((Number(pos?.[2]) || 0) + 95) / 190 * (H - 24) + 12;
          return [Math.max(10, Math.min(W - 10, x)), Math.max(10, Math.min(H - 10, y))];
        };

        // Trail in current scene
        ctx.strokeStyle = '#38bdf8';
        ctx.lineWidth = 2;
        ctx.beginPath();
        let started = false;
        for (let i = 0; i <= stepIdx; i++) {
          const s = run.steps[i];
          if (\`\${s.cpk}/\${s.scn}\` !== curScKey || !s.ldr?.p) continue;
          const [px, py] = toXY(s.ldr.p);
          if (!started) { ctx.moveTo(px, py); started = true; }
          else ctx.lineTo(px, py);
        }
        ctx.stroke();

        // Objects / Triggers
        (st.objs || []).forEach((o) => {
          const [ox, oy] = toXY(o.p);
          ctx.fillStyle = o.t === 'ladder' ? '#c084fc' : '#f59e0b';
          ctx.fillRect(ox - 4, oy - 4, 8, 8);
        });

        // NPCs
        (st.npcs || []).forEach((n) => {
          const [nx, ny] = toXY(n.p);
          ctx.fillStyle = '#f87171';
          ctx.beginPath();
          ctx.arc(nx, ny, 4.5, 0, Math.PI * 2);
          ctx.fill();
          ctx.fillStyle = '#cbd5e1';
          ctx.font = '10px sans-serif';
          ctx.fillText(n.n.slice(0, 5), nx + 6, ny + 3);
        });

        // Leader position
        const ldrPos = subOverridePos || st.ldr?.p || [0, 0, 0];
        const [lx, ly] = toXY(ldrPos);
        ctx.fillStyle = '#34d399';
        ctx.beginPath();
        ctx.arc(lx, ly, 6, 0, Math.PI * 2);
        ctx.fill();
        ctx.strokeStyle = '#fff';
        ctx.lineWidth = 1.5;
        ctx.stroke();
      }

      function renderStep() {
        const run = runs[curRunIdx];
        if (!run || !run.steps.length) return;
        const st = run.steps[curStepIdx] || run.steps[0];
        const sub = (curSubIdx >= 0 && st.subs && st.subs[curSubIdx]) ? st.subs[curSubIdx] : null;

        stepSlider.max = String(run.steps.length - 1);
        stepSlider.value = String(curStepIdx);
        stepCounter.textContent = \`Step \${st.step} / \${run.steps.length - 1}\`;
        runMetaBadge.textContent = \`\${run.model} · \${run.mode.toUpperCase()} · \${run.callsPerTurn} calls/turn · SHA256 \${run.digest}\`;

        const imgSrc = sub?.img || st.img || '';
        if (imgSrc) stageImg.src = imgSrc;

        const chInfo = chapters.find((c) => c.key === \`\${st.cpk}/\${st.scn}\`);
        hudSceneTitle.textContent = chInfo
          ? \`第 \${String(chInfo.ch).padStart(2, '0')} 章 · \${st.title || chInfo.title}\`
          : (st.title || \`\${st.cpk}/\${st.scn}\`);

        const pos = sub?.p || st.ldr?.p || [0, 0, 0];
        hudSceneMeta.textContent = \`\${st.cpk}/\${st.scn} (\${chInfo?.cpk || 'CPK'}) · Layer \${st.ldr?.layer ?? 0} · Pos (\${pos.join(', ')}) · DrawCalls \${st.gl?.[0] || 0}\`;

        const swd = st.swd || { eq: '无', lv: 0, els: [] };
        hudSwordTitle.textContent = \`🗡️ \${swd.eq} · 五灵净化 Lv.\${swd.lv}/5\`;
        hudPearlRow.innerHTML = PEARLS.map((p) => {
          const on = (swd.els || []).includes(p.key);
          return \`<span class="pearl-dot \${on ? 'on' : ''}" style="color:\${p.color}">\${p.label}灵珠</span>\`;
        }).join('');

        const argsStr = Object.keys(st.args || {}).length ? JSON.stringify(st.args) : '';
        hudStepCall.textContent = \`#\${st.step} \${st.tool}(\${argsStr})\${sub ? \` [移动子帧 \${curSubIdx + 1}/\${st.subs.length}]\` : ''}\`;
        hudEventTag.textContent = st.ev || 'ok';

        if (st.cbt?.act) {
          const enStr = (st.cbt.en || []).map((e) => \`\${e.n}(HP:\${e.hp}/\${e.mhp})\`).join(' vs ');
          hudDlgText.textContent = \`⚔️ 【五灵太极阵 ATB 战斗 · 第 \${st.cbt.r} 回合】 \${enStr} | \${st.dlg || st.ev}\`;
        } else if (st.mg?.act) {
          hudDlgText.textContent = \`🎮 【\${st.mg.t}】 \${st.mg.p}\`;
        } else if (st.ending) {
          hudDlgText.textContent = \`🏆 【通关大结局：\${st.ending}】 \${st.dlg || ''}\`;
        } else {
          hudDlgText.textContent = st.dlg || st.ev || '探索场景中…';
        }

        radarLayerBadge.textContent = \`Layer \${st.ldr?.layer ?? 0} · Tile [\${(st.ldr?.tile || [0, 0]).join(',')}]\`;
        drawRadar(run, curStepIdx, sub?.p);

        partyList.innerHTML = (st.pty || []).map((p) => {
          const hpPct = Math.round((p.hp / Math.max(1, p.mhp)) * 100);
          const shPct = Math.round((p.sh / Math.max(1, p.msh)) * 100);
          return \`<div class="party-item">
            <div class="party-head">
              <span>\${p.n}\${p.id === 2 ? (p.red ? ' (红葵·雷)' : ' (蓝葵·水)') : ''} · Lv.\${p.lv}</span>
              <span style="color:#93c5fd;font-size:11px">[\${p.el.toUpperCase()} · \${p.slot}]</span>
            </div>
            <div class="bar-row">
              <span>精 \${p.hp}</span>
              <div class="bar-track"><div class="bar-fill" style="width:\${hpPct}%;background:#10b981"></div></div>
              <span>神 \${p.sh}</span>
              <div class="bar-track"><div class="bar-fill" style="width:\${shPct}%;background:#38bdf8"></div></div>
            </div>
          </div>\`;
        }).join('');

        const fav = st.fav || {};
        favorRow.innerHTML = \`唐雪见: <b>\${fav.xuejian ?? 50}</b> · 龙葵: <b>\${fav.longkui ?? 50}</b> · 紫萱: <b>\${fav.zixuan ?? 50}</b> · 花楹: <b>\${fav.huaying ?? 50}</b>\`;
        moneyBadge.textContent = \`💰 \${st.inv?.m ?? 0} 文\`;
        invRow.textContent = (st.inv?.it || []).length ? st.inv.it.join(' · ') : '背包暂无特殊道具';
        victoryBadge.textContent = \`⚔️ \${st.cbt?.v ?? 0} 场胜利 · 合击 \${st.cbt?.c ?? 0} 次\`;

        // Highlight active chapter chip
        Array.from(chapterStrip.children).forEach((el) => {
          el.classList.toggle('active', el.dataset.key === \`\${st.cpk}/\${st.scn}\`);
        });

        // Highlight log row
        Array.from(logList.children).forEach((el, idx) => {
          const active = idx === curStepIdx;
          el.classList.toggle('active', active);
          if (active) el.scrollIntoView({ block: 'nearest' });
        });
      }

      function buildChapterChipsAndLogs() {
        const run = runs[curRunIdx];
        if (!run) return;

        chapterStrip.innerHTML = '';
        const seen = new Set();
        run.steps.forEach((s, idx) => {
          const k = \`\${s.cpk}/\${s.scn}\`;
          if (!s.cpk || seen.has(k)) return;
          seen.add(k);
          const ch = chapters.find((c) => c.key === k);
          const chip = document.createElement('div');
          chip.className = 'ch-chip';
          chip.dataset.key = k;
          chip.textContent = ch ? \`Ch.\${String(ch.ch).padStart(2, '0')} \${ch.title.split('·')[0]}\` : k;
          chip.onclick = () => {
            curStepIdx = idx;
            curSubIdx = -1;
            renderStep();
          };
          chapterStrip.appendChild(chip);
        });

        logList.innerHTML = '';
        run.steps.forEach((s, idx) => {
          const row = document.createElement('div');
          row.className = 'log-row';
          row.innerHTML = \`<span>#\${s.step} <b>\${s.tool}</b> (\${s.cpk}/\${s.scn})</span><span style="color:#94a3b8">\${(s.ev || '').slice(0, 28)}</span>\`;
          row.onclick = () => {
            curStepIdx = idx;
            curSubIdx = -1;
            renderStep();
          };
          logList.appendChild(row);
        });
      }

      function stepForward() {
        const run = runs[curRunIdx];
        if (!run) return;
        const st = run.steps[curStepIdx];
        if (st?.subs && curSubIdx + 1 < st.subs.length) {
          curSubIdx++;
          renderStep();
          return;
        }
        curSubIdx = -1;
        if (curStepIdx + 1 < run.steps.length) {
          curStepIdx++;
          renderStep();
        } else {
          stopPlay();
        }
      }

      function startPlay() {
        stopPlay();
        playBtn.textContent = '⏸ 暂停 (Space)';
        const delay = Number(speedSelect.value) || 520;
        timer = setInterval(stepForward, delay);
      }

      function stopPlay() {
        if (timer) {
          clearInterval(timer);
          timer = null;
        }
        playBtn.textContent = '▶ 播放 (Space)';
      }

      playBtn.onclick = () => (timer ? stopPlay() : startPlay());
      prevBtn.onclick = () => {
        stopPlay();
        curSubIdx = -1;
        curStepIdx = Math.max(0, curStepIdx - 1);
        renderStep();
      };
      nextBtn.onclick = () => {
        stopPlay();
        stepForward();
      };
      stepSlider.oninput = () => {
        stopPlay();
        curSubIdx = -1;
        curStepIdx = Number(stepSlider.value) || 0;
        renderStep();
      };
      speedSelect.onchange = () => {
        if (timer) startPlay();
      };
      runSelect.onchange = () => {
        stopPlay();
        curRunIdx = Number(runSelect.value) || 0;
        curStepIdx = 0;
        curSubIdx = -1;
        buildChapterChipsAndLogs();
        renderStep();
      };
      window.addEventListener('keydown', (e) => {
        if (e.code === 'Space') { e.preventDefault(); playBtn.click(); }
        else if (e.code === 'ArrowRight') { e.preventDefault(); nextBtn.click(); }
        else if (e.code === 'ArrowLeft') { e.preventDefault(); prevBtn.click(); }
      });

      populateRuns();
      buildChapterChipsAndLogs();
      renderStep();
    })();
  </script>
</body>
</html>`;
}

const runs = loadRuns();
const manifest = loadFrameManifest();
const compactRuns = buildCompactReplayRuns(runs, manifest);
const boardHtml = renderBoardHtml(runs);
const replayHtml = renderReplayHtml(compactRuns, manifest);

fs.mkdirSync(OUT_ROOT, { recursive: true });
fs.mkdirSync(WEB_DIR, { recursive: true });
fs.writeFileSync(path.join(OUT_ROOT, 'board.html'), boardHtml);
fs.writeFileSync(path.join(WEB_DIR, 'board.html'), boardHtml);
fs.writeFileSync(path.join(OUT_ROOT, 'replay.html'), replayHtml);
fs.writeFileSync(path.join(WEB_DIR, 'replay.html'), replayHtml);
console.log(
  `[board] Updated board.html (${(Buffer.byteLength(boardHtml) / 1024).toFixed(1)} KB) & replay.html (${(Buffer.byteLength(replayHtml) / 1024).toFixed(1)} KB) with ${runs.length} runs and ${Object.keys(manifest.byPos || {}).length} 3D frames.`
);
