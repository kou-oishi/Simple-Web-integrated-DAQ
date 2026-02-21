# SimpleDAQ

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

## TCP dummy device

`kc705_tof_dummy_device` sends 64-bit frames over TCP at a fixed interval.

Frame layout:
- top 3 bits: board ID (`0-7`)
- next 5 bits: channel ID (`0-31`)
- lower 56 bits: random value

Frames are sent as 8-byte payloads in network byte order (big-endian).

### Run

```bash
./build/kc705_tof_dummy_device --board-id 3 --channel-id 17 --port 9101 --interval-ms 100
```

To randomise channels within a range:

```bash
./build/kc705_tof_dummy_device --board-id 3 --channel-min 8 --channel-max 15 --port 9101 --interval-ms 100
```

### Install

```bash
cmake --install build --prefix ./install
./install/bin/kc705_tof_dummy_device --board-id 3 --channel-id 17 --port 9101 --interval-ms 100
```

### Quick receive test

```bash
nc 127.0.0.1 9101 | xxd -p -c8
```

For `board-id=3` and `channel-id=17`, the first byte is `0x71` (`011` + `10001`).

## DAQ Core

`daq_core` reads raw bytes from multiple TCP devices, passes them through a validator module, and writes validated frames to disk without decoding the payload.

Devices are configured as `--device <frontend>=<frontend-specific-spec>`.
The current frontend is `kc705_tof`, which uses TCP (`board_id@host:port`) and frames data as 8-byte KC705 TOF records.
The validator checks that frame board ID matches the configured `board_id`.
Runtime I/O is configured for low buffering (small FIFO reads and per-event flush on file writes).

### Run

```bash
./build/daq_core \
  --output-dir ./output \
  --run-start 10 \
  --events-per-file 100000 \
  --device kc705_tof=1@127.0.0.2:9101 \
  --device kc705_tof=2@127.0.0.3:9101
```

To stop after a fixed duration:

```bash
./build/daq_core \
  --output-dir ./output \
  --run-start 10 \
  --events-per-file 100000 \
  --device kc705_tof=1@127.0.0.2:9101 \
  --device kc705_tof=2@127.0.0.3:9101 \
  --duration-sec 10
```

Validated frames are merged into run/subrun files.
For `kc705_tof`, each 8-byte frame is written in host byte order for easier local numeric inspection.
- `output_dir/run00010_sub00000.dat`
- `output_dir/run00010_sub00001.dat`
- ...

Each file rolls over after `--events-per-file` validated events.

## Control API (ZeroMQ)

`daqd` provides a control API over ZeroMQ (`REQ/REP`) so CLI and future Web components can use the same interface.
It also publishes status updates over ZeroMQ (`PUB/SUB`).
Default values (endpoints, run numbering width, core runtime defaults) are centralised in `src/core/defaults.hpp`.

Default control endpoint:

```text
ipc:///tmp/simpledaq_ctrl.sock
```

Default status endpoint:

```text
ipc:///tmp/simpledaq_status.sock
```

Default data endpoint:

```text
ipc:///tmp/simpledaq_data.sock
```

Start daemon:

```bash
./build/daqd --endpoint ipc:///tmp/simpledaq_ctrl.sock --data-endpoint ipc:///tmp/simpledaq_data.sock
```

Send control commands from another terminal:

```bash
./build/daqctl --endpoint ipc:///tmp/simpledaq_ctrl.sock status
./build/daqctl --endpoint ipc:///tmp/simpledaq_ctrl.sock start --output-dir ./output --run-start 1 --events-per-file 100000 --device kc705_tof=1@127.0.0.2:9101 --device kc705_tof=2@127.0.0.3:9101
./build/daqctl --endpoint ipc:///tmp/simpledaq_ctrl.sock stop
./build/daqctl --endpoint ipc:///tmp/simpledaq_ctrl.sock shutdown
```

Monitor status events:

```bash
./build/daqmon --status-endpoint ipc:///tmp/simpledaq_status.sock
```

`datamon` decoders are selected as `--decoder <module>[=<module-specific-spec>]`, mirroring `daq_core --device`.
Current module:
- `kc705_tof`

`--root-out` uses a decoder-specific ROOT sink selected by `--decoder`, so branch layout depends on decoder module.
`--text-stream` uses decoder text formatting and prints every decoded event for low-level debugging.

Data monitor from an existing `.dat` file:

```bash
./build/datamon --input-file ./output/run00001_sub00000.dat --decoder kc705_tof --print-every 1000
```

Optional ROOT output (if built with ROOT available):

```bash
./build/datamon --input-file ./output/run00001_sub00000.dat --decoder kc705_tof --root-out ./monitor.root --no-console
```

Live monitor from DAQ output directory (follows `runXXXXX_subYYYYY.dat` as files grow/roll):

```bash
./build/datamon --live-output-dir ./output --run-start 1 --decoder kc705_tof --root-out ./live.root
```

Direct live monitor from `daqd` data PUB (avoids simultaneous read/write on `.dat`):

```bash
./build/datamon --data-endpoint ipc:///tmp/simpledaq_data.sock --decoder kc705_tof --root-out ./live.root
```

Low-level text debug stream (all events):

```bash
./build/datamon --data-endpoint ipc:///tmp/simpledaq_data.sock --decoder kc705_tof --text-stream --no-console
```

Control API contract (for CLI or Web server adapters):
- Transport: ZeroMQ REQ/REP to control endpoint.
- Request strings:
`status`
`start <daq args>`
`stop`
`shutdown`
- Response strings:
`ok ...` on success, `error ...` on failure.
