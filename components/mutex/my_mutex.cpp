#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "my_mutex.hpp"

class Mutex::MutexImpl {
  public:
    MutexImpl()
      : _mutex{ xSemaphoreCreateMutex() }
    { }
    MutexImpl(const MutexImpl &rhs) = default;
    MutexImpl &operator=(const MutexImpl &rhs) = default;
    MutexImpl(MutexImpl &&rhs) = default;
    MutexImpl &operator=(MutexImpl &&) = default;
    ~MutexImpl()
    {
        if (_mutex != nullptr) {
            vSemaphoreDelete(_mutex);
        }
    }

    bool Take(portTickType ticks_to_wait) noexcept;
    bool Give() noexcept;

  private:
    SemaphoreHandle_t _mutex = nullptr;
};

bool
Mutex::MutexImpl::Take(TickType_t ticks_to_wait) noexcept
{
    if (xSemaphoreTake(_mutex, ticks_to_wait) == pdTRUE)
        return true;
    else
        return false;
}

bool
Mutex::MutexImpl::Give() noexcept
{
    if (xSemaphoreGive(_mutex) == pdTRUE)
        return true;
    else
        return false;
}

Mutex::Mutex()
  : impl{ std::make_unique<Mutex::MutexImpl>() }
{ }

Mutex::~Mutex() = default;

bool
Mutex::lock(TickType_t ticks_to_wait) noexcept
{
    return impl->Take(ticks_to_wait);
}

bool
Mutex::unlock() noexcept
{
    return impl->Give();
}
Mutex::Mutex(const Mutex &rhs)
  : impl{ std::make_unique<Mutex::MutexImpl>(*rhs.impl) }
{ }

Mutex &
Mutex::operator=(const Mutex &rhs)
{
    *impl = *rhs.impl;
    return *this;
}

Mutex &Mutex::operator=(Mutex &&) = default;

Mutex::Mutex(Mutex &&) = default;
