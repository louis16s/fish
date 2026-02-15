function safeKeyPart(s) {
  return String(s || '')
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9._-]+/g, '_')
    .slice(0, 64);
}

function clampInt(v, a, b, def) {
  const n = parseInt(v, 10);
  if (!Number.isFinite(n)) return def;
  return Math.min(b, Math.max(a, n));
}

class LogCache {
  constructor(opts) {
    const o = opts || {};
    this.maxBytes = clampInt(o.maxBytes, 16 * 1024, 2 * 1024 * 1024, 256 * 1024);
    this.maxChunkBytes = clampInt(o.maxChunkBytes, 128, 16 * 1024, 2048);
    this._m = new Map(); // key -> { chunks: Buffer[], bytes: number, updatedAt: number }
  }

  _key(deviceId, name) {
    return safeKeyPart(deviceId) + ':' + safeKeyPart(name || 'error');
  }

  append(deviceId, name, text) {
    const key = this._key(deviceId, name);
    const s = (text == null) ? '' : String(text);
    if (!s) return;

    const b = Buffer.from(s, 'utf8');
    if (!b.length) return;

    // Keep each chunk reasonably sized so tail assembly isn't too expensive.
    const chunks = [];
    if (b.length <= this.maxChunkBytes) {
      chunks.push(b);
    } else {
      for (let i = 0; i < b.length; i += this.maxChunkBytes) {
        chunks.push(b.subarray(i, Math.min(b.length, i + this.maxChunkBytes)));
      }
    }

    let ent = this._m.get(key);
    if (!ent) {
      ent = { chunks: [], bytes: 0, updatedAt: Date.now() };
      this._m.set(key, ent);
    }

    for (const c of chunks) {
      ent.chunks.push(c);
      ent.bytes += c.length;
    }
    ent.updatedAt = Date.now();

    while (ent.bytes > this.maxBytes && ent.chunks.length) {
      const x = ent.chunks.shift();
      ent.bytes -= x ? x.length : 0;
    }
  }

  clear(deviceId, name) {
    this._m.delete(this._key(deviceId, name));
  }

  has(deviceId, name) {
    const ent = this._m.get(this._key(deviceId, name));
    return !!(ent && ent.bytes > 0);
  }

  updatedAt(deviceId, name) {
    const ent = this._m.get(this._key(deviceId, name));
    return ent ? ent.updatedAt : 0;
  }

  tail(deviceId, name, tailBytes) {
    const ent = this._m.get(this._key(deviceId, name));
    if (!ent || !ent.bytes || !ent.chunks.length) return '';
    const want = clampInt(tailBytes, 0, this.maxBytes, this.maxBytes);
    if (want <= 0) return '';

    if (want >= ent.bytes) {
      return Buffer.concat(ent.chunks, ent.bytes).toString('utf8');
    }

    // Build tail from the end without concatenating everything.
    let remaining = want;
    const out = [];
    for (let i = ent.chunks.length - 1; i >= 0 && remaining > 0; i--) {
      const c = ent.chunks[i];
      if (!c || !c.length) continue;
      if (c.length <= remaining) {
        out.push(c);
        remaining -= c.length;
      } else {
        out.push(c.subarray(c.length - remaining));
        remaining = 0;
      }
    }
    out.reverse();
    return Buffer.concat(out).toString('utf8');
  }
}

module.exports = { LogCache };

