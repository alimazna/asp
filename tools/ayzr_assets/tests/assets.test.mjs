// AYZR asset-pipeline test.
//
// Proves the brand assets are deterministic and correctly shaped, so a future
// edit cannot silently ship a wrong-sized icon or a non-reproducible file.

import { test } from 'node:test';
import assert from 'node:assert';
import { execFileSync } from 'node:child_process';
import { readFileSync, existsSync, mkdtempSync, rmSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, '..', '..', '..');
const gen = join(root, 'tools', 'ayzr_assets', 'generate_assets.py');

function sha(p) {
  return createHash('sha256').update(readFileSync(p)).digest('hex');
}

// Read a PNG's IHDR width/height without any image library.
function pngSize(buf) {
  assert.equal(buf.slice(1, 4).toString('ascii'), 'PNG', 'is a PNG');
  return { w: buf.readUInt32BE(16), h: buf.readUInt32BE(20) };
}

test('asset generator runs and emits all five assets', () => {
  const out = mkdtempSync(join(tmpdir(), 'ayzr-assets-'));
  try {
    execFileSync('python3', [gen, '--out', out], { stdio: 'pipe' });
    for (const name of ['logo-mark.png', 'favicon.png', 'app-icon.png',
                        'og-card.png', 'wordmark-ar.png']) {
      assert.ok(existsSync(join(out, name)), `${name} was written`);
    }
  } finally {
    rmSync(out, { recursive: true, force: true });
  }
});

test('assets have the exact dimensions Android and the web need', () => {
  const dir = join(root, 'tools', 'ayzr_observatory', 'assets');
  const expect = {
    'logo-mark.png': [256, 256],
    'favicon.png': [64, 64],
    'app-icon.png': [512, 512],
    'og-card.png': [1200, 630],
    'wordmark-ar.png': [1200, 260],
  };
  for (const [name, [w, h]] of Object.entries(expect)) {
    const p = join(dir, name);
    if (!existsSync(p)) continue;
    const { w: gotW, h: gotH } = pngSize(readFileSync(p));
    assert.equal(gotW, w, `${name} width`);
    assert.equal(gotH, h, `${name} height`);
  }
});

test('regeneration is byte-for-byte deterministic', () => {
  const a = mkdtempSync(join(tmpdir(), 'ayzr-a-'));
  const b = mkdtempSync(join(tmpdir(), 'ayzr-b-'));
  try {
    execFileSync('python3', [gen, '--out', a], { stdio: 'pipe' });
    execFileSync('python3', [gen, '--out', b], { stdio: 'pipe' });
    for (const name of ['logo-mark.png', 'app-icon.png', 'og-card.png']) {
      assert.equal(sha(join(a, name)), sha(join(b, name)),
        `${name} is reproducible across runs`);
    }
  } finally {
    rmSync(a, { recursive: true, force: true });
    rmSync(b, { recursive: true, force: true });
  }
});
