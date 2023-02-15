#pragma once
#include "../../../../../../Libraries/esp32/tools/tools/xtensa-esp32-elf/esp-2021r2-patch5-8.4.0/xtensa-esp32-elf/xtensa-esp32-elf/include/c++/8.4.0/memory"
#include "../../../../../../Libraries/esp32/esp-idf-v4.4.3/components/freertos/include/freertos/FreeRTOS.h"

class Mutex {
  public:
    Mutex();
    Mutex(const Mutex &);
    Mutex &operator=(const Mutex &);
    Mutex(Mutex &&);
    Mutex &operator=(Mutex &&);
    ~Mutex();

    bool lock(portTickType ticks_to_wait = portMAX_DELAY) noexcept;
    bool unlock() noexcept;

  private:
    class MutexImpl;
    std::unique_ptr<MutexImpl> impl;
};
