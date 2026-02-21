from __future__ import annotations

import argparse
import os
from pathlib import Path

import uvicorn


def main() -> None:
    parser = argparse.ArgumentParser(description="SimpleDAQ web server launcher")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--reload", action="store_true")
    parser.add_argument("--config", default="web/defaults.json", help="Path to web JSON config")
    args = parser.parse_args()

    cfg_path = Path(args.config).expanduser().resolve()
    os.environ["SIMPLEDAQ_WEB_CONFIG"] = str(cfg_path)
    uvicorn.run("web.app:app", host=args.host, port=args.port, reload=args.reload)


if __name__ == "__main__":
    main()
