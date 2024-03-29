#pragma once

#include <functional>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void   TaskCppTaskWrapper(void *);
[[noreturn]] void TaskLoopBreachHook(TaskHandle_t task, std::string name);
[[noreturn]] void TaskFunctorIsNullHook();

class Task {
  public:
    using TaskFunctor = std::function<void()>;
    using TimeT       = portTickType;
    using TickT       = portTickType;
    using CoreNumT    = BaseType_t;
    enum {
        TaskCreationSucceeded = pdPASS
    };
    Task(TaskFunctor &&new_functor,
         size_t        stackSize,
         size_t        new_priority,
         std::string   new_name,
         CoreNumT      runs_on_core,
         bool          suspended = false)
      : functor{ std::move(new_functor) }
      , name{ std::move(new_name) }
      , stackSize{ stackSize }
      , priority{ new_priority }
      , core{ runs_on_core }
    {
        if (not suspended)
            configASSERT(CreateFreeRTOSTask());
    }
    Task(TaskFunctor &&new_functor, size_t stacksize, size_t new_priority, std::string new_name, bool suspended = false)
      : Task{ std::move(new_functor), stacksize, new_priority, new_name, tskNO_AFFINITY, suspended }
    { }

    Task(Task const &)            = delete;
    Task &operator=(Task const &) = delete;
    Task(Task &&)                 = default;
    Task &operator=(Task &&)      = default;
    ~Task() noexcept { DeleteTask(); }

    void Reset() noexcept
    {
        if (deleted)
            return;

        Suspend();
        vTaskDelete(taskHandle);
        configASSERT(CreateFreeRTOSTask());
    }

    [[noreturn]] void Run() noexcept
    {
        if ((not functor) or deleted)
            TaskFunctorIsNullHook();

        functor();

        // should not get here
        TaskLoopBreachHook(taskHandle, name);
    }
    void Start() noexcept
    {
        if (hasBeenStarted or deleted)
            return;

        configASSERT(CreateFreeRTOSTask());
    }
    void Stop() noexcept { DeleteTask(); }
    void Suspend() noexcept
    {
        if ((not functor) or deleted)
            TaskFunctorIsNullHook();

        vTaskSuspend(taskHandle);
    }
    void Resume() noexcept
    {
        if ((not functor) or deleted)
            TaskFunctorIsNullHook();

        vTaskResume(taskHandle);
    }

    static void DelayMs(TimeT for_ms) noexcept { vTaskDelay(pdMS_TO_TICKS(for_ms)); }
    static void DelayTicks(TickT ticks) noexcept { vTaskDelay(ticks); }
    static void DelayMsUntil(TimeT for_ms) noexcept
    {
        auto time_now = xTaskGetTickCount();
        vTaskDelayUntil(&time_now, pdMS_TO_TICKS(for_ms));
    }
    static void SuspendAll() noexcept { vTaskSuspendAll(); }
    static void ResumeAll() noexcept { xTaskResumeAll(); }

    static UBaseType_t GetStackWatermarkOfThisTask() noexcept { return uxTaskGetStackHighWaterMark(nullptr); }

    UBaseType_t GetStackWatermark() noexcept
    {
        if (deleted)
            return 1;
        return uxTaskGetStackHighWaterMark(taskHandle);
    }

  protected:
    bool CreateFreeRTOSTask() noexcept
    {
        auto retval =
          xTaskCreatePinnedToCore(TaskCppTaskWrapper, name.c_str(), stackSize, this, priority, &taskHandle, core) ==
              TaskCreationSucceeded
            ? true
            : false;

        hasBeenStarted = true;

        return retval;
    }
    void DeleteTask() noexcept
    {
        if (taskHandle != nullptr) {
            vTaskDelete(taskHandle);
            deleted = true;
        }
    }

  private:
    TaskHandle_t taskHandle{ nullptr };
    TaskFunctor  functor;

    std::string name;
    size_t      stackSize;
    size_t      priority;
    CoreNumT    core;
    bool        hasBeenStarted{ false };
    bool        deleted{ false };
};