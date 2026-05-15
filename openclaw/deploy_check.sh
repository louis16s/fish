#!/usr/bin/env sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
COMPOSE_DIR="$ROOT_DIR/openclaw"
SERVER_DIR="$ROOT_DIR/server"
BASE_URL="${BASE_URL:-https://fish.530555.xyz}"
WECHAT_APPID="${WECHAT_APPID:-wx36b1be8c3e24b689}"

cd "$COMPOSE_DIR"

fail() {
  echo "ERROR: $*" >&2
  exit 1
}

need_file() {
  [ -f "$1" ] || fail "missing file: $1"
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || fail "missing command: $1"
}

require_env_key() {
  key="$1"
  if ! grep -q "^${key}=" .env; then
    fail "missing ${key} in openclaw/.env"
  fi
  val="$(sed -n "s/^${key}=//p" .env | tail -1)"
  case "$val" in
    ""|CHANGE_ME*|changeme*|default|password)
      fail "unsafe placeholder value for ${key}"
      ;;
  esac
}

echo "== Preflight =="
need_cmd docker
need_file "$COMPOSE_DIR/.env"
need_file "$COMPOSE_DIR/docker-compose.yml"
need_file "$SERVER_DIR/package.json"
need_file "$SERVER_DIR/package-lock.json"
need_file "$SERVER_DIR/src/index.js"

require_env_key POSTGRES_PASSWORD
require_env_key SESSION_SECRET
require_env_key ADMIN_PASSWORD
require_env_key MQTT_SERVER_USERNAME
require_env_key MQTT_SERVER_PASSWORD
require_env_key DEFAULT_DEVICE_ID

echo "== Syntax check =="
docker run --rm -v "$SERVER_DIR:/app" -w /app node:20-alpine \
  sh -c "node --check src/index.js && node --check src/db.js && node --check src/config.js"

echo "== Build =="
docker compose build fish-panel

echo "== Start =="
docker compose up -d postgres emqx fish-panel caddy

echo "== Container status =="
docker compose ps

echo "== Health check =="
tmp="$(mktemp)"
code="$(curl -ksS -o "$tmp" -w "%{http_code}" "$BASE_URL/healthz")"
cat "$tmp"
echo
[ "$code" = "200" ] || fail "healthz returned HTTP $code"
grep -q '"ok":true' "$tmp" || fail "healthz did not report ok=true"
rm -f "$tmp"

echo "== WeChat origin check =="
tmp="$(mktemp)"
code="$(curl -ksS -o "$tmp" -w "%{http_code}" \
  -X POST "$BASE_URL/api/auth/login" \
  -H "Content-Type: application/json" \
  -H "Referer: https://servicewechat.com/${WECHAT_APPID}/devtools/page-frame.html" \
  -H "Sec-Fetch-Site: cross-site" \
  --data '{"username":"__deploy_check__","password":"__deploy_check__"}')"
cat "$tmp"
echo
if [ "$code" = "403" ] && grep -q 'bad_origin' "$tmp"; then
  rm -f "$tmp"
  fail "WeChat Mini Program origin is still blocked"
fi
rm -f "$tmp"

echo "== Bad origin check =="
tmp="$(mktemp)"
code="$(curl -ksS -o "$tmp" -w "%{http_code}" \
  -X POST "$BASE_URL/api/auth/login" \
  -H "Content-Type: application/json" \
  -H "Referer: https://evil.example/x" \
  --data '{"username":"__deploy_check__","password":"__deploy_check__"}')"
cat "$tmp"
echo
[ "$code" = "403" ] || fail "bad origin was not blocked; HTTP $code"
grep -q 'bad_origin' "$tmp" || fail "bad origin did not return bad_origin"
rm -f "$tmp"

echo "Deployment check passed."
