#include "modbus.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/components/network/util.h"
#include <cerrno>

namespace esphome {
namespace modbustcp {
static const char *const TAG = "modbustcp";
static constexpr uint32_t RETRY_MS = 5000;
static constexpr uint32_t CONNECT_TIMEOUT_MS = 5000;
static constexpr size_t MAX_ADU = 260;
static uint16_t word(const uint8_t *p) { return (uint16_t(p[0]) << 8) | p[1]; }
static bool would_block() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }

void Modbus::setup() {
  if (!tcp_or_rtu_ || role != CLIENT) {
    ESP_LOGE(TAG, "Local TCP transport requires a TCP client; use native modbus for RTU");
    mark_failed();
  }
  rx_.reserve(MAX_ADU);
}
void Modbus::disconnect_() {
  // Destruction closes pending as well as established connections.
  socket_.reset();
  connected_ = false;
  waiting_for_response_ = 0;
  missed_responses_ = 0;
  tx_.clear();
  rx_.clear();
  tx_offset_ = 0;
  last_attempt_ = millis();
  attempted_ = true;
}
void Modbus::connect_() {
  last_attempt_ = millis();
  attempted_ = true;
  socket_ = socket::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (!socket_ || socket_->setblocking(false) != 0) { disconnect_(); return; }
  struct sockaddr_storage addr{};
  const auto len = socket::set_sockaddr(reinterpret_cast<sockaddr *>(&addr), sizeof(addr), host_.c_str(), port_);
  if (len == 0) { disconnect_(); return; }
  ESP_LOGD(TAG, "Connecting to %s:%u (nonblocking)", host_.c_str(), port_);
  const int result = socket_->connect(reinterpret_cast<sockaddr *>(&addr), len);
  if (result == 0) connected_ = true;
  else if (errno != EINPROGRESS && errno != EALREADY && !would_block()) {
    ESP_LOGW(TAG, "Connect failed: errno %d; retry in 5s", errno);
    disconnect_();
  }
}
void Modbus::loop() {
  if (!network::is_connected()) {
    if (socket_) disconnect_();
    return;
  }
  const uint32_t now = millis();
  if (!socket_) {
    if (!attempted_ || uint32_t(now - last_attempt_) >= RETRY_MS) connect_();
    return;
  }
  if (!connected_) {
    if (uint32_t(now - last_attempt_) >= CONNECT_TIMEOUT_MS) {
      ESP_LOGW(TAG, "Connect timed out; retry in 5s");
      disconnect_();
      return;
    }
    const int fd = socket_->get_fd();
    if (fd < 0 || fd >= FD_SETSIZE) { disconnect_(); return; }
    fd_set writable, errors;
    FD_ZERO(&writable);
    FD_ZERO(&errors);
    FD_SET(fd, &writable);
    FD_SET(fd, &errors);
    timeval timeout{};
    const int ready = select(fd + 1, nullptr, &writable, &errors, &timeout);
    if (ready < 0) { if (errno != EINTR) disconnect_(); return; }
    if (ready == 0) return;
    int error = 0;
    socklen_t size = sizeof(error);
    if (socket_->getsockopt(SOL_SOCKET, SO_ERROR, &error, &size) != 0 || error != 0) {
      ESP_LOGW(TAG, "Connect error %d; retry in 5s", error);
      disconnect_();
      return;
    }
    connected_ = true;
    ESP_LOGI(TAG, "Connected to %s:%u", host_.c_str(), port_);
  }
  flush_tx_();
  if (!socket_) return;
  receive_();
  if (!socket_) return;
  if (waiting_for_response_ && tx_.empty() && uint32_t(millis() - last_send_) > send_wait_time_) {
    waiting_for_response_ = 0;  // Late replies are ignored using transaction IDs.
    if (++missed_responses_ >= 3) {
      ESP_LOGW(TAG, "Three unanswered requests; reconnecting");
      disconnect_();
    }
  }
}
void Modbus::flush_tx_() {
  if (tx_.empty()) return;
  if (uint32_t(millis() - tx_started_) >= CONNECT_TIMEOUT_MS) { disconnect_(); return; }
  const auto count = socket_->write(tx_.data() + tx_offset_, tx_.size() - tx_offset_);
  if (count < 0) { if (!would_block()) disconnect_(); return; }
  if (count == 0) { disconnect_(); return; }
  tx_offset_ += count;
  if (tx_offset_ == tx_.size()) {
    tx_.clear();
    tx_offset_ = 0;
    last_send_ = millis();
  }
}
void Modbus::receive_() {
  // Bound work per loop so a bad peer cannot starve serial Modbus/API/Wi-Fi.
  for (unsigned budget = 0; budget < 4; ++budget) {
    uint8_t chunk[MAX_ADU];
    const auto count = socket_->read(chunk, MAX_ADU - rx_.size());
    if (count < 0) { if (!would_block()) disconnect_(); return; }
    if (count == 0) { disconnect_(); return; }
    rx_.insert(rx_.end(), chunk, chunk + count);
    while (rx_.size() >= 7) {
      const size_t length = word(rx_.data() + 4);
      if (word(rx_.data() + 2) != 0 || length < 2 || length > MAX_ADU - 6) {
        ESP_LOGW(TAG, "Invalid MBAP header; reconnecting");
        disconnect_();
        return;
      }
      const size_t size = length + 6;
      if (rx_.size() < size) break;
      if (!parse_frame_(rx_.data(), size)) {
        ESP_LOGW(TAG, "Invalid Modbus response; reconnecting");
        disconnect_();
        return;
      }
      rx_.erase(rx_.begin(), rx_.begin() + size);
    }
  }
}
bool Modbus::parse_frame_(const uint8_t *frame, size_t size) {
  if (!waiting_for_response_ || word(frame) != expected_transaction_ || frame[6] != waiting_for_response_)
    return true;  // Unsolicited/delayed response must not consume the current command.
  const uint8_t function = frame[7];
  if ((function & 0x7F) != expected_function_) return false;
  const bool exception = (function & 0x80) != 0;
  size_t offset = 8;
  if (exception) {
    if (size != 9) return false;
  } else if (function >= 1 && function <= 4) {
    if (size < 9 || size != size_t(frame[8]) + 9 || frame[8] != expected_bytes_) return false;
    offset = 9;
  } else if (function == 0x17) {
    if (size < 9 || size != size_t(frame[8]) + 9) return false;
    offset = 9;
  } else if (size != 12) return false;
  const uint8_t address = waiting_for_response_;
  waiting_for_response_ = 0;
  missed_responses_ = 0;
  for (auto *device : devices_) {
    if (device->address_ != address) continue;
    if (exception) device->on_modbus_error(function & 0x7F, frame[8]);
    else device->on_modbus_data(std::vector<uint8_t>(frame + offset, frame + size));
  }
  return true;
}
void Modbus::queue_frame_(std::vector<uint8_t> frame) {
  if (!connected_ || !socket_ || !network::is_connected() || waiting_for_response_ || !tx_.empty()) return;
  expected_transaction_ = word(frame.data());
  expected_function_ = frame[7];
  waiting_for_response_ = frame[6];
  tx_ = std::move(frame);
  tx_offset_ = 0;
  tx_started_ = millis();
  flush_tx_();
}
void Modbus::send(uint8_t address, uint8_t function, uint16_t start_address, uint16_t count,
                  uint8_t payload_len, const uint8_t *payload) {
  if (!connected_ || waiting_for_response_ || address == 0) return;
  if (function >= 1 && function <= 4) {
    if (count == 0 || count > (function <= 2 ? 2000 : 125)) return;
    expected_bytes_ = function <= 2 ? (count + 7) / 8 : count * 2;
  } else expected_bytes_ = 0;
  ++transaction_;
  std::vector<uint8_t> frame{uint8_t(transaction_ >> 8), uint8_t(transaction_), 0, 0, 0, 0,
                             address, function, uint8_t(start_address >> 8), uint8_t(start_address)};
  if (function != 5 && function != 6) { frame.push_back(count >> 8); frame.push_back(count); }
  if (payload != nullptr) {
    if (function == 0x0F || function == 0x10 || function == 0x17) frame.push_back(payload_len);
    else if (payload_len != 2) return;
    frame.insert(frame.end(), payload, payload + payload_len);
  }
  if (frame.size() > MAX_ADU) return;
  const uint16_t length = frame.size() - 6;
  frame[4] = length >> 8;
  frame[5] = length;
  queue_frame_(std::move(frame));
}
void Modbus::send_raw(const std::vector<uint8_t> &payload) {
  // Raw Modbus request: unit + PDU, no RTU CRC. Add the MBAP header.
  if (payload.size() < 2 || payload.size() > MAX_ADU - 6 || !connected_ || waiting_for_response_) return;
  ++transaction_;
  std::vector<uint8_t> frame{uint8_t(transaction_ >> 8), uint8_t(transaction_), 0, 0,
                             uint8_t(payload.size() >> 8), uint8_t(payload.size())};
  frame.insert(frame.end(), payload.begin(), payload.end());
  expected_bytes_ = (payload[1] >= 1 && payload[1] <= 4 && payload.size() >= 6)
                        ? (payload[1] <= 2 ? (word(payload.data() + 4) + 7) / 8 : word(payload.data() + 4) * 2) : 0;
  queue_frame_(std::move(frame));
}
void Modbus::on_shutdown() { disconnect_(); }
float Modbus::get_setup_priority() const { return setup_priority::AFTER_WIFI; }
void Modbus::dump_config() {
  ESP_LOGCONFIG(TAG, "Local Modbus TCP recovery v1: %s:%u, response wait %u ms", host_.c_str(), port_, send_wait_time_);
}
}  // namespace modbustcp
}  // namespace esphome
