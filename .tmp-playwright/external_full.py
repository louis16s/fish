from pathlib import Path
import json
import time
from playwright.sync_api import sync_playwright

BASE='https://fish.530555.xyz'
USER='admin'
PASS='admin69c7685f'
OUT=Path('d:/users/code/fish-github/.tmp-playwright')
ART=OUT/'artifacts'
ART.mkdir(parents=True, exist_ok=True)

checks=[
    {'key':'home','path':'/','must':['.topbar','.layout','#cfgPanel','#logPanel']},
    {'key':'rules','path':'/rules','must':['.top','.card','#dailyList','#cycleList','#ldList']},
    {'key':'config','path':'/config','must':['.top','.card','#statusLine','#userBody','#devBody']},
    {'key':'replay','path':'/replay','must':['.top','.card','#tbody','#chart']},
]

result={'target':'external','base_url':BASE,'login_ok':False,'login_error':None,'pages':[],'console_errors':[],'page_errors':[],'request_failures':[]}

with sync_playwright() as p:
    browser = p.chromium.launch(headless=True, channel='msedge', args=['--disable-blink-features=AutomationControlled'])
    context = browser.new_context(
        viewport={'width':1440,'height':900},
        ignore_https_errors=True,
        user_agent='Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36 Edg/133.0.0.0'
    )
    page=context.new_page()

    page.on('console', lambda m: result['console_errors'].append({'text':m.text,'url':page.url}) if m.type=='error' else None)
    page.on('pageerror', lambda e: result['page_errors'].append({'message':str(e),'url':page.url}))
    page.on('requestfailed', lambda r: result['request_failures'].append({'url':r.url,'method':r.method,'resource_type':r.resource_type,'failure':str(r.failure)}))

    try:
        page.goto(BASE + '/login', wait_until='commit', timeout=30000)
        page.wait_for_selector('#u', timeout=12000)
        page.fill('#u', USER)
        page.fill('#p', PASS)
        page.click('#btn')
        page.wait_for_timeout(3500)
        if page.locator('#u').count() > 0:
            msg=''
            if page.locator('#msg').count()>0:
                try:
                    msg = page.locator('#msg').inner_text().strip()
                except Exception:
                    pass
            raise RuntimeError(msg or 'still on login page')
        result['login_ok']=True
        page.screenshot(path=str(ART/'external-home-after-login.png'), full_page=True, timeout=6000)
    except Exception as e:
        result['login_error']=str(e)
        try:
            page.screenshot(path=str(ART/'external-login-final-failed.png'), full_page=True, timeout=6000)
        except Exception:
            pass

    if result['login_ok']:
        for c in checks:
            item={'key':c['key'],'path':c['path'],'ok':False,'url':None,'title':'','body_text_length':0,'visible_checks':[],'error':None,'screenshot':None}
            try:
                page.goto(BASE + c['path'], wait_until='domcontentloaded', timeout=30000)
                page.wait_for_timeout(2000)
                item['url']=page.url
                item['title']=page.title()
                item['body_text_length']=len((page.locator('body').inner_text() or '').strip())
                allv=True
                for s in c['must']:
                    vis=False
                    try:
                        vis=page.locator(s).first.is_visible(timeout=6000)
                    except Exception:
                        vis=False
                    item['visible_checks'].append({'selector':s,'visible':vis})
                    allv = allv and vis
                item['ok']=allv and item['body_text_length']>80
                shot=ART/f"external-{c['key']}-final.png"
                item['screenshot']=str(shot)
                page.screenshot(path=str(shot), full_page=True, timeout=6000)
            except Exception as e:
                item['error']=str(e)
                shot=ART/f"external-{c['key']}-final-error.png"
                item['screenshot']=str(shot)
                try:
                    page.screenshot(path=str(shot), full_page=True, timeout=6000)
                except Exception:
                    pass
            result['pages'].append(item)

    context.close()
    browser.close()

(OUT/'external-final-result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
print(str(OUT/'external-final-result.json'))
