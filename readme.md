# Fork that attempts to allow parallel use of both modbustcp TCP and esphome native modbus RTU
# Includes [text](components/exampleTCP_RTU_huawei_delta_max_basic.yaml) example tested on esp32-WROOM-32U board with RTU to RS485 conversion board. Connects via esphome native modbus RTU to ev charger, and via modbus TCP over wifi to huawei inverter to read grid power and solar production. Feel free to adapt params and logic according to your needs
# Use components/modsbustcp and modbustcp-controller to avoid overwriting the native RTU modbus of esphome and allow parallel use (RTU + TCP)

# Framework IDF or Arduino

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
