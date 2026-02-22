from __future__ import annotations

import ipaddress
import json
import os
import re
import shlex
import signal
import subprocess
import threading
import time
from pathlib import Path
from typing import Any

from fastapi import FastAPI, HTTPException, Query
from fastapi.responses import FileResponse, Response
from pydantic import BaseModel, Field

try:
    import zmq  # type: ignore
except Exception:  # pragma: no cover
    zmq = None

APP_ROOT = Path(__file__).resolve().parent
REPO_ROOT = APP_ROOT.parent
DEFAULTS_HPP = REPO_ROOT / "src" / "core" / "defaults.hpp"
DEFAULT_DAQCTL = REPO_ROOT / "build" / "daqctl"
DEFAULT_DAQD = REPO_ROOT / "build" / "daqd"
DEFAULT_DATAMON = REPO_ROOT / "build" / "datamon"
DEFAULT_WEB_CONFIG = APP_ROOT / "defaults.json"
DAQCTL_TIMEOUT_SEC = 2

_DAQD_LOCK = threading.Lock()
_DAQD_PROC: subprocess.Popen[Any] | None = None
_DATAMON_LOCK = threading.Lock()
_DATAMON_PROC: subprocess.Popen[Any] | None = None
_DATAMON_PID: int | None = None
_DATAMON_LAST_EXIT_CODE = 0
_DATAMON_ACTIVE_DECODER: dict[str, str] | None = None
_DATAMON_ACTIVE_ANALYSES: list[dict[str, str]] = []
_DATAMON_SELECTED_ANALYSIS = ""
_MONITOR_CTRL_LOCK = threading.Lock()


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


def _resolve_datamon_log_path() -> Path:
    configured = WEB_CONFIG.get("datamon_log_path") or os.getenv("SIMPLEDAQ_DATAMON_LOG") or "logs/datamon.log"
    return _resolve_repo_path(str(configured))


DATAMON_LOG_PATH = _resolve_datamon_log_path()


def _resolve_snapshot_select_endpoint() -> str:
    return str(WEB_CONFIG.get("datamon_snapshot_select_endpoint") or "ipc:///tmp/daq_monitors/select_analysis.sock")


SNAPSHOT_SELECT_ENDPOINT = _resolve_snapshot_select_endpoint()


def _resolve_datamon_state_path() -> Path:
    configured = WEB_CONFIG.get("datamon_state_path") or "/tmp/daq_monitors/datamon_state.json"
    return _resolve_repo_path(str(configured))


DATAMON_STATE_PATH = _resolve_datamon_state_path()


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


def _is_pid_alive(pid: int) -> bool:
    if pid <= 0:
        return False
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    except OSError:
        return False
    return True


def _read_datamon_state() -> dict[str, Any]:
    if not DATAMON_STATE_PATH.exists():
        return {}
    try:
        data = json.loads(DATAMON_STATE_PATH.read_text(encoding="utf-8"))
    except Exception:
        return {}
    return data if isinstance(data, dict) else {}


def _write_datamon_state(state: dict[str, Any]) -> None:
    DATAMON_STATE_PATH.parent.mkdir(parents=True, exist_ok=True)
    DATAMON_STATE_PATH.write_text(json.dumps(state, ensure_ascii=True, separators=(",", ":")), encoding="utf-8")


def _clear_datamon_state_file() -> None:
    try:
        DATAMON_STATE_PATH.unlink(missing_ok=True)
    except Exception:
        pass


def _persist_datamon_state() -> None:
    if _DATAMON_PID is None or _DATAMON_PID <= 0:
        _clear_datamon_state_file()
        return
    state: dict[str, Any] = {
        "pid": int(_DATAMON_PID),
        "last_exit": int(_DATAMON_LAST_EXIT_CODE),
        "selected_analysis": str(_DATAMON_SELECTED_ANALYSIS),
    }
    if _DATAMON_ACTIVE_DECODER is not None:
        state["active_decoder"] = _DATAMON_ACTIVE_DECODER
    if isinstance(_DATAMON_ACTIVE_ANALYSES, list):
        state["active_analyses"] = _DATAMON_ACTIVE_ANALYSES
    try:
        _write_datamon_state(state)
    except Exception:
        pass


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


def _datamon_base_args() -> list[str]:
    datamon_path_text = WEB_CONFIG.get("datamon_path") or os.getenv("SIMPLEDAQ_DATAMON") or str(DEFAULT_DATAMON)
    datamon_path = _resolve_repo_path(str(datamon_path_text))
    data_endpoint = os.getenv("SIMPLEDAQ_DATA_ENDPOINT") or DEFAULTS.get("kDataEndpoint") or "ipc:///tmp/simpledaq_data.sock"
    args = [str(datamon_path), "--data-endpoint", data_endpoint]
    poll_ms = WEB_CONFIG.get("datamon_poll_ms")
    if poll_ms is not None:
        try:
            args.extend(["--poll-ms", str(int(poll_ms))])
        except Exception:
            pass
    idle_timeout_sec = WEB_CONFIG.get("datamon_idle_timeout_sec")
    if idle_timeout_sec is not None:
        try:
            args.extend(["--idle-timeout-sec", str(int(idle_timeout_sec))])
        except Exception:
            pass
    return args


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


def _is_datamon_running() -> tuple[bool, int | None]:
    global _DATAMON_PROC
    global _DATAMON_PID
    global _DATAMON_LAST_EXIT_CODE
    global _DATAMON_ACTIVE_DECODER
    global _DATAMON_ACTIVE_ANALYSES
    global _DATAMON_SELECTED_ANALYSIS
    with _DATAMON_LOCK:
        if _DATAMON_PROC is not None:
            rc = _DATAMON_PROC.poll()
            if rc is None:
                _DATAMON_PID = _DATAMON_PROC.pid
                _persist_datamon_state()
                return True, _DATAMON_PID
            _DATAMON_LAST_EXIT_CODE = int(rc)
            _DATAMON_PROC = None
            _DATAMON_PID = None
            _DATAMON_ACTIVE_DECODER = None
            _DATAMON_ACTIVE_ANALYSES = []
            _DATAMON_SELECTED_ANALYSIS = ""
            _clear_datamon_state_file()
            return False, None

        if _DATAMON_PID is None:
            state = _read_datamon_state()
            pid_raw = state.get("pid")
            try:
                pid = int(pid_raw)
            except Exception:
                pid = 0
            if pid > 0:
                _DATAMON_PID = pid
                decoder = state.get("active_decoder")
                analyses = state.get("active_analyses")
                selected = state.get("selected_analysis")
                if isinstance(decoder, dict):
                    _DATAMON_ACTIVE_DECODER = decoder
                if isinstance(analyses, list):
                    _DATAMON_ACTIVE_ANALYSES = analyses
                if isinstance(selected, str):
                    _DATAMON_SELECTED_ANALYSIS = selected
                try:
                    _DATAMON_LAST_EXIT_CODE = int(state.get("last_exit", _DATAMON_LAST_EXIT_CODE))
                except Exception:
                    pass

        if _DATAMON_PID is not None and _is_pid_alive(_DATAMON_PID):
            return True, _DATAMON_PID

        _DATAMON_PID = None
        _DATAMON_ACTIVE_DECODER = None
        _DATAMON_ACTIVE_ANALYSES = []
        _DATAMON_SELECTED_ANALYSIS = ""
        _clear_datamon_state_file()
        return False, None


def _load_monitor_modules() -> dict[str, Any]:
    cmd = [*_datamon_base_args()[:1], "--list-modules", "--json"]
    rc, out, err = _run_cmd(cmd, timeout_sec=DAQCTL_TIMEOUT_SEC)
    if rc != 0:
        raise HTTPException(status_code=500, detail={"error": "datamon module list failed", "stderr": err, "stdout": out})
    try:
        payload = json.loads(out)
    except json.JSONDecodeError as ex:
        raise HTTPException(status_code=500, detail={"error": f"invalid datamon modules payload: {ex}", "payload": out}) from ex
    if not isinstance(payload, dict):
        raise HTTPException(status_code=500, detail={"error": "invalid datamon modules schema"})
    decoders = payload.get("decoders")
    analyses = payload.get("analyses")
    if not isinstance(decoders, list) or not isinstance(analyses, list):
        raise HTTPException(status_code=500, detail={"error": "invalid datamon modules schema content"})
    return {"decoders": decoders, "analyses": analyses}


def _tail_lines(path: Path, limit: int) -> list[str]:
    if limit <= 0 or not path.exists():
        return []
    try:
        with path.open("r", encoding="utf-8", errors="replace") as fh:
            lines = fh.readlines()
    except Exception:
        return []
    return [line.rstrip("\n") for line in lines[-limit:]]


def _send_selected_analysis(module: str, max_attempts: int = 6, send_timeout_ms: int = 120, recv_timeout_ms: int = 120) -> None:
    if zmq is None:
        raise RuntimeError("pyzmq is not available")
    text = module.strip()
    endpoint = SNAPSHOT_SELECT_ENDPOINT
    if endpoint.startswith("ipc://"):
        socket_path = endpoint[len("ipc://") :]
        if socket_path:
            Path(socket_path).expanduser().parent.mkdir(parents=True, exist_ok=True)
    last_error: Exception | None = None
    with _MONITOR_CTRL_LOCK:
        for _ in range(max(1, int(max_attempts))):
            ctx = zmq.Context.instance()
            sock = ctx.socket(zmq.REQ)
            try:
                sock.setsockopt(zmq.LINGER, 0)
                sock.setsockopt(zmq.SNDTIMEO, int(send_timeout_ms))
                sock.setsockopt(zmq.RCVTIMEO, int(recv_timeout_ms))
                sock.connect(endpoint)
                sock.send_string(text)
                sock.recv()
                return
            except Exception as ex:  # pragma: no cover
                last_error = ex
                time.sleep(0.03)
            finally:
                sock.close()
    if last_error is not None:
        raise last_error


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


class MonitorStartRequest(BaseModel):
    decoder: str | None = None
    analyses: list[str] | None = None
    snapshot_interval_sec: float | None = Field(default=None, gt=0)


class MonitorSelectionRequest(BaseModel):
    module: str | None = None


app = FastAPI(title="SimpleDAQ Web API", version="0.1.0")


@app.get("/")
def index() -> FileResponse:
    return FileResponse(APP_ROOT / "index.html")


@app.get("/run-log")
def runlog_page() -> FileResponse:
    return FileResponse(APP_ROOT / "runlog.html")


@app.get("/analysis")
def analysis_page() -> FileResponse:
    return FileResponse(APP_ROOT / "analysis.html")


@app.get("/styles.css")
def styles_css() -> FileResponse:
    return FileResponse(APP_ROOT / "styles.css", media_type="text/css")


@app.get("/status_panel.css")
def status_panel_css() -> FileResponse:
    return FileResponse(APP_ROOT / "status_panel.css", media_type="text/css")


@app.get("/app.js")
def app_js() -> FileResponse:
    return FileResponse(APP_ROOT / "app.js", media_type="application/javascript")


@app.get("/runlog.js")
def runlog_js() -> FileResponse:
    return FileResponse(APP_ROOT / "runlog.js", media_type="application/javascript")


@app.get("/analysis.js")
def analysis_js() -> FileResponse:
    return FileResponse(APP_ROOT / "analysis.js", media_type="application/javascript")


@app.get("/sidebar.js")
def sidebar_js() -> FileResponse:
    return FileResponse(APP_ROOT / "sidebar.js", media_type="application/javascript")


@app.get("/status_panel.js")
def status_panel_js() -> FileResponse:
    return FileResponse(APP_ROOT / "status_panel.js", media_type="application/javascript")


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


@app.get("/api/monitor/log")
def get_datamon_log(limit: int = Query(default=50, ge=1, le=500)) -> dict[str, Any]:
    lines = _tail_lines(DATAMON_LOG_PATH, limit)
    running, pid = _is_datamon_running()
    return {"running": running, "pid": pid, "lines": lines, "path": str(DATAMON_LOG_PATH)}


@app.get("/api/monitor/modules")
def get_monitor_modules() -> dict[str, Any]:
    return _load_monitor_modules()


@app.get("/api/monitor/status")
def get_monitor_status() -> dict[str, Any]:
    running, pid = _is_datamon_running()
    state = "running" if running else ("error" if _DATAMON_LAST_EXIT_CODE != 0 else "stopped")
    healthy = 1 if running else 0
    return {
        "running": running,
        "healthy": healthy,
        "state": state,
        "pid": pid,
        "last_exit": _DATAMON_LAST_EXIT_CODE,
        "active_decoder": _DATAMON_ACTIVE_DECODER if running else None,
        "active_analyses": _DATAMON_ACTIVE_ANALYSES if running else [],
        "selected_analysis": _DATAMON_SELECTED_ANALYSIS if running else "",
    }


@app.post("/api/monitor/selected-analysis")
def set_selected_analysis(req: MonitorSelectionRequest) -> dict[str, Any]:
    global _DATAMON_SELECTED_ANALYSIS
    name = (req.module or "").strip()
    running, _ = _is_datamon_running()
    _DATAMON_SELECTED_ANALYSIS = name
    _persist_datamon_state()
    if not running:
        return {"result": "queued", "selected_analysis": name, "running": False}
    try:
        _send_selected_analysis(name, max_attempts=3, send_timeout_ms=80, recv_timeout_ms=80)
        return {"result": "ok", "selected_analysis": name, "running": True}
    except Exception:
        return {"result": "queued", "selected_analysis": name, "running": True}


def _monitor_snapshot_root() -> Path:
    configured = WEB_CONFIG.get("datamon_snapshot_dir") or "/tmp/daq_monitors"
    return _resolve_repo_path(str(configured))


@app.get("/api/monitor/screens")
def get_monitor_screens() -> dict[str, Any]:
    root = _monitor_snapshot_root()
    title_map: dict[str, str] = {}
    for item in _DATAMON_ACTIVE_ANALYSES:
        if not isinstance(item, dict):
            continue
        name = str(item.get("name", "")).strip()
        if name == "":
            continue
        title = str(item.get("title", name)).strip() or name
        title_map[name] = title
    analyses: list[dict[str, Any]] = []
    if root.exists():
        for module_dir in sorted([p for p in root.iterdir() if p.is_dir()], key=lambda p: p.name):
            images = []
            for png in sorted(module_dir.glob("*.png"), key=lambda p: p.name):
                try:
                    mtime = int(png.stat().st_mtime)
                except Exception:
                    mtime = 0
                rel = png.relative_to(root).as_posix()
                images.append({"name": png.name, "path": rel, "url": f"/monitor-snapshots/{rel}?t={mtime}"})
            analyses.append({"name": module_dir.name, "title": title_map.get(module_dir.name, module_dir.name), "images": images})
    return {"analyses": analyses}


@app.get("/monitor-snapshots/{relative_path:path}")
def get_monitor_snapshot(relative_path: str) -> Response:
    root = _monitor_snapshot_root().resolve()
    target = (root / relative_path).resolve()
    try:
        target.relative_to(root)
    except ValueError as ex:
        raise HTTPException(status_code=400, detail={"error": "invalid snapshot path"}) from ex
    if not target.exists() or not target.is_file():
        raise HTTPException(status_code=404, detail={"error": "snapshot not found"})
    try:
        payload = target.read_bytes()
    except FileNotFoundError as ex:
        raise HTTPException(status_code=404, detail={"error": "snapshot not found"}) from ex
    except OSError as ex:
        raise HTTPException(status_code=500, detail={"error": f"snapshot read failed: {ex}"}) from ex
    return Response(content=payload, media_type="image/png")


@app.post("/api/monitor/start")
def start_datamon(req: MonitorStartRequest) -> dict[str, Any]:
    global _DATAMON_PROC
    global _DATAMON_PID
    global _DATAMON_LAST_EXIT_CODE
    global _DATAMON_ACTIVE_DECODER
    global _DATAMON_ACTIVE_ANALYSES
    global _DATAMON_SELECTED_ANALYSIS

    running, pid = _is_datamon_running()
    if running:
        return {"result": "already running", "pid": pid}

    modules = _load_monitor_modules()
    available_decoders: dict[str, str] = {}
    for item in modules.get("decoders", []):
        if not isinstance(item, dict):
            continue
        name = str(item.get("name", "")).strip()
        if name == "":
            continue
        title = str(item.get("title", name)).strip() or name
        available_decoders[name] = title
    available_analyses: dict[str, tuple[str, str]] = {}
    for item in modules.get("analyses", []):
        if not isinstance(item, dict):
            continue
        name = str(item.get("name", "")).strip()
        if name == "":
            continue
        title = str(item.get("title", name)).strip() or name
        expected = str(item.get("expected_decoder", "")).strip()
        available_analyses[name] = (title, expected)

    decoder = (req.decoder or str(WEB_CONFIG.get("datamon_decoder", "kc705_tof"))).strip()

    raw_analyses = req.analyses if req.analyses is not None else WEB_CONFIG.get("datamon_analyses", [])
    if not isinstance(raw_analyses, list):
        raw_analyses = []
    selected_analyses: list[str] = []
    required_decoder = ""
    for item in raw_analyses:
        name = str(item).strip()
        if name == "":
            continue
        info = available_analyses.get(name)
        if info is None:
            raise HTTPException(status_code=400, detail={"error": f"unsupported analysis '{name}'"})
        _, expected = info
        if expected:
            if required_decoder == "":
                required_decoder = expected
            elif required_decoder != expected:
                raise HTTPException(
                    status_code=400,
                    detail={"error": f"analysis decoder conflict: '{required_decoder}' vs '{expected}'"},
                )
        selected_analyses.append(name)

    if required_decoder:
        decoder = required_decoder

    if decoder == "":
        raise HTTPException(status_code=400, detail={"error": "decoder is required"})
    if decoder not in available_decoders:
        raise HTTPException(status_code=400, detail={"error": f"unsupported decoder '{decoder}'"})

    args = [*_datamon_base_args(), "--decoder", decoder, "--text-stream", "--no-gui"]
    snapshot_dir = str(WEB_CONFIG.get("datamon_snapshot_dir", "/tmp/daq_monitors"))
    args.extend(["--snapshot-dir", str(_resolve_repo_path(snapshot_dir))])
    snapshot_interval_ms: int | None = None
    if req.snapshot_interval_sec is not None:
        try:
            snapshot_interval_ms = max(1, int(float(req.snapshot_interval_sec) * 1000.0))
        except Exception:
            snapshot_interval_ms = None
    if snapshot_interval_ms is None:
        raw_cfg_ms = WEB_CONFIG.get("datamon_snapshot_interval_ms")
        raw_cfg_sec = WEB_CONFIG.get("datamon_snapshot_interval_sec")
        if raw_cfg_sec is not None:
            try:
                snapshot_interval_ms = max(1, int(float(raw_cfg_sec) * 1000.0))
            except Exception:
                snapshot_interval_ms = None
        if snapshot_interval_ms is None:
            try:
                snapshot_interval_ms = int(raw_cfg_ms) if raw_cfg_ms is not None else 1000
            except Exception:
                snapshot_interval_ms = 1000
    try:
        args.extend(["--snapshot-interval-ms", str(int(snapshot_interval_ms))])
    except Exception:
        args.extend(["--snapshot-interval-ms", "1000"])
    args.extend(["--snapshot-select-endpoint", SNAPSHOT_SELECT_ENDPOINT])
    for name in selected_analyses:
        args.extend(["--analysis", name])

    DATAMON_LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
    with _DATAMON_LOCK:
        try:
            with DATAMON_LOG_PATH.open("a", encoding="utf-8") as log_head:
                log_head.write("\n=== web requested datamon start ===\n")
            with DATAMON_LOG_PATH.open("a", encoding="utf-8") as log_out:
                _DATAMON_PROC = subprocess.Popen(
                    args,
                    cwd=str(REPO_ROOT),
                    stdout=log_out,
                    stderr=log_out,
                    text=True,
                    start_new_session=True,
                )
        except Exception as ex:
            _DATAMON_PROC = None
            raise HTTPException(status_code=500, detail={"error": f"failed to start datamon: {ex}", "cmd": shlex.join(args)}) from ex

    _DATAMON_LAST_EXIT_CODE = 0
    _DATAMON_PID = _DATAMON_PROC.pid
    _DATAMON_ACTIVE_DECODER = {"name": decoder, "title": available_decoders.get(decoder, decoder)}
    _DATAMON_ACTIVE_ANALYSES = []
    _DATAMON_SELECTED_ANALYSIS = ""
    try:
        _send_selected_analysis("", max_attempts=2, send_timeout_ms=60, recv_timeout_ms=60)
    except Exception:
        pass
    for name in selected_analyses:
        title = available_analyses.get(name, (name, ""))[0]
        _DATAMON_ACTIVE_ANALYSES.append({"name": name, "title": title})
    _persist_datamon_state()
    return {"result": "datamon started", "pid": _DATAMON_PROC.pid}


@app.post("/api/monitor/shutdown")
def stop_datamon() -> dict[str, Any]:
    global _DATAMON_PROC
    global _DATAMON_PID
    global _DATAMON_LAST_EXIT_CODE
    global _DATAMON_ACTIVE_DECODER
    global _DATAMON_ACTIVE_ANALYSES
    global _DATAMON_SELECTED_ANALYSIS

    with _DATAMON_LOCK:
        proc = _DATAMON_PROC
        pid = _DATAMON_PID
    if proc is None and (pid is None or not _is_pid_alive(pid)):
        return {"result": "datamon is not running"}
    rc = 0
    if proc is not None:
        try:
            proc.send_signal(signal.SIGINT)
            proc.wait(timeout=3)
        except Exception:
            try:
                proc.terminate()
                proc.wait(timeout=3)
            except Exception:
                proc.kill()
                proc.wait(timeout=3)
        polled = proc.poll()
        rc = int(polled) if polled is not None else 0
    else:
        assert pid is not None
        try:
            os.kill(pid, signal.SIGINT)
        except Exception:
            pass
        deadline = time.time() + 1.0
        while time.time() < deadline:
            if not _is_pid_alive(pid):
                break
            time.sleep(0.05)
        if _is_pid_alive(pid):
            try:
                os.kill(pid, signal.SIGTERM)
            except Exception:
                pass
            deadline = time.time() + 1.0
            while time.time() < deadline:
                if not _is_pid_alive(pid):
                    break
                time.sleep(0.05)
        if _is_pid_alive(pid):
            try:
                os.kill(pid, signal.SIGKILL)
            except Exception:
                pass
        rc = 0
    with _DATAMON_LOCK:
        _DATAMON_LAST_EXIT_CODE = int(rc)
        _DATAMON_PROC = None
        _DATAMON_PID = None
        _DATAMON_ACTIVE_DECODER = None
        _DATAMON_ACTIVE_ANALYSES = []
        _DATAMON_SELECTED_ANALYSIS = ""
        _clear_datamon_state_file()
    try:
        _send_selected_analysis("", max_attempts=1, send_timeout_ms=40, recv_timeout_ms=40)
    except Exception:
        pass
    return {"result": f"datamon stopped (exit={rc})", "exit_code": int(rc)}


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
    main_run_log_limit = WEB_CONFIG.get("main_run_log_limit", WEB_CONFIG.get("run_log_limit", 50))
    try:
        main_run_log_limit = int(main_run_log_limit)
    except Exception:
        main_run_log_limit = 50
    run_log_page_limit = WEB_CONFIG.get("run_log_page_limit", WEB_CONFIG.get("run_log_limit", 200))
    try:
        run_log_page_limit = int(run_log_page_limit)
    except Exception:
        run_log_page_limit = 200
    daqd_log_limit = WEB_CONFIG.get("daqd_log_limit", 300)
    try:
        daqd_log_limit = int(daqd_log_limit)
    except Exception:
        daqd_log_limit = 300
    datamon_log_limit = WEB_CONFIG.get("datamon_log_limit", 300)
    try:
        datamon_log_limit = int(datamon_log_limit)
    except Exception:
        datamon_log_limit = 300
    datamon_snapshot_interval_ms = WEB_CONFIG.get("datamon_snapshot_interval_ms")
    datamon_snapshot_interval_sec = WEB_CONFIG.get("datamon_snapshot_interval_sec")
    if datamon_snapshot_interval_sec is not None:
        try:
            datamon_snapshot_interval_ms = max(1, int(float(datamon_snapshot_interval_sec) * 1000.0))
        except Exception:
            datamon_snapshot_interval_ms = None
    if datamon_snapshot_interval_ms is None:
        datamon_snapshot_interval_ms = 1000
    try:
        datamon_snapshot_interval_ms = int(datamon_snapshot_interval_ms)
    except Exception:
        datamon_snapshot_interval_ms = 1000
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
    datamon_analyses = WEB_CONFIG.get("datamon_analyses", [])
    if not isinstance(datamon_analyses, list):
        datamon_analyses = []

    return {
        "title": title,
        "output_dir": default_output_dir,
        "events_per_file": events_per_file,
        "comment": comment,
        "main_run_log_limit": main_run_log_limit,
        "run_log_page_limit": run_log_page_limit,
        "daqd_log_limit": daqd_log_limit,
        "datamon_log_limit": datamon_log_limit,
        "datamon_decoder": str(WEB_CONFIG.get("datamon_decoder", "kc705_tof")),
        "datamon_analyses": datamon_analyses,
        "datamon_snapshot_dir": str(WEB_CONFIG.get("datamon_snapshot_dir", "/tmp/daq_monitors")),
        "datamon_snapshot_interval_ms": datamon_snapshot_interval_ms,
        "datamon_snapshot_interval_sec": (float(datamon_snapshot_interval_ms) / 1000.0),
        "datamon_snapshot_select_endpoint": str(WEB_CONFIG.get("datamon_snapshot_select_endpoint", "ipc:///tmp/daq_monitors/select_analysis.sock")),
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
    view: str = Query(default="all"),
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

    mode = str(view or "all").strip().lower()
    if mode not in {"all", "summary"}:
        raise HTTPException(status_code=400, detail={"error": "view must be 'all' or 'summary'"})

    if use_short:
        run_col = "run"
        subrun_col = "subrun"
        nevents_col = "nevents"
    else:
        run_col = "run_number"
        subrun_col = "subrun_number"
        nevents_col = "event_count"

    if mode == "all":
        query = (
            f"SELECT {run_col}, {subrun_col}, {nevents_col}, start_time, end_time, status, comment "
            f"FROM `{table}` ORDER BY {run_col} DESC, {subrun_col} DESC LIMIT {int(limit)} OFFSET {int(offset)}"
        )
    else:
        query = (
            "SELECT agg.run_val, agg.subrun_count, agg.nevents_total, agg.start_time, agg.end_time, "
            "CASE "
            "  WHEN ("
            "         tail.status = 'completed' "
            "         OR ("
            "              tail.status = 'stopped' "
            "              AND agg.stopped_count = 1 "
            "              AND agg.error_count = 0 "
            "              AND agg.unknown_count = 0 "
            "            )"
            "       ) AND agg.paused_count > 0 "
            "  THEN CONCAT('completed (paused ', agg.paused_count, ' times)') "
            "  WHEN ("
            "         tail.status = 'completed' "
            "         OR ("
            "              tail.status = 'stopped' "
            "              AND agg.stopped_count = 1 "
            "              AND agg.error_count = 0 "
            "              AND agg.unknown_count = 0 "
            "            )"
            "       ) "
            "  THEN 'completed' "
            "  ELSE tail.status "
            "END AS status, "
            "tail.comment "
            "FROM ("
            f"  SELECT {run_col} AS run_val, "
            f"         COUNT(*) AS subrun_count, "
            f"         SUM({nevents_col}) AS nevents_total, "
            "         MIN(start_time) AS start_time, "
            "         MAX(end_time) AS end_time, "
            "         SUM(CASE WHEN status = 'paused' THEN 1 ELSE 0 END) AS paused_count, "
            "         SUM(CASE WHEN status = 'stopped' THEN 1 ELSE 0 END) AS stopped_count, "
            "         SUM(CASE WHEN status = 'error' THEN 1 ELSE 0 END) AS error_count, "
            "         SUM(CASE WHEN status NOT IN ('completed','paused','stopped','error') THEN 1 ELSE 0 END) AS unknown_count, "
            f"         MAX({subrun_col}) AS last_subrun "
            f"  FROM `{table}` "
            f"  GROUP BY {run_col}"
            ") agg "
            f"JOIN `{table}` tail "
            f"  ON tail.{run_col} = agg.run_val AND tail.{subrun_col} = agg.last_subrun "
            "ORDER BY agg.run_val DESC "
            f"LIMIT {int(limit)} OFFSET {int(offset)}"
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

    return {"rows": rows, "limit": limit, "offset": offset, "view": mode}


@app.get("/api/run-log")
def get_run_log(
    limit: int = Query(default=100, ge=1, le=1000),
    offset: int = Query(default=0, ge=0),
    view: str = Query(default="all"),
) -> dict[str, Any]:
    return _get_run_log_rows(limit=limit, offset=offset, view=view)


@app.get("/api/subruns")
def get_subruns_compat(
    limit: int = Query(default=100, ge=1, le=1000),
    offset: int = Query(default=0, ge=0),
    view: str = Query(default="all"),
) -> dict[str, Any]:
    return _get_run_log_rows(limit=limit, offset=offset, view=view)
