#pragma once
#include "esphome/core/component.h"
#include "esphome/components/socket/socket.h"
#include "esphome/components/uart/uart.h"
#include "modbus_definitions.h"
#include <memory>
#include <vector>

namespace esphome {
namespace modbustcp {
enum ModbusRole { CLIENT, SERVER };
class ModbusDevice;

// TCP only. Charger RTU remains ESPHome's native modbus component.
// All transport and controller operations run on the ESPHome main loop.
class Modbus : public uart::UARTDevice, public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  void on_shutdown() override;
  float get_setup_priority() const override;
  void set_host(const std::string &host) { host_ = host; }
  void set_port(uint16_t port) { port_ = port; }
  void set_tcp_or_rtu(bool value) { tcp_or_rtu_ = value; }
  void set_role(ModbusRole value) { role = value; }
  void set_flow_control_pin(GPIOPin *) {}
  void set_disable_crc(bool) {}
  void set_send_wait_time(uint16_t value) { send_wait_time_ = value; }
  void set_turnaround_time(uint16_t value) { turnaround_delay_ms_ = value; }
  void register_device(ModbusDevice *device) { devices_.push_back(device); }
  void send(uint8_t address, uint8_t function, uint16_t start_address, uint16_t count,
            uint8_t payload_len = 0, const uint8_t *payload = nullptr);
  void send_raw(const std::vector<uint8_t> &payload);
  uint8_t waiting_for_response_{0};
  ModbusRole role{CLIENT};
 protected:
  void connect_();
  void disconnect_();
  void flush_tx_();
  void receive_();
  bool parse_frame_(const uint8_t *frame, size_t size);
  void queue_frame_(std::vector<uint8_t> frame);
  std::unique_ptr<socket::Socket> socket_;
  bool connected_{false};
  bool tcp_or_rtu_{false};
  bool attempted_{false};
  uint16_t port_{502};
  std::string host_;
  uint32_t last_attempt_{0};
  uint32_t last_send_{0};
  uint32_t tx_started_{0};
  uint16_t send_wait_time_{250};
  uint16_t turnaround_delay_ms_{100};
  uint16_t transaction_{0};
  uint16_t expected_transaction_{0};
  uint8_t expected_function_{0};
  uint16_t expected_bytes_{0};
  uint8_t missed_responses_{0};
  std::vector<uint8_t> tx_;
  size_t tx_offset_{0};
  std::vector<uint8_t> rx_;
  std::vector<ModbusDevice *> devices_;
};
class ModbusDevice {
 public:
  void set_parent(Modbus *parent) { parent_ = parent; }
  void set_address(uint8_t address) { address_ = address; }
  virtual void on_modbus_data(const std::vector<uint8_t> &data) = 0;
  virtual void on_modbus_error(uint8_t, uint8_t) {}
  virtual void on_modbus_read_registers(uint8_t, uint16_t, uint16_t) {}
  virtual void on_modbus_write_registers(uint8_t, const std::vector<uint8_t> &) {}
  void send(uint8_t function, uint16_t start_address, uint16_t count,
            uint8_t payload_len = 0, const uint8_t *payload = nullptr) {
    parent_->send(address_, function, start_address, count, payload_len, payload);
  }
  void send_raw(const std::vector<uint8_t> &payload) { parent_->send_raw(payload); }
  void send_error(uint8_t function, ModbusExceptionCode exception) {
    parent_->send_raw({address_, static_cast<uint8_t>(function | FUNCTION_CODE_EXCEPTION_MASK),
                       static_cast<uint8_t>(exception)});
  }
  bool waiting_for_response() { return parent_->waiting_for_response_ != 0; }
 protected:
  friend class Modbus;
  Modbus *parent_{nullptr};
  uint8_t address_{0};
};
}  // namespace modbustcp
}  // namespace esphome
