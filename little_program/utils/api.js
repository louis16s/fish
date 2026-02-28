const { BASE_URL } = require('./config');

let cookie = '';

function normalizeCookie(setCookie) {
  if (!setCookie) return '';
  const first = String(setCookie).split(';')[0].trim();
  return first;
}

function mergeCookie(newCookie) {
  if (!newCookie) return;
  const m = {};
  String(cookie || '').split(';').forEach((kv) => {
    const p = kv.trim();
    if (!p) return;
    const i = p.indexOf('=');
    if (i <= 0) return;
    m[p.slice(0, i).trim()] = p.slice(i + 1).trim();
  });
  String(newCookie || '').split(';').forEach((kv) => {
    const p = kv.trim();
    if (!p) return;
    const i = p.indexOf('=');
    if (i <= 0) return;
    m[p.slice(0, i).trim()] = p.slice(i + 1).trim();
  });
  cookie = Object.keys(m).map((k) => `${k}=${m[k]}`).join('; ');
}

function setCookieRaw(v) {
  cookie = String(v || '');
}

function getCookieRaw() {
  return cookie;
}

function request(path, options = {}) {
  const method = options.method || 'GET';
  const headers = Object.assign({}, options.headers || {});
  const useCookie = options.useCookie !== false;
  if (useCookie && cookie) headers.Cookie = cookie;

  const url = /^https?:\/\//i.test(path) ? path : `${BASE_URL}${path}`;

  return new Promise((resolve, reject) => {
    wx.request({
      url,
      method,
      timeout: options.timeout || 15000,
      header: headers,
      data: options.data,
      success(res) {
        try {
          const h = res.header || {};
          const sc = h['Set-Cookie'] || h['set-cookie'];
          if (sc) mergeCookie(normalizeCookie(sc));
        } catch (e) {}
        resolve(res);
      },
      fail(err) {
        reject(err);
      }
    });
  });
}

async function requestJSON(path, options = {}) {
  const res = await request(path, options);
  const code = Number(res.statusCode) || 0;
  let body = res.data;
  if (typeof body === 'string') {
    try {
      body = JSON.parse(body);
    } catch (e) {}
  }
  if (code < 200 || code >= 300) {
    const err = new Error((body && body.error) ? body.error : `HTTP ${code}`);
    err.statusCode = code;
    err.body = body;
    throw err;
  }
  return body;
}

async function requestText(path, options = {}) {
  const res = await request(path, options);
  const code = Number(res.statusCode) || 0;
  const body = typeof res.data === 'string' ? res.data : JSON.stringify(res.data || {});
  if (code < 200 || code >= 300) {
    const err = new Error(`HTTP ${code}`);
    err.statusCode = code;
    err.body = body;
    throw err;
  }
  return body;
}

function buildQuery(obj) {
  const parts = [];
  Object.keys(obj || {}).forEach((k) => {
    const v = obj[k];
    if (v === undefined || v === null || v === '') return;
    parts.push(`${encodeURIComponent(k)}=${encodeURIComponent(String(v))}`);
  });
  return parts.length ? `?${parts.join('&')}` : '';
}

module.exports = {
  request,
  requestJSON,
  requestText,
  buildQuery,
  setCookieRaw,
  getCookieRaw,
  BASE_URL
};
