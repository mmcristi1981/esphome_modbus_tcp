#pragma once
namespace esphome { namespace network {
extern bool available;
inline bool is_connected() { return available; }
} }
