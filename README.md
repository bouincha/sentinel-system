# Sentinel System

Autonomous intrusion detection that combines **IoT**, **computer vision** and **workflow automation**.

When motion is detected (ESP32 + PIR sensor, or a manual API call), the system captures a frame, runs a **YOLOv8** person detector locally, uploads the annotated picture to **Cloudinary**, and then reacts on three channels at once: a **Gmail** HTML alert, a row in **Google Sheets**, and a physical alarm (LED + buzzer) through **MQTT**.

## How it works

```
 ESP32 + PIR (MQTT)          Manual test (Webhook /postman)
          \                          /
           v                        v
        1. Trigger  ->  2. motion === true ?
                              |
                              v
        3. HTTP GET  https://<your-tunnel>.loca.lt/test-detect
              Flask + YOLOv8 + Cloudinary (runs on your PC)
                              |
                              v
        4. Normalize JSON (confidence 0.79 -> "79%")
                              |
                              v
        5. person_detected === true ?
              |               |                |
              v               v                v
        Google Sheets    MQTT publish      Gmail HTML alert
         (history)      (ALARM_ON)         (with the image)
```

Example response of the detection server:

```json
{
  "success": true,
  "person_detected": true,
  "confidence": 0.79,
  "timestamp": "2026-09-07 00:17:46",
  "message": "Person detected!",
  "image_url": "https://res.cloudinary.com/<cloud>/image/upload/.../sentinel_detections/xxxx.jpg"
}
```

## Repository layout

```
sentinel-system/
├── server/                  Flask + YOLOv8 detection server (Python)
│   ├── app.py
│   ├── requirements.txt
│   └── .env.example
├── firmware/                ESP32 firmware (PlatformIO)
│   ├── platformio.ini
│   ├── include/secrets.h.example
│   └── src/main.cpp
└── workflow/
    └── sentinel-workflow.json   Automation workflow (credentials removed)
```

## Requirements

- Python 3.9+ and a webcam
- Node.js (for LocalTunnel)
- A free [Cloudinary](https://cloudinary.com) account
- A Google account (Sheets + Gmail)
- A workflow tool able to import `workflow/sentinel-workflow.json` (the node-based editor used for this project). Any tool that offers MQTT, HTTP, Google Sheets and Gmail nodes can reproduce the same logic from the diagram above.
- Optional hardware: ESP32 DevKit, HC-SR501 PIR sensor, LED, buzzer, two push buttons

## Quick start

### 1. Run the detection server

```bash
git clone https://github.com/bouincha/sentinel-system.git
cd sentinel-system/server

python -m venv .venv
source .venv/bin/activate        # Windows: .venv\Scripts\activate
pip install -r requirements.txt

cp .env.example .env             # Windows: copy .env.example .env
# edit .env and fill in your Cloudinary credentials

python app.py
```

The YOLOv8 weights (`yolov8n.pt`) are downloaded automatically on the first run.

### 2. Expose the server with LocalTunnel

In a **second terminal**:

```bash
npm install -g localtunnel
lt --port 5000
```

LocalTunnel prints a public URL such as `https://huge-rats-hope.loca.lt`. The sub-domain is random and changes every time you restart the tunnel.

### 3. Test it in the browser

Open the URL, then add `/test-detect` at the end:

```
https://huge-rats-hope.loca.lt/test-detect
```

The first visit shows a LocalTunnel reminder page: click through it (it may ask for the tunnel password, which is the public IP of the machine running the tunnel). You should now see the JSON response and a new image in your Cloudinary `sentinel_detections` folder.

### 4. Import the workflow

1. Import `workflow/sentinel-workflow.json` into your automation tool.
2. In the **HTTP Request** node, paste the **full** tunnel URL including `/test-detect`.
3. Create your own Google Sheet with this header row (sheet name `Sheet1`):

   | A | B | C | D |
   |---|---|---|---|
   | Timestamp | Event Status | Confidence Level | Image URL |

4. In the **history** (Google Sheets) node, connect your own Google account and set `spreadsheetId`.
5. In the **Gmail1** node, connect your own Google account and set the recipient address.
6. In the **MQTT Trigger** and **MQTT Publish** nodes, replace `YOUR_UNIQUE_ID` with the same random string you use in the firmware (see step 5).
7. Start the workflow. To test without hardware, send a request to the webhook (`/postman`):

   ```json
   { "event": "MOTION_DETECTED", "device_id": "sentinel_01", "motion": true, "date": "2026-09-07", "time": "00:17:46" }
   ```

> Every user must connect **their own** Google credentials. The exported workflow intentionally contains placeholders only (`YOUR_...`).

### 5. Flash the ESP32 (optional)

```bash
cd firmware
cp include/secrets.h.example include/secrets.h
# edit secrets.h: Wi-Fi name/password and a unique TOPIC_PREFIX
pio run -t upload
pio device monitor
```

**Wiring**

| Component | ESP32 pin | Notes |
|---|---|---|
| PIR sensor (HC-SR501) OUT | GPIO 4 | |
| LED | GPIO 33 | add a resistor |
| Buzzer | GPIO 26 | |
| Manual OFF button | GPIO 35 | input-only pin |
| Manual trigger button | GPIO 34 | input-only pin |

GPIO 34 and 35 have no internal pull resistors: wire each button with an external pull-down (about 10 kΩ to GND).

Behaviour: motion must stay active for 5 seconds to be confirmed, then an event is published on `<TOPIC_PREFIX>/events` (10 seconds cooldown). The workflow answers on `<TOPIC_PREFIX>/commands` with `ALARM_ON`; the OFF button silences the alarm. Supported commands: `ALARM_ON`, `ALARM_OFF`, `LED_ON`, `LED_OFF`.

## Configuration

Server (`server/.env`):

| Variable | Required | Description |
|---|---|---|
| `CLOUDINARY_CLOUD_NAME` | yes | Cloudinary cloud name |
| `CLOUDINARY_API_KEY` | yes | Cloudinary API key |
| `CLOUDINARY_API_SECRET` | yes | Cloudinary API secret |
| `API_KEY` | no | If set, requests must send the header `X-API-Key: <value>` |
| `CAMERA_INDEX` | no | Camera index for OpenCV (default `0`) |
| `HOST` | no | Bind address (default `127.0.0.1`, enough for LocalTunnel) |
| `PORT` | no | Port (default `5000`) |

If you set `API_KEY`, add the same `X-API-Key` header to the **HTTP Request** node of the workflow.

## Security notes

- **Never commit** `.env`, `secrets.h` or any exported workflow that still contains credentials. If a secret ever leaks, rotate it immediately.
- `/test-detect` triggers the camera and an upload. Anyone who knows your tunnel URL can call it: set `API_KEY` and stop the tunnel when you do not need it.
- The default MQTT broker (`broker.hivemq.com`) is **public and unauthenticated**. Always use a long random `TOPIC_PREFIX`, otherwise other people can trigger your alarm or spoof events. For anything serious, use a private broker with a username/password and TLS.
- LocalTunnel is meant for prototypes. See the roadmap for production-grade options.

## Troubleshooting

| Symptom | Fix |
|---|---|
| The HTTP Request node receives HTML instead of JSON | LocalTunnel reminder page: add the header `bypass-tunnel-reminder: true` to the node |
| `Failed to grab frame` | Wrong `CAMERA_INDEX`, or the camera is used by another application |
| Server exits with `Missing environment variables` | Create `server/.env` from `.env.example` |
| Tunnel URL stopped working | Restart `lt --port 5000` and update the URL in the workflow |
| ESP32 never triggers the workflow | Check that `TOPIC_PREFIX` is identical in `secrets.h` and in both MQTT nodes |

## Roadmap

- Replace LocalTunnel with Cloudflare Tunnel or ngrok (stable authenticated URL)
- Containerize the server with Docker (Raspberry Pi friendly)
- Face recognition whitelist (authorized people vs intruders)
- Multi-class detection (fire/smoke, vehicles, animals)
- ESP32-CAM or RTSP IP camera instead of the PC webcam
- Faster edge inference (YOLOv8-nano, TensorRT)
- Push notifications (Telegram / WhatsApp / Firebase)
- PostgreSQL or MongoDB for multi-camera history and dashboards

## License

MIT, see [LICENSE](LICENSE).
