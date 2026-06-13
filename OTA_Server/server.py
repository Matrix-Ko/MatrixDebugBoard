"""
MatrixDebugBoard OTA Server
Lightweight Flask-based firmware update server for ESP32-S3.
"""

import json
import os
import sys
import logging
import argparse
from datetime import datetime
from flask import Flask, jsonify, send_file, abort, request, Response

# ── Paths ──────────────────────────────────────────────────────────────────────
BASE_DIR     = os.path.dirname(os.path.abspath(__file__))
CONFIG_FILE  = os.path.join(BASE_DIR, "config.json")
FIRMWARE_DIR = os.path.join(BASE_DIR, "firmware")

# ── Logging ────────────────────────────────────────────────────────────────────
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
log = logging.getLogger("ota-server")

# ── Flask app ──────────────────────────────────────────────────────────────────
app = Flask(__name__)
app.config["JSON_SORT_KEYS"] = False


def load_config() -> dict:
    """Load config.json, raise on missing or invalid file."""
    if not os.path.isfile(CONFIG_FILE):
        log.error("config.json not found: %s", CONFIG_FILE)
        raise FileNotFoundError("config.json missing")
    with open(CONFIG_FILE, "r", encoding="utf-8") as f:
        return json.load(f)


def firmware_path(filename: str) -> str:
    return os.path.join(FIRMWARE_DIR, filename)


def log_request(extra: str = ""):
    log.info("%s %s  client=%s  %s", request.method, request.path,
             request.remote_addr, extra)


# ── Routes ─────────────────────────────────────────────────────────────────────

@app.route("/manifest.json")
def manifest():
    """
    Return the current firmware manifest.
    The ESP32 fetches this to compare version numbers.
    """
    cfg = load_config()
    host = request.host          # e.g. "192.168.1.10:8080"
    fw_url = f"http://{host}/firmware/{cfg['filename']}"

    payload = {
        "version": cfg["version"],
        "url":     fw_url,
        "notes":   cfg.get("notes", ""),
    }
    log_request(f"version={cfg['version']}")
    return jsonify(payload)


@app.route("/firmware/<filename>")
def firmware(filename: str):
    """
    Serve the firmware binary.
    Content-Length is set so the ESP32 can track download progress.
    """
    # Prevent path traversal
    safe_name = os.path.basename(filename)
    path = firmware_path(safe_name)

    if not os.path.isfile(path):
        log.warning("Firmware not found: %s", path)
        abort(404, description=f"Firmware file '{safe_name}' not found")

    size = os.path.getsize(path)
    log_request(f"file={safe_name}  size={size:,} bytes")

    response = send_file(path, mimetype="application/octet-stream",
                         as_attachment=True, download_name=safe_name)
    response.headers["Content-Length"] = size
    return response


@app.route("/status")
def status():
    """Server status — use this to verify the server is reachable."""
    cfg = load_config()
    path = firmware_path(cfg["filename"])
    fw_size = os.path.getsize(path) if os.path.isfile(path) else -1
    host = request.host

    return jsonify({
        "server":        "MatrixDebugBoard OTA Server",
        "status":        "running",
        "version":       cfg["version"],
        "firmware":      cfg["filename"],
        "firmware_size": fw_size,
        "notes":         cfg.get("notes", ""),
        "manifest_url":  f"http://{host}/manifest.json",
        "firmware_url":  f"http://{host}/firmware/{cfg['filename']}",
        "time":          datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
    })


@app.route("/upload", methods=["POST"])
def upload():
    """
    Upload a new firmware binary.
    Usage:
        curl -X POST http://<server>/upload \
             -F "firmware=@matrixdebugboard.bin" \
             -F "version=1.2.0" \
             -F "notes=Description"
    After upload, config.json is updated automatically.
    """
    if "firmware" not in request.files:
        return jsonify({"error": "No firmware file in request"}), 400

    file    = request.files["firmware"]
    version = request.form.get("version", "").strip()
    notes   = request.form.get("notes", "").strip()

    if not version:
        return jsonify({"error": "version field is required"}), 400
    if not file.filename.endswith(".bin"):
        return jsonify({"error": "Only .bin files are accepted"}), 400

    os.makedirs(FIRMWARE_DIR, exist_ok=True)
    filename = os.path.basename(file.filename)
    save_path = firmware_path(filename)
    file.save(save_path)
    size = os.path.getsize(save_path)

    # Update config.json
    cfg = load_config() if os.path.isfile(CONFIG_FILE) else {}
    cfg["version"]  = version
    cfg["filename"] = filename
    cfg["notes"]    = notes
    with open(CONFIG_FILE, "w", encoding="utf-8") as f:
        json.dump(cfg, f, ensure_ascii=False, indent=2)

    log.info("Firmware uploaded: %s  version=%s  size=%d bytes", filename, version, size)
    return jsonify({
        "status":   "ok",
        "version":  version,
        "filename": filename,
        "size":     size,
    })


@app.errorhandler(404)
def not_found(e):
    return jsonify({"error": str(e)}), 404


@app.errorhandler(500)
def server_error(e):
    return jsonify({"error": str(e)}), 500


# ── Entry point ────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="MatrixDebugBoard OTA Server")
    parser.add_argument("--host", default="0.0.0.0",
                        help="Bind address (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=8080,
                        help="Port (default: 8080)")
    args = parser.parse_args()

    os.makedirs(FIRMWARE_DIR, exist_ok=True)

    # Validate config on startup
    try:
        cfg = load_config()
        path = firmware_path(cfg["filename"])
        fw_ok = os.path.isfile(path)
        log.info("Config loaded — version=%s  firmware=%s  file_exists=%s",
                 cfg["version"], cfg["filename"], fw_ok)
        if not fw_ok:
            log.warning("Firmware file not found in firmware/ — "
                        "place the .bin file before clients connect")
    except FileNotFoundError:
        log.warning("config.json missing — create it before clients connect")

    log.info("OTA Server starting on http://%s:%d", args.host, args.port)
    log.info("Manifest : http://<your-ip>:%d/manifest.json", args.port)
    log.info("Status   : http://<your-ip>:%d/status", args.port)

    app.run(host=args.host, port=args.port, debug=False, threaded=True)


if __name__ == "__main__":
    main()
