# Fork that attempts to allow parallel use of both modbustcp TCP and esphome native modbus RTU
# Includes [text](components/exampleTCP_RTU_huawei_delta_max_basic.yaml) example tested on esp32-WROOM-32U board with RTU to RS485 conversion board. Connects via esphome native modbus RTU to ev charger, and via modbus TCP over wifi to huawei inverter to read grid power and solar production. Feel free to adapt params and logic according to your needs
# Use components/modsbustcp and modbustcp-controller to avoid overwriting the native RTU modbus of esphome and allow parallel use (RTU + TCP)

## TCP connection recovery

The `modbustcp` transport uses ESPHome's nonblocking `socket` component and runs
on the ESPHome main loop. It waits for network connectivity, releases pending and
established sockets on failure, and retries after five seconds. It bounds work
per loop, handles partial writes and fragmented/coalesced responses, and matches
responses to the outstanding transaction. Three unanswered requests trigger a
reconnect. `modbustcp_controller` also guards against receiving a response with
an empty command queue.

This transport supports **TCP client mode only**. Use ESPHome's built-in `modbus`
and `modbus_controller` for serial RTU. The legacy `type: RTU` and server options
inside `modbustcp` are not supported by this implementation; setup marks those
modes as failed. The separate legacy `components/modbus` and
`components/modbus_controller` directories are not changed or loaded by the
external-component selection below.

Dependency roles:

| Purpose | Component/source |
| --- | --- |
| TCP Modbus client | This repository: `modbustcp` and `modbustcp_controller` |
| TCP socket implementation | ESPHome built-in `socket`; this transport no longer uses AsyncTCP |
| Serial RTU | ESPHome built-in `uart`, `modbus` and `modbus_controller` |
| Wi-Fi, API, web server and OTA | ESPHome built-in components selected by your configuration |

The `robertklep/esphome-custom-component` source shown in the example is needed
only when a configuration actually uses its legacy custom components. Listing it
under `external_components` alone does not instantiate those components.

External components are resolved at build time and compiled into the firmware;
a running device does not fetch GitHub updates. Rebuild and flash to apply a
repository change. ESPHome 2026.5.0 with Arduino-ESP32 3.3.8 and ESP-IDF 5.5.4 was
used for the recovery firmware build. Other framework/version combinations have
not been validated for this change.

See [transport tests](tests/README.md) for the simulated failure/recovery checks.
Physical power-cycle and delayed-access-point recovery still require validation.
This change does not alter application charging logic or add a stale-sensor
policy.

## Framework

# for modbus TCP
```yaml

esphome:
  name: example-controller
  platformio_options:
    lib_ldf_mode: chain+

esp32:
  board: esp32dev
  framework:
    type: arduino

external_components:
  - source: github://robertklep/esphome-custom-component
    components: [ custom, custom_component ]
  - source: github://mmcristi1981/esphome_modbus_tcp
    components: [ modbustcp, modbustcp_controller ]
    refresh: 0s

modbustcp:
  - id: my_tcp_modbus
    type: TCP
    host: 192.168.1.100 # your modbus device IP address
    port: 502 # your device modbus port
    send_wait_time: 5s

modbustcp_controller:
  - id: my_modbus_controller
    modbustcp_id: my_tcp_modbus
    address: 1
    update_interval: 5s
    setup_priority: -100
```
