"""Sentinel System - detection server.

GET /test-detect grabs a frame from the webcam, runs YOLOv8 (class 0 = person),
uploads the annotated image to Cloudinary and returns a JSON report.
"""
import os
import tempfile
import threading
from datetime import datetime

import cloudinary
import cloudinary.uploader
import cv2
from dotenv import load_dotenv
from flask import Flask, jsonify, request
from ultralytics import YOLO

load_dotenv()

REQUIRED_VARS = ("CLOUDINARY_CLOUD_NAME", "CLOUDINARY_API_KEY", "CLOUDINARY_API_SECRET")
missing = [name for name in REQUIRED_VARS if not os.getenv(name)]
if missing:
    raise SystemExit(
        "Missing environment variables: "
        + ", ".join(missing)
        + ". Copy .env.example to .env and fill it in."
    )

API_KEY = os.getenv("API_KEY")
CAMERA_INDEX = int(os.getenv("CAMERA_INDEX", "0"))
HOST = os.getenv("HOST", "127.0.0.1")
PORT = int(os.getenv("PORT", "5000"))

cloudinary.config(
    cloud_name=os.getenv("CLOUDINARY_CLOUD_NAME"),
    api_key=os.getenv("CLOUDINARY_API_KEY"),
    api_secret=os.getenv("CLOUDINARY_API_SECRET"),
    secure=True,
)

app = Flask(__name__)

print("Loading YOLOv8 model...")
model = YOLO("yolov8n.pt")  # downloaded automatically on first run

print("Opening camera...")
camera = cv2.VideoCapture(CAMERA_INDEX)
camera_lock = threading.Lock()


def grab_frame():
    """Return (ok, frame). The first read flushes a stale buffered frame."""
    with camera_lock:
        camera.read()
        return camera.read()


@app.route("/health", methods=["GET"])
def health():
    return jsonify({"status": "ok"})


@app.route("/test-detect", methods=["GET"])
def test_detect():
    if API_KEY and request.headers.get("X-API-Key") != API_KEY:
        return jsonify({"success": False, "error": "Unauthorized"}), 401

    try:
        ok, frame = grab_frame()
        if not ok:
            return jsonify({"success": False, "error": "Failed to grab frame"}), 500

        capture_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        results = model(frame, classes=[0], verbose=False)
        confidences = [float(box.conf[0]) for r in results for box in r.boxes]
        person_detected = bool(confidences)
        confidence = max(confidences, default=0.0)

        annotated_frame = results[0].plot()
        with tempfile.TemporaryDirectory() as tmp_dir:
            path = os.path.join(tmp_dir, "detection.jpg")
            cv2.imwrite(path, annotated_frame)
            upload = cloudinary.uploader.upload(path, folder="sentinel_detections")

        return jsonify(
            {
                "success": True,
                "person_detected": person_detected,
                "confidence": round(confidence, 2),
                "timestamp": capture_time,
                "message": "Person detected!" if person_detected else "No person detected.",
                "image_url": upload["secure_url"],
            }
        )
    except Exception:
        app.logger.exception("Detection failed")
        return jsonify({"success": False, "error": "Internal server error"}), 500


if __name__ == "__main__":
    app.run(host=HOST, port=PORT, debug=False)
