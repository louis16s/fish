const fs = require('fs');
const path = require('path');
const { chromium } = require('playwright');

const targets = [
  { name: 'internal', baseUrl: 'http://192.168.1.3' },
  { name: 'external', baseUrl: 'https://fish.530555.xyz' },
];

const USER = 'admin';
const PASS = 'admin69c7685f';

const outDir = path.resolve(process.cwd(), '.tmp-playwright', 'artifacts');
fs.mkdirSync(outDir, { recursive: true });

const pageChecks = [
  {
    key: 'home',
    path: '/',
    mustVisible: ['.topbar', '.layout', '#cfgPanel', '#logPanel'],
  },
  {
    key: 'rules',
    path: '/rules',
    mustVisible: ['.top', '.card', '#dailyList', '#cycleList', '#ldList'],
  },
  {
    key: 'config',
    path: '/config',
    mustVisible: ['.top', '.card', '#statusLine', '#userBody', '#devBody'],
  },
  {
    key: 'replay',
    path: '/replay',
    mustVisible: ['.top', '.card', '#tbody', '#chart'],
  },
];

function sleep(ms){ return new Promise(r => setTimeout(r, ms)); }

async function runTarget(browser, target){
  const context = await browser.newContext({
    viewport: { width: 1440, height: 900 },
    ignoreHTTPSErrors: true,
  });
  const page = await context.newPage();

  const consoleErrors = [];
  const pageErrors = [];
  const requestFailures = [];

  page.on('console', (msg) => {
    if (msg.type() === 'error') {
      consoleErrors.push({ text: msg.text(), url: page.url() });
    }
  });
  page.on('pageerror', (err) => {
    pageErrors.push({ message: String(err && err.message || err), url: page.url() });
  });
  page.on('requestfailed', (req) => {
    requestFailures.push({
      url: req.url(),
      method: req.method(),
      failure: req.failure() ? req.failure().errorText : 'unknown',
      resourceType: req.resourceType(),
    });
  });

  const result = {
    target: target.name,
    baseUrl: target.baseUrl,
    loginOk: false,
    loginError: null,
    pages: [],
    consoleErrors,
    pageErrors,
    requestFailures,
  };

  try {
    await page.goto(`${target.baseUrl}/login`, { waitUntil: 'domcontentloaded', timeout: 20000 });
    await page.fill('#u', USER, { timeout: 8000 });
    await page.fill('#p', PASS, { timeout: 8000 });
    await Promise.all([
      page.waitForURL((url) => !url.pathname.includes('/login'), { timeout: 20000 }),
      page.click('#btn'),
    ]);
    result.loginOk = true;
  } catch (e) {
    result.loginError = String(e && e.message || e);
    await page.screenshot({ path: path.join(outDir, `${target.name}-login-failed.png`), fullPage: true }).catch(() => {});
    await context.close();
    return result;
  }

  for (const p of pageChecks) {
    const item = {
      key: p.key,
      path: p.path,
      ok: false,
      url: null,
      visibleChecks: [],
      bodyTextLength: 0,
      title: '',
      error: null,
      screenshot: null,
    };

    try {
      await page.goto(`${target.baseUrl}${p.path}`, { waitUntil: 'networkidle', timeout: 25000 });
      await sleep(1200);
      item.url = page.url();
      item.title = await page.title();
      item.bodyTextLength = await page.locator('body').innerText().then(t => (t || '').trim().length);

      for (const sel of p.mustVisible) {
        let vis = false;
        try {
          vis = await page.locator(sel).first().isVisible({ timeout: 6000 });
        } catch (_) {
          vis = false;
        }
        item.visibleChecks.push({ selector: sel, visible: vis });
      }

      const allVisible = item.visibleChecks.every(v => v.visible);
      item.ok = allVisible && item.bodyTextLength > 80;
      item.screenshot = path.join(outDir, `${target.name}-${p.key}.png`);
      await page.screenshot({ path: item.screenshot, fullPage: true });
    } catch (e) {
      item.error = String(e && e.message || e);
      item.screenshot = path.join(outDir, `${target.name}-${p.key}-error.png`);
      await page.screenshot({ path: item.screenshot, fullPage: true }).catch(() => {});
    }

    result.pages.push(item);
  }

  await context.close();
  return result;
}

(async () => {
  let browser;
  try {
    browser = await chromium.launch({ headless: true, channel: 'msedge' });
  } catch (e) {
    browser = await chromium.launch({ headless: true });
  }

  const all = [];
  for (const t of targets) {
    all.push(await runTarget(browser, t));
  }

  await browser.close();

  const outFile = path.resolve(process.cwd(), '.tmp-playwright', 'result.json');
  fs.writeFileSync(outFile, JSON.stringify({ generatedAt: new Date().toISOString(), all }, null, 2), 'utf8');
  console.log(outFile);
})();
