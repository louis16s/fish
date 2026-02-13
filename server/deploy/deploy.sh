#!/usr/bin/env bash
set -euo pipefail

APP_NAME="fish-panel"
APP_DST="/opt/fish-panel"
SERVICE_DST="/etc/systemd/system/fish-panel.service"
NGINX_SITE="/etc/nginx/sites-available/fish-panel"
NGINX_LINK="/etc/nginx/sites-enabled/fish-panel"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$(cd "${HERE}/.." && pwd)"

echo "[1/6] Sync app to ${APP_DST}"
sudo mkdir -p "${APP_DST}"
sudo rsync -a --delete \
  --exclude ".env" \
  --exclude "node_modules" \
  "${SRC_DIR}/" "${APP_DST}/"

echo "[2/6] Ensure .env exists"
if [[ ! -f "${APP_DST}/.env" ]]; then
  echo "Missing ${APP_DST}/.env"
  echo "Create it first:"
  echo "  sudo cp ${APP_DST}/.env.example ${APP_DST}/.env"
  echo "  sudo nano ${APP_DST}/.env"
  exit 1
fi

echo "[3/6] Install node deps"
cd "${APP_DST}"
if [[ -f package-lock.json ]]; then
  sudo npm ci --omit=dev
else
  sudo npm install --omit=dev
fi

echo "[4/6] Install systemd service"
sudo cp "${APP_DST}/deploy/fish-panel.service" "${SERVICE_DST}"
sudo systemctl daemon-reload
sudo systemctl enable --now fish-panel
sudo systemctl restart fish-panel

echo "[5/6] Install nginx site (HTTP)"
sudo cp "${APP_DST}/deploy/nginx-fish-panel.conf" "${NGINX_SITE}"
sudo ln -sf "${NGINX_SITE}" "${NGINX_LINK}"
sudo nginx -t
sudo systemctl reload nginx || sudo service nginx reload || true

echo "[6/6] Health check"
curl -fsS "http://127.0.0.1:3000/healthz" | head -c 400 || true
echo
echo "Done."

