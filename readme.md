# Fork that attempts to allow parallel use of both modbustcp TCP and esphome native modbus RTU
# Includes [text](components/exampleTCP_RTU_huawei_delta_max_basic.yaml) example tested on esp32-WROOM-32U board with RTU to RS485 conversion board. Connects via esphome native modbus RTU to ev charger, and via modbus TCP over wifi to huawei inverter to read grid power and solar production. Feel free to adapt params and logic according to your needs
# Use components/modsbustcp and modbustcp-controller to avoid overwriting the native RTU modbus of esphome and allow parallel use (RTU + TCP)

# Framework IDF or Arduino

# for modbus TCP
```yaml
external_components:
  - source: github://creepystefan/esphome_tcp
    refresh: 0s
esphome:
  min_version: 2026.2.4
  
modbus:
  - id: modbustesttcp
    type: TCP               # RTU no use please
    host: 192.168.178.46    # Required
    port: 502               # Optional 502 is default
    send_wait_time: 250ms   # Optional 250ms is default
```
all Components orignal from ESPHOME

in modbus_controller:  address = UNIT ID
platform: modbus_controller
sensor
number
switch
textsensor
....


# useful link
https://ipc2u.de/artikel/wissenswertes/detaillierte-beschreibung-des-modbus-tcp-protokolls-mit-befehlsbeispielen/
# https://github.com/Gucioo/esphome_modbus_tcp_master
