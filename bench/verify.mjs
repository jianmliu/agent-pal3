#!/usr/bin/env node
/**
 * verify.mjs — 仙剑奇侠传三 (agent-pal3) 确定性重放核验与 Pure 模式公平性审计器
 *
 *   node bench/verify.mjs [runDir...]
 */
import { spawn } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const HOST_BIN =
  process.env.AGENT_PAL3_HOST_BIN ||
  path.resolve(__dirname, '../build/agent-pal3-host');
const OUT_ROOT = path.resolve(__dirname, 'out');

class HostProcess {
  constructor(mode = 'normal', fullCampaign = true) {
    const workDir = fs.mkdtempSync(path.join(os.tmpdir(), 'pal3-verify-'));
    const args = ['--work-dir', workDir, '--mode', mode];
    if (fullCampaign) args.push('--full-campaign');
    this.proc = spawn(HOST_BIN, args, {
      stdio: ['pipe', 'pipe', 'pipe'],
    });
    this.buf = '';
    this.waiters = [];
    this.proc.stdout.on('data', (chunk) => {
      this.buf += chunk.toString();
      const lines = this.buf.split('\n');
      this.buf = lines.pop();
      for (const line of lines) {
        if (!line.trim()) continue;
        const msg = JSON.parse(line);
        const resolve = this.waiters.shift();
        if (resolve) resolve(msg);
      }
    });
  }

  send(cmd, extra = {}) {
    return new Promise((resolve) => {
      this.waiters.push(resolve);
      this.proc.stdin.write(JSON.stringify({ cmd, ...extra }) + '\n');
    });
  }

  close() {
    this.proc.kill();
  }
}

export function computeEpisodeDigest(runDir) {
  const h = crypto.createHash('sha256');
  for (const name of ['result.json', 'episode.jsonl', 'client.jsonl']) {
    const p = path.join(runDir, name);
    h.update(`\0${name}\0`);
    if (fs.existsSync(p)) {
      h.update(fs.readFileSync(p));
    }
  }
  return h.digest('hex');
}

export function readEpisodeLog(jsonlPath) {
  const lines = fs
    .readFileSync(jsonlPath, 'utf8')
    .split('\n')
    .filter((l) => l.trim());
  const out = { start: null, calls: [], end: null };
  for (const line of lines) {
    const ev = JSON.parse(line);
    if (ev.type === 'start') out.start = ev;
    else if (ev.type === 'call') out.calls.push(ev);
    else if (ev.type === 'end') out.end = ev;
  }
  if (!out.start) throw new Error(`${jsonlPath}: missing start event`);
  return out;
}

const samePos = (a, b) => {
  if (!Array.isArray(a) || !Array.isArray(b) || a.length !== b.length) return false;
  return a.every((v, i) => Math.abs(Number(v) - Number(b[i])) < 1e-2);
};

export async function verifyRunDir(runDir) {
  const runId = path.basename(runDir);
  const resultPath = path.join(runDir, 'result.json');
  const logPath = path.join(runDir, 'episode.jsonl');
  if (!fs.existsSync(resultPath) || !fs.existsSync(logPath)) {
    return null;
  }

  const result = JSON.parse(fs.readFileSync(resultPath, 'utf8'));
  const log = readEpisodeLog(logPath);
  const mode = result.mode || log.start?.mode || 'normal';
  const fullCampaign = Boolean(result.fullCampaign ?? log.start?.fullCampaign ?? true);
  const host = new HostProcess(mode, fullCampaign);
  const diffs = [];
  const visitedScenes = new Set();

  try {
    const init = await host.send('observe');
    visitedScenes.add(`${init.cpk}/${init.scn}`);
    if (
      log.start?.initial &&
      (init.cpk !== log.start.initial.cpk || init.scn !== log.start.initial.scn)
    ) {
      diffs.push(
        `initial cpk/scn mismatch: logged ${log.start.initial.cpk}/${log.start.initial.scn}, replayed ${init.cpk}/${init.scn}`
      );
    }

    let lastState = init;
    for (let i = 0; i < log.calls.length; i++) {
      const call = log.calls[i];
      const args = call.args || {};
      lastState = await host.send(call.tool, args);
      if (lastState.cpk && lastState.scn) {
        visitedScenes.add(`${lastState.cpk}/${lastState.scn}`);
      }
      const expected = call.state;
      if (!expected) continue;

      if (lastState.cpk !== expected.cpk || lastState.scn !== expected.scn) {
        diffs.push(
          `step #${call.step} (${call.tool}): cpk/scn ${expected.cpk}/${expected.scn} -> ${lastState.cpk}/${lastState.scn}`
        );
        break;
      }
      if (
        expected.leader?.position &&
        lastState.leader?.position &&
        !samePos(expected.leader.position, lastState.leader.position)
      ) {
        diffs.push(
          `step #${call.step} (${call.tool}): leader.position ${JSON.stringify(expected.leader.position)} -> ${JSON.stringify(lastState.leader.position)}`
        );
        break;
      }
      if (
        expected.combat?.victories !== undefined &&
        lastState.combat?.victories !== expected.combat?.victories
      ) {
        diffs.push(
          `step #${call.step} (${call.tool}): combat.victories ${expected.combat?.victories} -> ${lastState.combat?.victories}`
        );
        break;
      }
      if (Boolean(lastState.game_cleared) !== Boolean(expected.game_cleared)) {
        diffs.push(
          `step #${call.step} (${call.tool}): game_cleared ${expected.game_cleared} -> ${lastState.game_cleared}`
        );
        break;
      }
    }

    if (result.toolCalls !== log.calls.length) {
      diffs.push(
        `toolCalls mismatch: result.json=${result.toolCalls}, episode.jsonl=${log.calls.length}`
      );
    }
    if (
      result.gameCleared !== undefined &&
      Boolean(result.gameCleared) !== Boolean(lastState.game_cleared)
    ) {
      diffs.push(
        `final gameCleared mismatch: result.json=${result.gameCleared}, replay=${Boolean(lastState.game_cleared)}`
      );
    }

    const digest = computeEpisodeDigest(runDir);
    const agentTurns = Number(result.agentTurns) || 1;
    const callsPerTurn = Number((log.calls.length / Math.max(1, agentTurns)).toFixed(2));
    const moveToCalls = log.calls.filter(
      (c) => c.tool === 'move_to' || c.tool === 'walk_to'
    ).length;

    let usedExternalScript = false;
    const clientPath = path.join(runDir, 'client.jsonl');
    if (fs.existsSync(clientPath)) {
      const clientRaw = fs.readFileSync(clientPath, 'utf8');
      if (/\b(drive\.mjs|s\.sh|g\.sh)\b/.test(clientRaw)) {
        usedExternalScript = true;
      }
    }

    const optimizationWarnings = [];
    if (mode === 'pure') {
      if (usedExternalScript) {
        optimizationWarnings.push('external_driver_script_detected');
      }
      if (callsPerTurn > 2.5) {
        optimizationWarnings.push(`excessive_turn_batching(${callsPerTurn}_calls_per_turn)`);
      }
      if (moveToCalls === 0 && log.calls.length > 10) {
        optimizationWarnings.push('zero_move_to_navigation');
      }
      if (visitedScenes.size < 24) {
        optimizationWarnings.push(`incomplete_24_chapters(${visitedScenes.size}/24)`);
      }
      if ((lastState.demon_sword?.infused_elements?.length ?? 0) < 5) {
        optimizationWarnings.push('incomplete_five_spirit_pearls');
      }
    }

    const verdict = {
      ok: diffs.length === 0,
      state: diffs.length === 0 ? 'verified' : 'failed',
      runId,
      model: result.model,
      mode,
      fullCampaign,
      toolCalls: log.calls.length,
      agentTurns,
      callsPerTurn,
      moveToCalls,
      scenesVisited: visitedScenes.size,
      optimizationWarnings,
      finalScene: `${lastState.cpk}/${lastState.scn}`,
      endingName: lastState.ending_name || '',
      divineSword: lastState.demon_sword?.equipped_divine_sword || '',
      purificationLevel: lastState.demon_sword?.purification_level ?? 0,
      infusedElements: lastState.demon_sword?.infused_elements ?? [],
      gameCleared: Boolean(lastState.game_cleared),
      victories: lastState.combat?.victories ?? 0,
      episodeDigest: digest,
      verifiedAt: new Date().toISOString(),
      diffs,
    };
    fs.writeFileSync(
      path.join(runDir, 'verify.json'),
      JSON.stringify(verdict, null, 2) + '\n'
    );
    return verdict;
  } finally {
    host.close();
  }
}

async function main() {
  if (!fs.existsSync(OUT_ROOT)) {
    console.log('No runs found in bench/out.');
    return;
  }
  const args = process.argv.slice(2).filter((a) => !a.startsWith('--'));
  const dirs =
    args.length > 0
      ? args.map((d) => path.resolve(d))
      : fs
          .readdirSync(OUT_ROOT, { withFileTypes: true })
          .filter((d) => d.isDirectory())
          .map((d) => path.join(OUT_ROOT, d.name))
          .sort();

  let passed = 0;
  let failed = 0;
  for (const dir of dirs) {
    const v = await verifyRunDir(dir);
    if (!v) continue;
    if (v.ok) {
      passed++;
      console.log(
        `[VERIFIED ✓] ${v.runId} | model=${v.model} | mode=${v.mode} | steps=${v.toolCalls} | scenes=${v.scenesVisited}/24 | sword=${v.divineSword}(Lv.${v.purificationLevel}) | ending=${v.endingName} | warnings=${v.optimizationWarnings.length} | sha256=${v.episodeDigest.slice(0, 12)}`
      );
    } else {
      failed++;
      console.error(`[FAILED ✗]   ${v.runId} | diffs=${v.diffs.join('; ')}`);
    }
  }
  console.log(`\nReplay Verification Summary: ${passed} verified, ${failed} failed.`);
  if (failed > 0) process.exit(1);
}

if (import.meta.url === `file://${process.argv[1]}`) {
  main().catch((err) => {
    console.error(err);
    process.exit(1);
  });
}
