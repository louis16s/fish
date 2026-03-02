from playwright.sync_api import sync_playwright
import time

BASE='https://fish.530555.xyz'
USER='admin'
PASS='admin69c7685f'

with sync_playwright() as p:
    browser = p.chromium.launch(headless=True, channel='msedge', args=['--disable-blink-features=AutomationControlled'])
    context = browser.new_context(
        viewport={"width":1440,"height":900},
        ignore_https_errors=True,
        user_agent='Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36 Edg/133.0.0.0'
    )
    page = context.new_page()

    for u in [BASE+'/login', BASE+'/', BASE+'/login?next=%2F']:
        try:
            page.goto(u, wait_until='commit', timeout=30000)
            time.sleep(2)
            print('URL', u, '->', page.url)
            print('TITLE', page.title())
            print('HAS #u', page.locator('#u').count(), 'HAS #p', page.locator('#p').count(), 'HAS #btn', page.locator('#btn').count())
            page.screenshot(path='d:/users/code/fish-github/.tmp-playwright/artifacts/external-probe.png', full_page=True, timeout=5000)
        except Exception as e:
            print('ERR', u, str(e))

    if page.locator('#u').count()>0 and page.locator('#p').count()>0 and page.locator('#btn').count()>0:
        page.fill('#u', USER)
        page.fill('#p', PASS)
        page.click('#btn')
        time.sleep(3)
        print('AFTER LOGIN URL', page.url)
        print('STILL LOGIN FORM', page.locator('#u').count())
        if page.locator('#msg').count()>0:
            try:
                print('MSG', page.locator('#msg').inner_text())
            except Exception:
                pass
        page.screenshot(path='d:/users/code/fish-github/.tmp-playwright/artifacts/external-after-login.png', full_page=True, timeout=5000)

    context.close()
    browser.close()
