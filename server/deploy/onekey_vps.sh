#!/usr/bin/env bash
set -euo pipefail

# One-key installer for VPS (Debian/Ubuntu).
# - Installs: nginx, mosquitto, nodejs, npm, rsync
# - Deploys panel to /opt/fish-panel (systemd + nginx reverse proxy)
# - Creates Mosquitto auth (fish1 user) if not present
#
# Safe behavior:
# - Does not stop nginx, only reload after config test.
# - Does not overwrite existing mosquitto config file if it already exists.
# - Does not overwrite existing /opt/fish-panel/.env if it already exists.

APP_DST="/opt/fish-panel"
DOMAIN_DEFAULT="fish.530555.xyz"
MOSQ_CONF="/etc/mosquitto/conf.d/fish1.conf"
MOSQ_PASSWD="/etc/mosquitto/passwd"

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$(cd "${HERE}/.." && pwd)"

need_cmd() { command -v "$1" >/dev/null 2>&1; }

if ! need_cmd sudo; then
  echo "sudo not found"
  exit 1
fi

if ! need_cmd apt-get; then
  echo "This script currently supports Debian/Ubuntu (apt-get)."
  exit 1
fi

echo "[1/7] Install packages"
sudo apt-get update
sudo apt-get install -y nginx mosquitto mosquitto-clients rsync nodejs npm curl

echo "[2/7] Setup Mosquitto (if needed)"
sudo mkdir -p /etc/mosquitto/conf.d

if [[ ! -f "${MOSQ_PASSWD}" ]]; then
  echo "Creating Mosquitto password file: ${MOSQ_PASSWD}"
  echo "You will be prompted to set password for user 'fish1'."
  sudo mosquitto_passwd -c "${MOSQ_PASSWD}" fish1
fi

if [[ ! -f "${MOSQ_CONF}" ]]; then
  echo "Writing Mosquitto conf: ${MOSQ_CONF}"
  sudo tee "${MOSQ_CONF}" >/dev/null <<EOF
listener 1883 0.0.0.0
allow_anonymous false
password_file ${MOSQ_PASSWD}
persistence true
EOF
else
  echo "Mosquitto conf exists, skip: ${MOSQ_CONF}"
fi

sudo systemctl enable --now mosquitto
sudo systemctl restart mosquitto

echo "[3/7] Sync panel to ${APP_DST}"
sudo mkdir -p "${APP_DST}"
sudo rsync -a --delete \
  --exclude ".env" \
  --exclude "node_modules" \
  "${SRC_DIR}/" "${APP_DST}/"

echo "[4/7] Ensure panel .env"
if [[ ! -f "${APP_DST}/.env" ]]; then
  sudo cp "${APP_DST}/.env.example" "${APP_DST}/.env"
  echo "Created ${APP_DST}/.env"
  echo "Edit it now:"
  echo "  sudo nano ${APP_DST}/.env"
  echo "Then re-run this script."
  exit 2
fi

DOMAIN="$(grep -E '^PANEL_DOMAIN=' "${APP_DST}/.env" 2>/dev/null | head -n1 | cut -d= -f2- || true)"
if [[ -z "${DOMAIN}" ]]; then
  DOMAIN="${DOMAIN_DEFAULT}"
fi

echo "[5/7] Install node deps"
cd "${APP_DST}"
if [[ -f package-lock.json ]]; then
  sudo npm ci --omit=dev
else
  sudo npm install --omit=dev
fi

echo "[6/7] Install systemd service"
sudo cp "${APP_DST}/deploy/fish-panel.service" /etc/systemd/system/fish-panel.service
sudo systemctl daemon-reload
sudo systemctl enable --now fish-panel
sudo systemctl restart fish-panel

echo "[7/7] Install nginx site"
NGINX_AVAIL="/etc/nginx/sites-available/fish-panel"
NGINX_ENABLED="/etc/nginx/sites-enabled/fish-panel"
NGINX_CONFD="/etc/nginx/conf.d/fish-panel.conf"

if [[ -d /etc/nginx/sites-available && -d /etc/nginx/sites-enabled ]]; then
  sudo tee "${NGINX_AVAIL}" >/dev/null <<EOF
server {
  listen 80;
  server_name ${DOMAIN};

  location / {
    proxy_pass http://127.0.0.1:3000;
    proxy_http_version 1.1;
    proxy_set_header Host \$host;
    proxy_set_header X-Real-IP \$remote_addr;
    proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Proto \$scheme;

    proxy_set_header Upgrade \$http_upgrade;
    proxy_set_header Connection "upgrade";
  }
}
EOF
  sudo ln -sf "${NGINX_AVAIL}" "${NGINX_ENABLED}"
else
  sudo tee "${NGINX_CONFD}" >/dev/null <<EOF
server {
  listen 80;
  server_name ${DOMAIN};

  location / {
    proxy_pass http://127.0.0.1:3000;
    proxy_http_version 1.1;
    proxy_set_header Host \$host;
    proxy_set_header X-Real-IP \$remote_addr;
    proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
    proxy_set_header X-Forwarded-Proto \$scheme;

    proxy_set_header Upgrade \$http_upgrade;
    proxy_set_header Connection "upgrade";
  }
}
EOF
fi

sudo nginx -t
sudo systemctl reload nginx || sudo service nginx reload || true

echo "Health:"
curl -fsS "http://127.0.0.1:3000/healthz" | head -c 400 || true
echo
echo "Done. Open: http://${DOMAIN}/"

