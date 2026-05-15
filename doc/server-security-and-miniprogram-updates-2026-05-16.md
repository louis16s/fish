# Server Security and Mini Program Updates - 2026-05-16

## Summary

This record tracks the security, audit, deployment, migration, and WeChat Mini Program compatibility changes made around 2026-05-15/16.

## Server Code Changes

- Added `server/package-lock.json` and changed Docker build to `npm ci --omit=dev --ignore-scripts` for reproducible dependency installs.
- Pinned EMQX Compose image to `emqx:5.8.3` instead of floating `emqx/emqx:5`.
- Added `WECHAT_APPID` to `openclaw/.env.example` and Compose environment passthrough.
- Added deployment verification script: `openclaw/deploy_check.sh`.
- Added migration checklist: `openclaw/MIGRATION_CHECKLIST.md`.
- Expanded `/healthz` with service metadata, uptime, DB status/latency, and MQTT status.
- Added `/livez` for process liveness checks.
- Added `/.well-known/security.txt`.
- Added stricter production security headers:
  - CSP constrained to self-hosted resources while keeping current inline handlers compatible.
  - `Permissions-Policy`.
  - `X-Robots-Tag`.
  - HSTS when secure cookies are enabled.
- Changed secure session cookie name to `__Host-fish_sid` when `COOKIE_SECURE=1`.
- Added JSON request validation and consistent JSON body error responses.
- Added production startup checks for placeholder `SESSION_SECRET` and `ADMIN_PASSWORD`.
- Added login audit table `login_records`.
- Added login audit API: `GET /api/admin/login-records`.
- Added login attempt recording for successful login, bad credentials, disabled users, and missing credentials.
- Changed `/api/cmd` authorization from admin-only to any authenticated user.
- Kept admin-only restrictions for user management, device management, settings, rule/config writes, and log clearing/download.
- Added WeChat Mini Program same-origin exception for `WECHAT_APPID=wx36b1be8c3e24b689`.
  - Allows `Referer: https://servicewechat.com/wx36b1be8c3e24b689/...`.
  - Allows the Mini Program request even when `Sec-Fetch-Site: cross-site` is present.
  - Still rejects other AppIDs and ordinary cross-site requests.

## Mini Program Changes

- Added fallback entry page `pages/index/index` and made it the first `app.json` page.
  - It redirects logged-in users to `pages/panel/index`.
  - It redirects unauthenticated users to `pages/login/index`.
- Updated panel control permissions:
  - Logged-in users can operate gate commands.
  - Controls are disabled only when device/MQTT state is unavailable or a command is in flight.
- Updated admin status page to read both old and new `/healthz` formats.

## Deployment Notes

- Server host: `67.209.185.215`.
- Docker Compose project path: `/root/openclaw/openclaw`.
- Server source path: `/root/openclaw/server`.
- Service container: `openclaw-fish-panel-1`.
- Backups created during deployment:
  - `/root/openclaw/backups/server-security-20260515-221212`
  - `/root/openclaw/backups/pre-github-deploy-20260516-003129`
  - `/root/openclaw/backups/wechat-origin-fix-20260516-010040`
  - `/root/openclaw/backups/wechat-origin-fix2-20260516-010624`

## Operational Changes

- User `test` password was reset in the production database to `testwxwx`.
- Password reset was done by hashing the new password with `bcryptjs` inside the application container and updating only `users.password_hash`.
- No plaintext password is stored in code or in the database.

## Verification Performed

- Node syntax checks passed for changed server files.
- Mini Program JavaScript syntax checks passed.
- `server/package.json` and `server/package-lock.json` parsed successfully.
- Production `npm install --package-lock-only --ignore-scripts` reported 0 vulnerabilities when the lockfile was generated.
- `/healthz` returned OK with DB and MQTT healthy.
- Simulated WeChat Mini Program login request for AppID `wx36b1be8c3e24b689` no longer returns `bad_origin`.
- Simulated request with a wrong AppID still returns `bad_origin`.
- `test / testwxwx` login returned `{"ok":true}`.
