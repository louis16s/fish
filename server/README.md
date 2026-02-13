# Fish VPS Control Panel

This is a small web panel meant to run on your VPS (fish.530555.xyz).
It talks to the ESP32 device via MQTT (telemetry subscribe + command publish).

## Run (local test)

```bash
cd server
npm i
cp .env.example .env
npm start
```

Then open `http://127.0.0.1:3000/`.

## Env

- `ADMIN_USER`, `ADMIN_PASS`: login credentials
- `SESSION_SECRET`: random long string
- `MQTT_URL`: e.g. `mqtt://127.0.0.1:1883`
- `MQTT_USERNAME`, `MQTT_PASSWORD`
- `MQTT_TELEMETRY_TOPIC`: must match device `MQTT_Pub`
- `MQTT_COMMAND_TOPIC`: must match device `MQTT_Sub`

## Notes

- The ESP32 firmware can also enable basic-auth for the local web panel.
- For production you should put nginx in front (TLS) and proxy to `localhost:3000`.

