#pragma once
#include <functional>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

class TimerFreeRTOS {
  public:
    using TimeT        = TickType_t;
    using CallbackType = std::function<void()>;
    enum {
        TicksToWait = 0
    };

    explicit TimerFreeRTOS(TimeT period) noexcept;
    TimerFreeRTOS(const TimerFreeRTOS &other);
    TimerFreeRTOS &operator=(const TimerFreeRTOS &rhs);
    TimerFreeRTOS(TimerFreeRTOS &&other);
    TimerFreeRTOS &operator=(TimerFreeRTOS &&rhs);
    ~TimerFreeRTOS() noexcept;

    void SetNewCallback(CallbackType &&new_callback, int call_delay_ms);
    void InvokeCallback();
    TimeT static GetCurrentTime() noexcept;
  protected:
  private:
    TimerHandle_t freertosTimer = static_cast<TimerHandle_t>(nullptr);
    CallbackType  callback;
};
