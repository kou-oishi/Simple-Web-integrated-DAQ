# SimpleDAQ

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

## TCP dummy device

`tcp_dummy_device` sends 64-bit frames over TCP at a fixed interval.

Frame layout:
- top 3 bits: board ID (`0-7`)
- next 5 bits: channel ID (`0-31`)
- lower 56 bits: random value

Frames are sent as 8-byte payloads in network byte order (big-endian).

### Run

```bash
./build/tcp_dummy_device --board-id 3 --channel-id 17 --port 9101 --interval-ms 100
```

To randomise channels within a range:

```bash
./build/tcp_dummy_device --board-id 3 --channel-min 8 --channel-max 15 --port 9101 --interval-ms 100
```

### Install

```bash
cmake --install build --prefix ./install
./install/bin/tcp_dummy_device --board-id 3 --channel-id 17 --port 9101 --interval-ms 100
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

Validated frames are merged into run files.
For `kc705_tof`, each 8-byte frame is written in host byte order for easier local numeric inspection.
- `output_dir/run000000010.dat`
- `output_dir/run000000011.dat`
- ...

Each file rolls over after `--events-per-file` validated events.
