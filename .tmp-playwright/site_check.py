from pathlib import Path
import json
import time
from playwright.sync_api import sync_playwright

TARGETS = [
    {"name": "internal", "base_url": "http://192.168.1.3"},
    {"name": "external", "base_url": "https://fish.530555.xyz"},
]

USER = "admin"
PASS = "admin69c7685f"

LOGIN_PATHS = ["/login", "/login.html", "/"]

PAGE_CHECKS = [
    {"key": "home", "path": "/", "must_visible": [".topbar", ".layout", "#cfgPanel", "#logPanel"]},
    {"key": "rules", "path": "/rules", "must_visible": [".top", ".card", "#dailyList", "#cycleList", "#ldList"]},
    {"key": "config", "path": "/config", "must_visible": [".top", ".card", "#statusLine", "#userBody", "#devBody"]},
    {"key": "replay", "path": "/replay", "must_visible": [".top", ".card", "#tbody", "#chart"]},
]

ROOT = Path.cwd() / ".tmp-playwright"
ART = ROOT / "artifacts"
ART.mkdir(parents=True, exist_ok=True)


def safe_screenshot(page, path):
    try:
        page.screenshot(path=str(path), full_page=True, timeout=5000, animations="disabled")
    except Exception:
        pass


def has_login_form(page):
    try:
        return page.locator("#u").count() > 0 and page.locator("#p").count() > 0 and page.locator("#btn").count() > 0
    except Exception:
        return False


def goto_login(page, base_url):
    attempts = []
    for p in LOGIN_PATHS:
        url = f"{base_url}{p}"
        try:
            page.goto(url, wait_until="domcontentloaded", timeout=25000)
            time.sleep(0.5)
            if has_login_form(page):
                return url, attempts
            attempts.append({"url": url, "ok": False, "reason": "no_login_form"})
        except Exception as e:
            attempts.append({"url": url, "ok": False, "reason": str(e)})
    return None, attempts


def run_target(browser, target):
    context = browser.new_context(viewport={"width": 1440, "height": 900}, ignore_https_errors=True)
    page = context.new_page()

    console_errors = []
    page_errors = []
    request_failures = []

    page.on("console", lambda msg: console_errors.append({"text": msg.text, "url": page.url}) if msg.type == "error" else None)
    page.on("pageerror", lambda err: page_errors.append({"message": str(err), "url": page.url}))
    page.on("requestfailed", lambda req: request_failures.append({
        "url": req.url,
        "method": req.method,
        "resource_type": req.resource_type,
        "failure": str(req.failure),
    }))

    result = {
        "target": target["name"],
        "base_url": target["base_url"],
        "login_ok": False,
        "login_error": None,
        "login_attempts": [],
        "pages": [],
        "console_errors": console_errors,
        "page_errors": page_errors,
        "request_failures": request_failures,
    }

    login_url, attempts = goto_login(page, target["base_url"])
    result["login_attempts"] = attempts
    if not login_url:
        result["login_error"] = "No valid login page with #u/#p/#btn found on /login, /login.html, /."
        safe_screenshot(page, ART / f"{target['name']}-login-failed.png")
        context.close()
        return result

    try:
        page.fill("#u", USER, timeout=8000)
        page.fill("#p", PASS, timeout=8000)
        page.click("#btn")
        time.sleep(2.5)

        if has_login_form(page):
            msg = ""
            try:
                m = page.locator("#msg")
                if m.count() > 0 and m.first.is_visible():
                    msg = (m.first.inner_text() or "").strip()
            except Exception:
                pass
            if not msg:
                msg = f"still on login page: {page.url}"
            raise RuntimeError(msg)

        result["login_ok"] = True
    except Exception as e:
        result["login_error"] = str(e)
        safe_screenshot(page, ART / f"{target['name']}-login-failed.png")
        context.close()
        return result

    for p in PAGE_CHECKS:
        item = {
            "key": p["key"],
            "path": p["path"],
            "ok": False,
            "url": None,
            "title": "",
            "body_text_length": 0,
            "visible_checks": [],
            "error": None,
            "screenshot": None,
        }

        try:
            page.goto(f"{target['base_url']}{p['path']}", wait_until="domcontentloaded", timeout=30000)
            time.sleep(1.5)
            item["url"] = page.url
            item["title"] = page.title()
            body_text = page.locator("body").inner_text().strip()
            item["body_text_length"] = len(body_text)

            all_visible = True
            for sel in p["must_visible"]:
                vis = False
                try:
                    vis = page.locator(sel).first.is_visible(timeout=7000)
                except Exception:
                    vis = False
                item["visible_checks"].append({"selector": sel, "visible": vis})
                all_visible = all_visible and vis

            item["ok"] = all_visible and item["body_text_length"] > 80
            shot = ART / f"{target['name']}-{p['key']}.png"
            safe_screenshot(page, shot)
            item["screenshot"] = str(shot)
        except Exception as e:
            item["error"] = str(e)
            shot = ART / f"{target['name']}-{p['key']}-error.png"
            item["screenshot"] = str(shot)
            safe_screenshot(page, shot)

        result["pages"].append(item)

    context.close()
    return result


def main():
    with sync_playwright() as p:
        browser = None
        try:
            browser = p.chromium.launch(headless=True, channel="msedge")
        except Exception:
            browser = p.chromium.launch(headless=True)

        all_results = []
        for t in TARGETS:
            all_results.append(run_target(browser, t))

        browser.close()

    out = ROOT / "result.json"
    out.write_text(json.dumps({"generated_at": time.strftime("%Y-%m-%dT%H:%M:%S"), "all": all_results}, ensure_ascii=False, indent=2), encoding="utf-8")
    print(str(out))


if __name__ == "__main__":
    main()
