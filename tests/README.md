# Transport regression tests

These tests compile the production `components/modbustcp/modbus.cpp` against
small ESPHome clock, network and socket shims. No physical device or network
connection is used. Assertions must remain enabled.

Run with Python 3 and a C++14 compiler:

```sh
python tests/run_tests.py
python tests/run_tests.py --compiler clang++
python tests/run_tests.py --compiler /path/to/zig c++
```

The Zig 0.14.1 compiler was used for validation on Windows. The socket shim also
provides POSIX headers for Linux/macOS host compilers. The runner builds into a
temporary directory and removes its own generated files on completion.

Coverage includes delayed Wi-Fi startup, repeated connection failures and socket
cleanup, pending connection timeouts, retry backoff, fragmented/coalesced frames,
late and unsolicited replies, exceptions, partial/stalled writes, silent peers,
Wi-Fi loss, remote close, malformed responses and timer wraparound. Read requests
are checked against literal expected Modbus TCP bytes; response payloads are
checked for their exact length and contents.

These are transport simulations, not hardware or charging-policy tests. They do
not exercise the full ESPHome controller queue or prove recovery on a particular
access point, inverter or ESP32. The corresponding TCP client firmware also
compiled successfully with ESPHome 2026.5.0, Arduino-ESP32 3.3.8 and ESP-IDF 5.5.4.
