from __future__ import annotations

import ipaddress
import json
import os
import re
import shlex
import subprocess
import threading
from pathlib import Path
from typing import Any

from fastapi import FastAPI, HTTPException, Query
from fastapi.responses import FileResponse
from pydantic import BaseModel, Field

APP_ROOT = Path(__file__).resolve().parent
REPO_ROOT = APP_ROOT.parent
DEFAULTS_HPP = REPO_ROOT / "src" / "core" / "defaults.hpp"
DEFAULT_DAQCTL = REPO_ROOT / "build" / "daqctl"
DEFAULT_DAQD = REPO_ROOT / "build" / "daqd"
DEFAULT_WEB_CONFIG = APP_ROOT / "defaults.json"
DAQCTL_TIMEOUT_SEC = 2

_DAQD_LOCK = threading.Lock()
_DAQD_PROC: subprocess.Popen[Any] | None = None


def _load_defaults() -> dict[str, str]:
    try:
        text = DEFAULTS_HPP.read_text(encoding="utf-8")
    except Exception:
        return {}

    pattern = re.compile(
        r"inline\s+constexpr\s+(?:const\s+char\*|uint32_t|int|bool|std::size_t)\s+"
        r"(k[A-Za-z0-9_]+)\s*=\s*(.+?);"
    )

    out: dict[str, str] = {}
    for key, raw_value in pattern.findall(text):
        value = raw_value.strip()
        if value.startswith('"') and value.endswith('"') and len(value) >= 2:
            out[key] = value[1:-1]
            continue
        out[key] = value
    return out


DEFAULTS = _load_defaults()


def _load_web_config() -> dict[str, Any]:
    cfg_path = Path(os.getenv("SIMPLEDAQ_WEB_CONFIG", str(DEFAULT_WEB_CONFIG))).expanduser()
    if not cfg_path.exists():
        return {}
    try:
        loaded = json.loads(cfg_path.read_text(encoding="utf-8"))
    except Exception:
        return {}
    if not isinstance(loaded, dict):
        return {}
    return loaded


WEB_CONFIG = _load_web_config()


def _resolve_repo_path(text: str) -> Path:
    path = Path(text).expanduser()
    if not path.is_absolute():
        path = REPO_ROOT / path
    return path


def _resolve_daqd_log_path() -> Path:
    configured = (
        WEB_CONFIG.get("daqd_log_path")
        or os.getenv("SIMPLEDAQ_DAQD_LOG")
        or DEFAULTS.get("kDaqdLogPath")
        or "logs/daqd.log"
    )
    return _resolve_repo_path(str(configured))


DAQD_LOG_PATH = _resolve_daqd_log_path()


def _run_cmd(args: list[str], timeout_sec: int = 30) -> tuple[int, str, str]:
    try:
        proc = subprocess.run(
            args,
            cwd=str(REPO_ROOT),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=timeout_sec,
        )
        return proc.returncode, proc.stdout.strip(), proc.stderr.strip()
    except subprocess.TimeoutExpired as ex:
        return 124, (ex.stdout or "").strip(), f"timeout after {timeout_sec}s"
    except OSError as ex:
        return 127, "", str(ex)


def _daqctl_base_args() -> list[str]:
    daqctl_path = Path(os.getenv("SIMPLEDAQ_DAQCTL", str(DEFAULT_DAQCTL))).expanduser()
    endpoint = os.getenv("SIMPLEDAQ_CTRL_ENDPOINT") or DEFAULTS.get("kControlEndpoint") or "ipc:///tmp/simpledaq_ctrl.sock"
    return [str(daqctl_path), "--endpoint", endpoint]


def _daqd_base_args() -> list[str]:
    daqd_path_text = WEB_CONFIG.get("daqd_path") or os.getenv("SIMPLEDAQ_DAQD") or str(DEFAULT_DAQD)
    daqd_path = _resolve_repo_path(str(daqd_path_text))
    endpoint = os.getenv("SIMPLEDAQ_CTRL_ENDPOINT") or DEFAULTS.get("kControlEndpoint") or "ipc:///tmp/simpledaq_ctrl.sock"
    status_endpoint = os.getenv("SIMPLEDAQ_STATUS_ENDPOINT") or DEFAULTS.get("kStatusEndpoint") or "ipc:///tmp/simpledaq_status.sock"
    data_endpoint = os.getenv("SIMPLEDAQ_DATA_ENDPOINT") or DEFAULTS.get("kDataEndpoint") or "ipc:///tmp/simpledaq_data.sock"
    return [
        str(daqd_path),
        "--endpoint",
        endpoint,
        "--status-endpoint",
        status_endpoint,
        "--data-endpoint",
        data_endpoint,
        "--log-file",
        str(DAQD_LOG_PATH),
    ]


def _mysql_base_args() -> tuple[list[str], dict[str, str]]:
    host = os.getenv("SIMPLEDAQ_MYSQL_HOST") or DEFAULTS.get("kMySqlHost") or "127.0.0.1"
    port = os.getenv("SIMPLEDAQ_MYSQL_PORT") or DEFAULTS.get("kMySqlPort") or "3306"
    user = os.getenv("SIMPLEDAQ_MYSQL_USER") or DEFAULTS.get("kMySqlUser") or "daq"
    password = os.getenv("SIMPLEDAQ_MYSQL_PASSWORD") or DEFAULTS.get("kMySqlPassword") or "daq"
    database = os.getenv("SIMPLEDAQ_MYSQL_DATABASE") or DEFAULTS.get("kMySqlDatabase") or "daq"

    args = [
        "mysql",
        "--protocol=TCP",
        f"--host={host}",
        f"--port={port}",
        f"--user={user}",
        f"--database={database}",
        "--batch",
        "--skip-column-names",
        "--raw",
    ]

    env = os.environ.copy()
    env["MYSQL_PWD"] = password
    return args, env


def _run_mysql_query(query: str, timeout_sec: int = 30) -> tuple[int, str, str]:
    base_args, env = _mysql_base_args()
    try:
        proc = subprocess.run(
            [*base_args, f"--execute={query}"],
            cwd=str(REPO_ROOT),
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=timeout_sec,
        )
        return proc.returncode, proc.stdout.strip(), proc.stderr.strip()
    except subprocess.TimeoutExpired as ex:
        return 124, (ex.stdout or "").strip(), f"mysql timeout after {timeout_sec}s"
    except OSError as ex:
        return 127, "", str(ex)


def _detect_run_log_schema(table: str) -> tuple[bool, str]:
    query = (
        "SELECT COLUMN_NAME FROM INFORMATION_SCHEMA.COLUMNS "
        f"WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='{table}'"
    )
    rc, out, err = _run_mysql_query(query)
    if rc != 0:
        raise HTTPException(status_code=500, detail={"error": "mysql schema query failed", "stderr": err, "query": query})

    cols = {line.strip() for line in out.splitlines() if line.strip()}
    short_ok = {"run", "subrun", "nevents", "start_time", "end_time", "status", "comment"}.issubset(cols)
    long_ok = {"run_number", "subrun_number", "event_count", "start_time", "end_time", "status", "comment"}.issubset(cols)
    if short_ok:
        return True, ""
    if long_ok:
        return False, ""
    return False, f"unsupported columns in table '{table}': {sorted(cols)}"


def _parse_status_line(line: str) -> dict[str, Any]:
    result: dict[str, Any] = {"raw": line}
    payload = line.strip()
    if payload.lower().startswith("ok "):
        payload = payload[3:]

    for token in payload.split():
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        if value.isdigit():
            result[key] = int(value)
        else:
            result[key] = value
    return result


def _is_daqd_running() -> tuple[bool, int | None]:
    global _DAQD_PROC
    with _DAQD_LOCK:
        if _DAQD_PROC is None:
            return False, None
        rc = _DAQD_PROC.poll()
        if rc is not None:
            _DAQD_PROC = None
            return False, None
        return True, _DAQD_PROC.pid


def _tail_lines(path: Path, limit: int) -> list[str]:
    if limit <= 0 or not path.exists():
        return []
    try:
        with path.open("r", encoding="utf-8", errors="replace") as fh:
            lines = fh.readlines()
    except Exception:
        return []
    return [line.rstrip("\n") for line in lines[-limit:]]


class StartRequest(BaseModel):
    output_dir: str | None = Field(default=None, description="Output directory")
    devices: list[dict[str, Any]] | None = Field(default=None, description="List of device parameter objects")
    events_per_file: int | None = Field(default=None, ge=1)
    reconnect_ms: int | None = Field(default=None, ge=1)
    read_timeout_ms: int | None = Field(default=None, ge=1)
    duration_sec: int | None = Field(default=None, ge=1)
    startup_connect_timeout_sec: int | None = Field(default=None, ge=1)
    reconnect_failure_timeout_sec: int | None = Field(default=None, ge=1)
    comment: str | None = None


app = FastAPI(title="SimpleDAQ Web API", version="0.1.0")


@app.get("/")
def index() -> FileResponse:
    return FileResponse(APP_ROOT / "index.html")


@app.get("/styles.css")
def styles_css() -> FileResponse:
    return FileResponse(APP_ROOT / "styles.css", media_type="text/css")


@app.get("/app.js")
def app_js() -> FileResponse:
    return FileResponse(APP_ROOT / "app.js", media_type="application/javascript")


@app.get("/api/health")
def health() -> dict[str, str]:
    return {"status": "ok"}


@app.post("/api/daqd/start")
def start_daqd() -> dict[str, Any]:
    global _DAQD_PROC

    running, pid = _is_daqd_running()
    if running:
        return {"result": "already running", "pid": pid}

    args = _daqd_base_args()
    DAQD_LOG_PATH.parent.mkdir(parents=True, exist_ok=True)

    with _DAQD_LOCK:
        try:
            with DAQD_LOG_PATH.open("a", encoding="utf-8") as log_head:
                log_head.write("\n=== web requested daqd start ===\n")
            _DAQD_PROC = subprocess.Popen(
                args,
                cwd=str(REPO_ROOT),
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                text=True,
                start_new_session=True,
            )
        except Exception as ex:
            _DAQD_PROC = None
            raise HTTPException(status_code=500, detail={"error": f"failed to start daqd: {ex}", "cmd": shlex.join(args)}) from ex

    return {"result": "daqd started", "pid": _DAQD_PROC.pid}


@app.get("/api/daqd/log")
def get_daqd_log(limit: int = Query(default=50, ge=1, le=500)) -> dict[str, Any]:
    lines = _tail_lines(DAQD_LOG_PATH, limit)
    running, pid = _is_daqd_running()
    return {"running": running, "pid": pid, "lines": lines, "path": str(DAQD_LOG_PATH)}


@app.get("/api/ui-config")
def get_ui_config() -> dict[str, Any]:
    title = str(WEB_CONFIG.get("title", "DAQ Control"))
    default_output_dir = str(WEB_CONFIG.get("output_dir", "./output"))
    events_per_file = WEB_CONFIG.get("events_per_file", WEB_CONFIG.get("events_per_subrun", 100000))
    try:
        events_per_file = int(events_per_file)
    except Exception:
        events_per_file = 100000
    comment = str(WEB_CONFIG.get("comment", "") or "")
    run_log_limit = WEB_CONFIG.get("run_log_limit", 50)
    try:
        run_log_limit = int(run_log_limit)
    except Exception:
        run_log_limit = 50
    daqd_log_limit = WEB_CONFIG.get("daqd_log_limit", 300)
    try:
        daqd_log_limit = int(daqd_log_limit)
    except Exception:
        daqd_log_limit = 300
    startup_connect_timeout_sec = WEB_CONFIG.get(
        "startup_connect_timeout_sec",
        DEFAULTS.get("kStartupConnectTimeoutSec", 5),
    )
    try:
        startup_connect_timeout_sec = int(startup_connect_timeout_sec)
    except Exception:
        startup_connect_timeout_sec = 5
    reconnect_failure_timeout_sec = WEB_CONFIG.get(
        "reconnect_failure_timeout_sec",
        DEFAULTS.get("kReconnectFailureTimeoutSec", 10),
    )
    try:
        reconnect_failure_timeout_sec = int(reconnect_failure_timeout_sec)
    except Exception:
        reconnect_failure_timeout_sec = 10

    devices = WEB_CONFIG.get("devices", [])
    if not isinstance(devices, list):
        devices = []

    return {
        "title": title,
        "output_dir": default_output_dir,
        "events_per_file": events_per_file,
        "comment": comment,
        "run_log_limit": run_log_limit,
        "daqd_log_limit": daqd_log_limit,
        "startup_connect_timeout_sec": startup_connect_timeout_sec,
        "reconnect_failure_timeout_sec": reconnect_failure_timeout_sec,
        "devices": devices,
    }


def _daqctl_request(command: list[str], timeout_sec: int = DAQCTL_TIMEOUT_SEC) -> str:
    rc, out, err = _run_cmd([*_daqctl_base_args(), *command], timeout_sec=timeout_sec)
    if rc != 0:
        raise HTTPException(
            status_code=500,
            detail={"error": "daqctl request failed", "stderr": err, "stdout": out, "cmd": " ".join(command)},
        )
    if out.lower().startswith("error"):
        raise HTTPException(status_code=400, detail={"error": out})
    return out


def _load_frontend_catalog() -> dict[str, Any]:
    out = _daqctl_request(["frontends"], timeout_sec=DAQCTL_TIMEOUT_SEC)
    payload = out[3:].strip() if out.lower().startswith("ok ") else out
    try:
        parsed = json.loads(payload)
    except json.JSONDecodeError as ex:
        raise HTTPException(status_code=500, detail={"error": f"invalid frontends payload: {ex}", "payload": payload}) from ex
    if not isinstance(parsed, dict) or not isinstance(parsed.get("frontends"), list):
        raise HTTPException(status_code=500, detail={"error": "invalid frontends schema structure", "payload": parsed})
    return parsed


def _validate_device_value(field: dict[str, Any], value: Any) -> str:
    field_name = str(field.get("name", ""))
    field_type = str(field.get("type", "string"))

    if field_type == "integer":
        try:
            int_value = int(value)
        except Exception as ex:
            raise HTTPException(status_code=400, detail={"error": f"device field '{field_name}' must be integer"}) from ex
        if "min" in field and int_value < int(field["min"]):
            raise HTTPException(status_code=400, detail={"error": f"device field '{field_name}' must be >= {field['min']}"})
        if "max" in field and int_value > int(field["max"]):
            raise HTTPException(status_code=400, detail={"error": f"device field '{field_name}' must be <= {field['max']}"})
        return str(int_value)

    text_value = str(value).strip()
    if text_value == "":
        raise HTTPException(status_code=400, detail={"error": f"device field '{field_name}' must not be empty"})
    if field_type == "ipv4":
        try:
            ip = ipaddress.ip_address(text_value)
        except ValueError as ex:
            raise HTTPException(status_code=400, detail={"error": f"device field '{field_name}' must be valid IPv4"}) from ex
        if ip.version != 4:
            raise HTTPException(status_code=400, detail={"error": f"device field '{field_name}' must be IPv4"})
    return text_value


def _build_device_spec(device: dict[str, Any], frontend_schema: dict[str, Any]) -> str:
    template = str(frontend_schema.get("spec_template", "")).strip()
    fields = frontend_schema.get("fields", [])
    if template == "" or not isinstance(fields, list):
        raise HTTPException(status_code=500, detail={"error": "invalid frontend schema: missing spec_template/fields"})

    values: dict[str, str] = {}
    for field in fields:
        if not isinstance(field, dict):
            continue
        field_name = str(field.get("name", ""))
        required = bool(field.get("required", True))
        if field_name == "":
            continue
        if required and field_name not in device:
            raise HTTPException(status_code=400, detail={"error": f"device field '{field_name}' is required"})
        if field_name in device:
            values[field_name] = _validate_device_value(field, device[field_name])
    try:
        return template.format(**values)
    except KeyError as ex:
        raise HTTPException(status_code=400, detail={"error": f"missing device field for template: {ex}"}) from ex


@app.get("/api/frontends")
def get_frontends() -> dict[str, Any]:
    return _load_frontend_catalog()


@app.get("/api/status")
def get_status() -> dict[str, Any]:
    rc, out, err = _run_cmd([*_daqctl_base_args(), "status"], timeout_sec=DAQCTL_TIMEOUT_SEC)
    if rc != 0:
        raise HTTPException(status_code=500, detail={"error": "daqctl status failed", "stderr": err, "stdout": out})
    return _parse_status_line(out)


@app.get("/api/next-run")
def get_next_run() -> dict[str, int]:
    table = (
        os.getenv("SIMPLEDAQ_MYSQL_TABLE")
        or DEFAULTS.get("kMySqlRunLogTable")
        or DEFAULTS.get("kMySqlSubrunLogTable")
        or "daq_log"
    )
    use_short, err_text = _detect_run_log_schema(table)
    if err_text:
        raise HTTPException(status_code=500, detail={"error": err_text})

    run_col = "run" if use_short else "run_number"
    query = f"SELECT COALESCE(MAX({run_col}) + 1, 0) FROM `{table}`"
    rc, out, err = _run_mysql_query(query)
    if rc != 0:
        raise HTTPException(status_code=500, detail={"error": "mysql next-run query failed", "stderr": err, "query": query})
    try:
        value = int(out.strip() or "0")
    except ValueError:
        value = 0
    return {"next_run": value}


@app.post("/api/start")
def start_daq(req: StartRequest) -> dict[str, Any]:
    output_dir = req.output_dir or str(WEB_CONFIG.get("output_dir", "./output"))
    devices_input = req.devices if req.devices is not None else WEB_CONFIG.get("devices", [])
    if not isinstance(devices_input, list) or not devices_input:
        raise HTTPException(status_code=400, detail="devices must not be empty")

    catalog = _load_frontend_catalog()
    frontends = {
        str(item.get("id", "")): item
        for item in catalog.get("frontends", [])
        if isinstance(item, dict) and str(item.get("id", "")) != ""
    }

    args: list[str] = [*_daqctl_base_args(), "start", "--output-dir", output_dir]
    for device in devices_input:
        if not isinstance(device, dict):
            raise HTTPException(status_code=400, detail={"error": "each device must be an object"})
        frontend_id = str(device.get("frontend", "")).strip()
        if frontend_id == "":
            raise HTTPException(status_code=400, detail={"error": "device.frontend is required"})
        frontend_schema = frontends.get(frontend_id)
        if frontend_schema is None:
            raise HTTPException(status_code=400, detail={"error": f"unknown frontend '{frontend_id}'"})
        spec = _build_device_spec(device, frontend_schema)
        args.extend(["--device", f"{frontend_id}={spec}"])
    events_per_file = req.events_per_file
    if events_per_file is None:
        default_events = WEB_CONFIG.get("events_per_file", WEB_CONFIG.get("events_per_subrun"))
        if default_events is not None:
            try:
                events_per_file = int(default_events)
            except Exception:
                events_per_file = None
    if events_per_file is not None:
        args.extend(["--events-per-file", str(events_per_file)])
    if req.reconnect_ms is not None:
        args.extend(["--reconnect-ms", str(req.reconnect_ms)])
    if req.read_timeout_ms is not None:
        args.extend(["--read-timeout-ms", str(req.read_timeout_ms)])
    if req.duration_sec is not None:
        args.extend(["--duration-sec", str(req.duration_sec)])
    startup_connect_timeout_sec = req.startup_connect_timeout_sec
    if startup_connect_timeout_sec is None:
        raw_startup_timeout = WEB_CONFIG.get("startup_connect_timeout_sec")
        if raw_startup_timeout is not None:
            try:
                startup_connect_timeout_sec = int(raw_startup_timeout)
            except Exception:
                startup_connect_timeout_sec = None
    if startup_connect_timeout_sec is not None:
        args.extend(["--startup-connect-timeout-sec", str(startup_connect_timeout_sec)])
    reconnect_failure_timeout_sec = req.reconnect_failure_timeout_sec
    if reconnect_failure_timeout_sec is None:
        raw_reconnect_timeout = WEB_CONFIG.get("reconnect_failure_timeout_sec")
        if raw_reconnect_timeout is not None:
            try:
                reconnect_failure_timeout_sec = int(raw_reconnect_timeout)
            except Exception:
                reconnect_failure_timeout_sec = None
    if reconnect_failure_timeout_sec is not None:
        args.extend(["--reconnect-failure-timeout-sec", str(reconnect_failure_timeout_sec)])
    comment = req.comment
    if comment is None:
        raw_comment = WEB_CONFIG.get("comment")
        comment = str(raw_comment) if raw_comment is not None else None
    if comment:
        args.extend(["--comment", comment])

    rc, out, err = _run_cmd(args, timeout_sec=max(DAQCTL_TIMEOUT_SEC, 5))
    if rc != 0:
        raise HTTPException(status_code=500, detail={"error": "daqctl start failed", "stderr": err, "stdout": out, "cmd": shlex.join(args)})
    if out.lower().startswith("error"):
        raise HTTPException(status_code=400, detail={"error": out})
    return {"result": out}


@app.post("/api/pause")
def pause_daq() -> dict[str, str]:
    rc, out, err = _run_cmd([*_daqctl_base_args(), "pause"], timeout_sec=DAQCTL_TIMEOUT_SEC)
    if rc != 0 or out.lower().startswith("error"):
        raise HTTPException(status_code=400, detail={"error": out or err})
    return {"result": out}


@app.post("/api/resume")
def resume_daq() -> dict[str, str]:
    rc, out, err = _run_cmd([*_daqctl_base_args(), "resume"], timeout_sec=DAQCTL_TIMEOUT_SEC)
    if rc != 0 or out.lower().startswith("error"):
        raise HTTPException(status_code=400, detail={"error": out or err})
    return {"result": out}


@app.post("/api/stop")
def stop_daq() -> dict[str, str]:
    rc, out, err = _run_cmd([*_daqctl_base_args(), "stop"], timeout_sec=DAQCTL_TIMEOUT_SEC)
    if rc != 0 or out.lower().startswith("error"):
        raise HTTPException(status_code=400, detail={"error": out or err})
    return {"result": out}


@app.post("/api/shutdown")
def shutdown_daqd() -> dict[str, str]:
    rc, out, err = _run_cmd([*_daqctl_base_args(), "shutdown"], timeout_sec=DAQCTL_TIMEOUT_SEC)
    if rc != 0 or out.lower().startswith("error"):
        raise HTTPException(status_code=400, detail={"error": out or err})
    return {"result": out}


def _get_run_log_rows(
    limit: int = Query(default=100, ge=1, le=1000),
    offset: int = Query(default=0, ge=0),
) -> dict[str, Any]:
    table = (
        os.getenv("SIMPLEDAQ_MYSQL_TABLE")
        or DEFAULTS.get("kMySqlRunLogTable")
        or DEFAULTS.get("kMySqlSubrunLogTable")
        or "daq_log"
    )
    use_short, err_text = _detect_run_log_schema(table)
    if err_text:
        raise HTTPException(status_code=500, detail={"error": err_text})

    if use_short:
        query = (
            f"SELECT run, subrun, nevents, start_time, end_time, status, comment "
            f"FROM `{table}` ORDER BY run DESC, subrun DESC LIMIT {int(limit)} OFFSET {int(offset)}"
        )
    else:
        query = (
            f"SELECT run_number, subrun_number, event_count, start_time, end_time, status, comment "
            f"FROM `{table}` ORDER BY run_number DESC, subrun_number DESC LIMIT {int(limit)} OFFSET {int(offset)}"
        )

    rc, out, err = _run_mysql_query(query)
    if rc != 0:
        raise HTTPException(status_code=500, detail={"error": "mysql query failed", "stderr": err, "query": query})

    rows: list[dict[str, Any]] = []
    if out:
        for line in out.splitlines():
            parts = line.split("\t")
            if len(parts) != 7:
                continue
            run, subrun, nevents, start_time, end_time, status, comment = parts
            rows.append(
                {
                    "run": int(run),
                    "subrun": int(subrun),
                    "nevents": int(nevents),
                    "start_time": start_time,
                    "end_time": end_time,
                    "status": status,
                    "comment": comment,
                }
            )

    return {"rows": rows, "limit": limit, "offset": offset}


@app.get("/api/run-log")
def get_run_log(
    limit: int = Query(default=100, ge=1, le=1000),
    offset: int = Query(default=0, ge=0),
) -> dict[str, Any]:
    return _get_run_log_rows(limit=limit, offset=offset)


@app.get("/api/subruns")
def get_subruns_compat(
    limit: int = Query(default=100, ge=1, le=1000),
    offset: int = Query(default=0, ge=0),
) -> dict[str, Any]:
    return _get_run_log_rows(limit=limit, offset=offset)
