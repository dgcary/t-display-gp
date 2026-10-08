#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
using boolean = bool;
using byte = uint8_t;
unsigned long millis();
void yield();
void vTaskDelay(unsigned ticks);
#define pgm_read_byte_near(address) (*reinterpret_cast<const uint8_t*>(address))
class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* data, size_t length) {
    size_t count = 0;
    while (count < length) count += write(data[count]);
    return count;
  }
};
