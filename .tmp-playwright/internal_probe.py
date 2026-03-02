from playwright.sync_api import sync_playwright
import time

BASE='http://192.168.1.3'
USER='admin'
PASS='admin69c7685f'

with sync_playwright() as p:
    browser = p.chromium.launch(headless=True, channel='msedge', args=['--no-proxy-server','--disable-blink-features=AutomationControlled'])
    context = browser.new_context(viewport={'width':1440,'height':900}, ignore_https_errors=True)
    page = context.new_page()

    for u in [BASE+'/login', BASE+'/', BASE+'/login.html']:
        try:
            page.goto(u, wait_until='commit', timeout=30000)
            page.wait_for_timeout(2000)
            print('URL',u,'->',page.url,'TITLE',page.title(),'u',page.locator('#u').count(),'p',page.locator('#p').count(),'btn',page.locator('#btn').count())
            page.screenshot(path='d:/users/code/fish-github/.tmp-playwright/artifacts/internal-probe.png', full_page=True, timeout=6000)
        except Exception as e:
            print('ERR',u,str(e))

    if page.locator('#u').count()>0 and page.locator('#p').count()>0 and page.locator('#btn').count()>0:
        page.fill('#u', USER)
        page.fill('#p', PASS)
        page.click('#btn')
        page.wait_for_timeout(3500)
        print('AFTER',page.url,'u',page.locator('#u').count())
        if page.locator('#msg').count()>0:
            try:
                print('MSG',page.locator('#msg').inner_text())
            except Exception:
                pass
        page.screenshot(path='d:/users/code/fish-github/.tmp-playwright/artifacts/internal-after-login.png', full_page=True, timeout=6000)

    context.close()
    browser.close()
