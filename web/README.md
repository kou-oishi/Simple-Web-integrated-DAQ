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
  "daqd_path": "build/daqd",
  "daqd_log_path": "logs/daqd.log",
  "output_dir": "./output",
  "events_per_subrun": 100000,
  "startup_connect_timeout_sec": 5,
  "reconnect_failure_timeout_sec": 10,
  "comment": "",
  "main_run_log_limit": 50,
  "run_log_page_limit": 200,
  "run_log_limit": 50,
  "devices": [
    { "frontend": "kc705_tof", "board_id": 1, "host": "127.0.0.2", "port": 9101 }
  ]
}
```

## Main APIs

- `GET /api/health`
- `GET /api/status`
- `GET /api/ui-config`
- `GET /api/frontends`
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
- `SIMPLEDAQ_CTRL_ENDPOINT`
- `SIMPLEDAQ_MYSQL_HOST`
- `SIMPLEDAQ_MYSQL_PORT`
- `SIMPLEDAQ_MYSQL_USER`
- `SIMPLEDAQ_MYSQL_PASSWORD`
- `SIMPLEDAQ_MYSQL_DATABASE`
- `SIMPLEDAQ_MYSQL_TABLE`
- `SIMPLEDAQ_WEB_CONFIG`
