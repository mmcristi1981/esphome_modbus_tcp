#pragma once
#include <cstdint>
#include <string>
namespace esphome {
extern uint32_t test_clock;
inline uint32_t millis() { return test_clock; }
class GPIOPin {};
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  virtual void on_shutdown() {}
  virtual float get_setup_priority() const { return 0; }
  void mark_failed() {}
};
namespace setup_priority { const float AFTER_WIFI = 200; }
}
