# SimpleDAQ Web Backend

## Install

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r web/requirements.txt
```

## Run

```bash
python3 -m web.server --host 0.0.0.0 --port 8080 --config web/defaults.json
```

`--config` で UI と start default を読み込みます。

Example config (`web/defaults.json`):

```json
{
  "title": "TOF DAQ Control",
  "bin_path": "build",
  "daqd_log_path": "logs/daqd.log",
  "datamon_log_path": "logs/datamon.log",
  "datamon_snapshot_dir": "/tmp/daq_monitors",
  "datamon_snapshot_interval_sec": 1.0,
  "datamon_snapshot_select_endpoint": "ipc:///tmp/daq_monitors/select_analysis.sock",
  "output_dir": "./output",
  "events_per_subrun": 100000,
  "startup_connect_timeout_sec": 5,
  "reconnect_failure_timeout_sec": 10,
  "comment": "",
  "main_run_log_limit": 50,
  "run_log_page_limit": 200,
  "datamon_log_limit": 300,
  "datamon_decoder": "kc705_tof",
  "datamon_analyses": ["kc705_tof_overview"],
  "run_log_limit": 50,
  "devices": [
    { "frontend": "kc705_tof", "board_id": 1, "host": "127.0.0.2", "port": 9101 }
  ]
}
```

`bin_path` 配下に `daqctl` / `daqd` / `datamon` がある前提で解決します（必要なら `daqctl_path` / `daqd_path` / `datamon_path` で個別上書き可能）。

## Main APIs

- `GET /api/health`
- `GET /api/status`
- `GET /api/ui-config`
- `GET /api/frontends`
- `GET /api/monitor/modules`
- `GET /api/monitor/status`
- `GET /api/monitor/log`
- `GET /api/monitor/screens`
- `POST /api/monitor/start`
- `POST /api/monitor/shutdown`
- `POST /api/monitor/selected-analysis`
- `POST /api/start`
- `POST /api/pause`
- `POST /api/resume`
- `POST /api/stop`
- `POST /api/shutdown`
- `GET /api/run-log?limit=100&offset=0`

`POST /api/start` expects:
- `output_dir: string`
- `devices: [{ frontend: "...", ...frontend fields... }]`

Device fields are obtained at runtime from `GET /api/frontends` and are validated on the backend.

## Environment Overrides

- `SIMPLEDAQ_DAQCTL`
- `SIMPLEDAQ_DAQD`
- `SIMPLEDAQ_DAQD_LOG`
- `SIMPLEDAQ_DATAMON`
- `SIMPLEDAQ_DATAMON_LOG`
- `SIMPLEDAQ_CTRL_ENDPOINT`
- `SIMPLEDAQ_MYSQL_HOST`
- `SIMPLEDAQ_MYSQL_PORT`
- `SIMPLEDAQ_MYSQL_USER`
- `SIMPLEDAQ_MYSQL_PASSWORD`
- `SIMPLEDAQ_MYSQL_DATABASE`
- `SIMPLEDAQ_MYSQL_TABLE`
- `SIMPLEDAQ_WEB_CONFIG`
