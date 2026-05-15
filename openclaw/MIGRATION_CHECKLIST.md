# Migration Checklist

Use this checklist when deploying to a new server or rebuilding the current server from GitHub.

## 1. Prepare Server

- Install Docker and Docker Compose.
- Open inbound ports:
  - `80/tcp`
  - `443/tcp`
  - `1883/tcp` if devices connect to EMQX directly.
- Clone the repository.
- Copy `openclaw/.env.example` to `openclaw/.env`.

## 2. Fill Required Secrets

Set strong production values in `openclaw/.env`:

- `POSTGRES_PASSWORD`
- `SESSION_SECRET`
- `ADMIN_PASSWORD`
- `MQTT_SERVER_USERNAME`
- `MQTT_SERVER_PASSWORD`
- `WECHAT_APPID`
- `DEFAULT_DEVICE_ID`

Do not use any `CHANGE_ME` placeholder value.

## 3. Data Migration

For an existing production system, migrate these before switching traffic:

- Postgres data or dump:
  - users
  - devices
  - settings
  - telemetry
  - login_records
- EMQX data and users.
- Caddy certificates/data if preserving current TLS state is required.

Example Postgres dump from old server:

```sh
cd /root/openclaw/openclaw
docker compose exec -T postgres pg_dump -U fish -d fish > fish.sql
```

Example restore on new server:

```sh
cd /root/openclaw/openclaw
docker compose up -d postgres
docker compose exec -T postgres psql -U fish -d fish < fish.sql
```

## 4. Deploy

Run:

```sh
cd openclaw
sh deploy_check.sh
```

The script checks required files, required environment values, Node syntax, Docker build, container startup, `/healthz`, WeChat Mini Program origin handling, and bad-origin blocking.

## 5. Verify Manually

- Open `https://fish.530555.xyz/healthz`.
- Log in from the web panel.
- Log in from the WeChat Mini Program.
- Send a harmless command such as stop.
- Check the admin login records page.
- Confirm DB/MQTT are OK.

## 6. Mini Program Release

Website deployment does not publish the Mini Program.

- Open `little_program` in WeChat Developer Tools.
- Compile with the default entry.
- Upload a new version.
- Set it as trial or submit for review/release.
