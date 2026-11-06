#!/usr/bin/env node
/**
 * render-frames.mjs — 从原版《仙剑奇侠传三》零售 CPK 确定性重放并光栅化渲染全流程 3D 实机画面
 *
 *   node bench/render-frames.mjs [runDir...]
 *
 * 通过 agent-pal3-host 内置的软件 GLES3 光栅化管线 (render_frame / render_substeps)，
 * 将 24 章通关轨迹中每一个 (cpk, scn, leader, position) 状态渲染为原版 .pol/.dds + .mv3/.tga 3D 画面，
 * 输出至 web/frames/ 与 bench/out/frames/ 并生成 manifest.json 供 3D 回放器调用。
 */
import { spawn, execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { readEpisodeLog } from './verify.mjs';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '..');
const HOST_BIN =
  process.env.AGENT_PAL3_HOST_BIN || path.resolve(ROOT, 'build/agent-pal3-host');
const GAME_DIR =
  process.env.PAL3_GAME_DIR || '/Volumes/T7-Data/pal/pal3-data/PAL3';
const WEB_FRAMES_DIR = path.resolve(ROOT, 'web/frames');
const OUT_FRAMES_DIR = path.resolve(ROOT, 'bench/out/frames');
const OUT_ROOT = path.resolve(__dirname, 'out');

export function makePosKey(st) {
  const cpk = st?.cpk || st?.w || '';
  const scn = st?.scn || st?.sc || '';
  const ldrName = st?.leader?.name || st?.ldr?.n || '景天';
  const rawPos = st?.leader?.position || st?.ldr?.p || [0, 0, 0];
  const p = rawPos.map((x) => Math.round((Number(x) || 0) / 8) * 8).join(',');
  return `${cpk}/${scn}@${ldrName}@${p}`;
}

export function makeSceneLeaderKey(st) {
  const cpk = st?.cpk || st?.w || '';
  const scn = st?.scn || st?.sc || '';
  const ldrName = st?.leader?.name || st?.ldr?.n || '景天';
  return `${cpk}/${scn}@${ldrName}`;
}

export function makeSceneKey(st) {
  const cpk = st?.cpk || st?.w || '';
  const scn = st?.scn || st?.sc || '';
  return `${cpk}/${scn}`;
}

class HostProcess {
  constructor(mode = 'pure', fullCampaign = true, gameDir = GAME_DIR) {
    const workDir = fs.mkdtempSync(path.join(os.tmpdir(), 'pal3-render-work-'));
    const args = ['--work-dir', workDir, '--mode', mode];
    if (fullCampaign) args.push('--full-campaign');
    if (gameDir && fs.existsSync(gameDir)) {
      args.push('--game-dir', gameDir);
    }
    this.workDir = workDir;
    this.proc = spawn(HOST_BIN, args, { stdio: ['pipe', 'pipe', 'pipe'] });
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
    try {
      this.proc.stdin.end();
      this.proc.kill();
    } catch {}
    try {
      fs.rmSync(this.workDir, { recursive: true, force: true });
    } catch {}
  }
}

async function main() {
  const cliDirs = process.argv.slice(2).filter((a) => !a.startsWith('--'));
  let runDirs = [];
  if (cliDirs.length > 0) {
    runDirs = cliDirs.map((d) => path.resolve(d));
  } else if (fs.existsSync(OUT_ROOT)) {
    runDirs = fs
      .readdirSync(OUT_ROOT, { withFileTypes: true })
      .filter((d) => d.isDirectory() && d.name !== 'frames')
      .map((d) => path.join(OUT_ROOT, d.name))
      .filter((d) => fs.existsSync(path.join(d, 'episode.jsonl')))
      .sort()
      .reverse();
  }

  if (runDirs.length === 0) {
    console.log('No episode runs found in bench/out.');
    return;
  }

  fs.mkdirSync(WEB_FRAMES_DIR, { recursive: true });
  fs.mkdirSync(OUT_FRAMES_DIR, { recursive: true });
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'pal3-frames-'));

  const manifest = {
    width: 512,
    height: 288,
    generatedAt: new Date().toISOString(),
    byPos: {},
    bySceneLeader: {},
    byScene: {},
    byWorld: {},
    substepsByStep: {},
  };

  const renderedPosKeys = new Map();
  const extraBmpJobs = [];
  let frameCounter = 0;
  let substepCounter = 0;

  async function captureIfNeeded(host, st) {
    if (!st || !st.cpk) return;
    const posKey = makePosKey(st);
    const scLdrKey = makeSceneLeaderKey(st);
    const scKey = makeSceneKey(st);
    const wKey = st.cpk;

    let fileName = renderedPosKeys.get(posKey);
    if (!fileName) {
      frameCounter++;
      const slug = `${st.cpk}_${st.scn}`.replace(/[^a-zA-Z0-9_-]/g, '_');
      fileName = `f_${String(frameCounter).padStart(3, '0')}_${slug}.jpg`;
      const bmpPath = path.join(tmpDir, `${frameCounter}.bmp`);
      await host.send('render_frame', {
        path: bmpPath,
        width: 512,
        height: 288,
      });
      renderedPosKeys.set(posKey, fileName);
      renderedPosKeys.set(`__bmp_${frameCounter}`, { bmpPath, fileName });
    }

    manifest.byPos[posKey] = fileName;
    if (!manifest.bySceneLeader[scLdrKey]) {
      manifest.bySceneLeader[scLdrKey] = fileName;
    }
    if (!manifest.byScene[scKey]) {
      manifest.byScene[scKey] = fileName;
    }
    if (!manifest.byWorld[wKey]) {
      manifest.byWorld[wKey] = fileName;
    }
  }

  for (const runDir of runDirs) {
    const runId = path.basename(runDir);
    const logPath = path.join(runDir, 'episode.jsonl');
    const resultPath = path.join(runDir, 'result.json');
    if (!fs.existsSync(logPath)) continue;

    const log = readEpisodeLog(logPath);
    const result = fs.existsSync(resultPath)
      ? JSON.parse(fs.readFileSync(resultPath, 'utf8'))
      : {};
    const mode = result.mode || log.start?.mode || 'pure';
    const fullCampaign = Boolean(result.fullCampaign ?? log.start?.fullCampaign ?? true);

    console.log(`[render-frames] Replaying ${runId} (mode=${mode}, steps=${log.calls.length})...`);
    const host = new HostProcess(mode, fullCampaign, GAME_DIR);
    try {
      let prevSt = await host.send('observe');
      await captureIfNeeded(host, prevSt);

      for (let i = 0; i < log.calls.length; i++) {
        const c = log.calls[i];
        const stepNum = c.step || i + 1;

        if ((c.tool === 'move_to' || c.tool === 'walk_to') && prevSt?.leader?.position) {
          const fromPos = prevSt.leader.position;
          const toX = Number(c.args?.x ?? fromPos[0]);
          const toZ = Number(c.args?.z ?? fromPos[2]);
          const toY = Number(c.args?.layer === 1 ? 20 : fromPos[1]);
          const subPrefix = path.join(tmpDir, `sub_${runId}_${stepNum}`);
          const subRes = await host.send('render_substeps', {
            prefix: subPrefix,
            from_x: fromPos[0],
            from_y: fromPos[1],
            from_z: fromPos[2],
            to_x: toX,
            to_y: toY,
            to_z: toZ,
            width: 512,
            height: 288,
            max_substeps: 3,
          });
          if (subRes?.ok && Array.isArray(subRes.substeps) && subRes.substeps.length > 0) {
            const slug = `${prevSt.cpk}_${prevSt.scn}`.replace(/[^a-zA-Z0-9_-]/g, '_');
            const stepSubs = [];
            for (let sIdx = 0; sIdx < subRes.substeps.length; sIdx++) {
              const sub = subRes.substeps[sIdx];
              substepCounter++;
              const subFile = `s_${runId.slice(0, 9)}_${String(stepNum).padStart(3, '0')}_${sIdx + 1}_${slug}.jpg`;
              extraBmpJobs.push({ bmpPath: sub.path, fileName: subFile });
              stepSubs.push({
                w: prevSt.cpk,
                sc: prevSt.scn,
                p: (sub.position || [0, 0, 0]).map((v) => Math.round(Number(v) * 10) / 10),
                yaw: Math.round(Number(sub.yaw || 0)),
                img: subFile,
              });
            }
            manifest.substepsByStep[`${runId}:${stepNum}`] = stepSubs;
            if (!manifest.substepsByStep[String(stepNum)]) {
              manifest.substepsByStep[String(stepNum)] = stepSubs;
            }
          }
        }

        const st = await host.send(c.tool, c.args || {});
        await captureIfNeeded(host, st);
        prevSt = st;
        if ((i + 1) % 25 === 0 || i === log.calls.length - 1) {
          console.log(
            `[render-frames] ${runId} Step ${i + 1}/${log.calls.length} (${st.cpk}/${st.scn}) — ${frameCounter} key frames + ${substepCounter} sub-steps`
          );
        }
      }
    } finally {
      host.close();
    }
  }

  // Batch convert all BMPs to optimized JPEGs using Python Pillow
  const jobs = [];
  for (let i = 1; i <= frameCounter; i++) {
    const info = renderedPosKeys.get(`__bmp_${i}`);
    if (info) jobs.push(info);
  }
  for (const job of extraBmpJobs) {
    jobs.push(job);
  }

  const pyScript = `
import sys, json, shutil
from pathlib import Path
from PIL import Image

jobs = json.loads(sys.stdin.read())
web_dir = Path(${JSON.stringify(WEB_FRAMES_DIR)})
out_dir = Path(${JSON.stringify(OUT_FRAMES_DIR)})

for j in jobs:
    bmp = Path(j["bmpPath"])
    if not bmp.exists():
        continue
    im = Image.open(bmp)
    dst_web = web_dir / j["fileName"]
    dst_out = out_dir / j["fileName"]
    im.save(dst_web, format="JPEG", quality=82, optimize=True)
    shutil.copy2(dst_web, dst_out)
`;
  execFileSync('python3', ['-c', pyScript], {
    input: JSON.stringify(jobs),
    stdio: ['pipe', 'inherit', 'inherit'],
  });

  fs.rmSync(tmpDir, { recursive: true, force: true });

  const manifestJson = JSON.stringify(manifest, null, 2) + '\n';
  fs.writeFileSync(path.join(WEB_FRAMES_DIR, 'manifest.json'), manifestJson);
  fs.writeFileSync(path.join(OUT_FRAMES_DIR, 'manifest.json'), manifestJson);

  console.log(
    `[render-frames] Done! Generated ${frameCounter} 3D key frames + ${substepCounter} sub-step frames in web/frames/ and bench/out/frames/.`
  );
}

if (import.meta.url === `file://${process.argv[1]}`) {
  main().catch((err) => {
    console.error(err);
    process.exit(1);
  });
}
