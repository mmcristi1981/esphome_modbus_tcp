#include "modbus.h"

#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/components/network/util.h"

namespace esphome::modbustcp {

static const char *const TAG = "modbustcp";
static constexpr size_t MODBUS_MAX_LOG_BYTES = 64;


// -----------------------------------------------------------------------------
// TCP client lifecycle
// -----------------------------------------------------------------------------

void Modbus::create_client_() {
  client_ = new AsyncClient();

  ESP_LOGD("tcp", "Created new TCP client");

  client_->onConnect([](void *arg, AsyncClient *c) {
    auto *self = static_cast<Modbus *>(arg);

    ESP_LOGI("tcp",
             "Connected to %s:%d",
             self->host_.c_str(),
             self->port_);

    self->connected_ = true;
    self->reset_client_pending_ = false;
    self->last_attempt_ = millis();

  }, this);


  client_->onDisconnect([](void *arg, AsyncClient *c) {
    auto *self = static_cast<Modbus *>(arg);

    ESP_LOGW("tcp",
             "Disconnected from %s:%d, TCP client will be recreated",
             self->host_.c_str(),
             self->port_);

    self->connected_ = false;
    self->waiting_for_response_ = 0;

    // IMPORTANT:
    // Do not delete AsyncClient while executing one of its callbacks.
    self->schedule_client_reset_();

  }, this);


  client_->onError([](void *arg, AsyncClient *c, int8_t err) {
    auto *self = static_cast<Modbus *>(arg);

    ESP_LOGE("tcp",
             "TCP error: %s",
             c->errorToString(err));

    self->connected_ = false;
    self->waiting_for_response_ = 0;

    // Actual delete/new happens later from loop().
    self->schedule_client_reset_();

  }, this);


  client_->onData([](void *arg,
                     AsyncClient *c,
                     void *data,
                     size_t len) {
    auto *self = static_cast<Modbus *>(arg);

    if (data == nullptr || len == 0) {
      return;
    }

    uint8_t *byte = reinterpret_cast<uint8_t *>(data);

    self->handle_message(byte);

  }, this);
}


void Modbus::schedule_client_reset_() {
  reset_client_pending_ = true;
}


void Modbus::reset_client_() {
  ESP_LOGW("tcp", "Resetting TCP client");

  connected_ = false;
  waiting_for_response_ = 0;

  if (client_ != nullptr) {
    // close() is part of ESPHome AsyncClient API.
    client_->close();

    delete client_;
    client_ = nullptr;
  }

  create_client_();

  // create_client_() installs all callbacks on a fresh object.
  reset_client_pending_ = false;

  // Permit reconnect on the next pass through loop().
  last_attempt_ = 0;

  ESP_LOGW("tcp", "TCP client recreated");
}


// -----------------------------------------------------------------------------
// ESPHome setup
// -----------------------------------------------------------------------------

void Modbus::setup() {
  if (tcp_or_rtu_) {
    create_client_();
  }
}


// -----------------------------------------------------------------------------
// TCP connect
// -----------------------------------------------------------------------------

void Modbus::connect() {
  if (!tcp_or_rtu_) {
    return;
  }

  if (client_ == nullptr) {
    ESP_LOGW("tcp", "TCP client missing, creating new client");
    create_client_();
  }

  if (connected_) {
    return;
  }

  if (client_->connecting()) {
    ESP_LOGD("tcp", "TCP connection attempt already in progress");
    return;
  }

  ESP_LOGD("tcp",
           "Connecting to %s:%d",
           host_.c_str(),
           port_);

  if (!client_->connect(host_.c_str(), port_)) {
    ESP_LOGW("tcp",
             "Connection failed, TCP client will be recreated");

    connected_ = false;
    waiting_for_response_ = 0;

    // connect() was called from loop(), so it would technically be
    // possible to reset immediately here. Keeping all resets deferred
    // gives us one safe and predictable reset path.
    schedule_client_reset_();
  }
}


// -----------------------------------------------------------------------------
// Incoming TCP Modbus response
// -----------------------------------------------------------------------------

void Modbus::handle_message(uint8_t byte[256]) {
  std::string res;
  char buf[5];

  size_t data_len = byte[8];

  for (size_t i = 9; i < data_len + 9; i++) {
    sprintf(buf, "%02X", byte[i]);
    res += buf;
    res += ":";
  }

  ESP_LOGD(TAG,
           "<<< %02X%02X %02X%02X %02X%02X %02X %02X %02X %s ",
           byte[0],
           byte[1],
           byte[2],
           byte[3],
           byte[4],
           byte[5],
           byte[6],
           byte[7],
           byte[8],
           res.c_str());

  if (this->parse_modbus_byte_(byte)) {
    // Response parsed
  }
}


// -----------------------------------------------------------------------------
// Shutdown
// -----------------------------------------------------------------------------

void Modbus::on_shutdown() {
  connected_ = false;
  waiting_for_response_ = 0;

  if (client_ != nullptr) {
    client_->close();

    delete client_;
    client_ = nullptr;
  }
}


// -----------------------------------------------------------------------------
// Main loop
// -----------------------------------------------------------------------------

void Modbus::loop() {
  const uint32_t now = App.get_loop_component_start_time();

  if (tcp_or_rtu_) {
    // IMPORTANT:
    // Recreate AsyncClient here rather than inside onDisconnect/onError.
    if (reset_client_pending_) {
      reset_client_();
    }

    if (!connected_) {
      const uint32_t nowtcp = millis();

      if (nowtcp - last_attempt_ > 5000) {
        last_attempt_ = nowtcp;

        ESP_LOGW("tcp",
                 "Reconnecting to %s:%d...",
                 host_.c_str(),
                 port_);

        connect();
      }
    }
  }

  if (now - this->last_send_ > send_wait_time_) {
    if (waiting_for_response_ > 0) {
      ESP_LOGV(TAG,
               "Stop waiting for response from %d",
               waiting_for_response_);
    }

    waiting_for_response_ = 0;
  }
}


// -----------------------------------------------------------------------------
// Parse Modbus TCP response
// -----------------------------------------------------------------------------

bool Modbus::parse_modbus_byte_(uint8_t byte[256]) {
  uint8_t bytelen_len = 9;

  size_t data_len = byte[8];

  uint8_t address = byte[6];
  uint8_t function_code = byte[7];

  std::vector<uint8_t> data(
      byte + bytelen_len,
      byte + bytelen_len + bytelen_len + data_len);

  bool found = false;

  for (auto *device : this->devices_) {
    if (device->address_ == address) {
      found = true;

      // Is it an error response?
      if ((function_code & FUNCTION_CODE_EXCEPTION_MASK) ==
          FUNCTION_CODE_EXCEPTION_MASK) {

        ESP_LOGD(TAG,
                 "Modbus error function code: 0x%X exception: %d",
                 function_code,
                 byte[8]);

        if (waiting_for_response_ != 0) {
          device->on_modbus_error(
              function_code & FUNCTION_CODE_MASK,
              byte[8]);
        } else {
          ESP_LOGD(TAG,
                   "Ignoring Modbus error - not expecting a response");
        }

        continue;
      }

      device->on_modbus_data(data);
    }

    waiting_for_response_ = 0;
  }

  return true;
}


// -----------------------------------------------------------------------------
// Configuration dump
// -----------------------------------------------------------------------------

void Modbus::dump_config() {
  if (tcp_or_rtu_) {
    ESP_LOGCONFIG(TAG,
                  "Modbus_TCP: \n"
                  "  Host: %s:%d \n"
                  "  Send Wait Time: %d ms\n"
                  "  Turnaround Time: %d ms\n",
                  host_.c_str(),
                  port_,
                  this->send_wait_time_,
                  this->turnaround_delay_ms_);

  } else {
    ESP_LOGCONFIG(TAG,
                  "Modbus_RTU:\n"
                  "  Send Wait Time: %d ms\n"
                  "  Turnaround Time: %d ms\n"
                  "  CRC Disabled: %s",
                  this->send_wait_time_,
                  this->turnaround_delay_ms_,
                  YESNO(this->disable_crc_));
  }
}


// -----------------------------------------------------------------------------
// Setup priority
// -----------------------------------------------------------------------------

float Modbus::get_setup_priority() const {
  if (tcp_or_rtu_) {
    return setup_priority::AFTER_WIFI;
  }

  return setup_priority::BUS - 1.0f;
}


// -----------------------------------------------------------------------------
// Send Modbus request
// -----------------------------------------------------------------------------

void Modbus::send(uint8_t address,
                  uint8_t function_code,
                  uint16_t start_address,
                  uint16_t number_of_entities,
                  uint8_t payload_len,
                  const uint8_t *payload) {
  std::vector<uint8_t> data_send;

  if (tcp_or_rtu_) {
    Transaction_Identifier++;

    // MBAP header
    data_send.push_back(Transaction_Identifier >> 8);
    data_send.push_back(Transaction_Identifier >> 0);

    data_send.insert(data_send.end(), {0x00, 0x00, 0x00});

    if (payload != nullptr) {
      data_send.push_back(0x04 + payload_len);
    } else {
      data_send.push_back(0x06);
    }
  }

  data_send.push_back(address);
  data_send.push_back(function_code);

  if (this->role == ModbusRole::CLIENT) {
    data_send.push_back(start_address >> 8);
    data_send.push_back(start_address >> 0);

    if (function_code != ModbusFunctionCode::WRITE_SINGLE_COIL &&
        function_code != ModbusFunctionCode::WRITE_SINGLE_REGISTER) {

      data_send.push_back(number_of_entities >> 8);
      data_send.push_back(number_of_entities >> 0);
    }
  }

  if (payload != nullptr) {
    if (function_code == 0x0F ||
        function_code == 0x17) {
      // Byte count required for multiple write.
      data_send.push_back(payload_len);
    } else {
      // Write single register or coil.
      payload_len = 2;
    }

    for (int i = 0; i < payload_len; i++) {
      data_send.push_back(payload[i]);
    }
  }

  if (tcp_or_rtu_) {
    std::string res1;
    char buf1[5];

    for (size_t i = 12;
         i < data_send[5] + 6 && i < data_send.size();
         i++) {
      sprintf(buf1, "%02X", data_send[i]);
      res1 += buf1;
      res1 += ":";
    }

    if (connected_ &&
        client_ != nullptr) {

      // IMPORTANT FIX:
      // Original code used sizeof(data_send), which is sizeof(std::vector),
      // not the number of bytes in the Modbus TCP packet.
      const size_t written =
          client_->write(
              reinterpret_cast<const char *>(data_send.data()),
              data_send.size());

      if (written != data_send.size()) {
        ESP_LOGW("tcp",
                 "TCP write incomplete: %u/%u bytes",
                 static_cast<unsigned>(written),
                 static_cast<unsigned>(data_send.size()));

        connected_ = false;
        waiting_for_response_ = 0;
        schedule_client_reset_();

      } else {
        ESP_LOGD(
            TAG,
            ">>> %02X%02X %02X%02X %02X%02X %02X %02X "
            "%02X%02X %02X%02X %s",
            data_send.size() > 0 ? data_send[0] : 0,
            data_send.size() > 1 ? data_send[1] : 0,
            data_send.size() > 2 ? data_send[2] : 0,
            data_send.size() > 3 ? data_send[3] : 0,
            data_send.size() > 4 ? data_send[4] : 0,
            data_send.size() > 5 ? data_send[5] : 0,
            data_send.size() > 6 ? data_send[6] : 0,
            data_send.size() > 7 ? data_send[7] : 0,
            data_send.size() > 8 ? data_send[8] : 0,
            data_send.size() > 9 ? data_send[9] : 0,
            data_send.size() > 10 ? data_send[10] : 0,
            data_send.size() > 11 ? data_send[11] : 0,
            res1.c_str());
      }

    } else {
      ESP_LOGW("tcp",
               "Cannot send Modbus request - TCP not connected");
    }
  }

  waiting_for_response_ = address;
  last_send_ = millis();
}


// -----------------------------------------------------------------------------
// Send raw Modbus request
// -----------------------------------------------------------------------------

void Modbus::send_raw(const std::vector<uint8_t> &payload) {
  if (payload.empty()) {
    return;
  }

  if (tcp_or_rtu_) {
    if (!connected_ || client_ == nullptr) {
      ESP_LOGW("tcp",
               "Cannot send raw Modbus request - TCP not connected");
      return;
    }

    // IMPORTANT FIX:
    // Original code used sizeof(payload), which is sizeof(std::vector).
    const size_t written =
        client_->write(
            reinterpret_cast<const char *>(payload.data()),
            payload.size());

    if (written != payload.size()) {
      ESP_LOGW("tcp",
               "Raw TCP write incomplete: %u/%u bytes",
               static_cast<unsigned>(written),
               static_cast<unsigned>(payload.size()));

      connected_ = false;
      waiting_for_response_ = 0;
      schedule_client_reset_();

      return;
    }
  }

  waiting_for_response_ = payload[0];

  ESP_LOGV(TAG,
           "Modbus write raw: %s",
           format_hex_pretty(payload).c_str());

  last_send_ = millis();
}

}  // namespace esphome::modbustcp