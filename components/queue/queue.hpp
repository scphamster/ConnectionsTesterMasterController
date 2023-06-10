#pragma once

#include <deque>
#include <optional>
#include <mutex>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "semaphore.hpp"
#include "freertos/stream_buffer.h"
#include "my_mutex.hpp"
template<typename ItemType>
class Queue {
  public:
    using TimeT = portTickType;
    using Byte  = uint8_t;

    explicit Queue(size_t queue_length)
      : handle{ xQueueCreate(queue_length, sizeof(ItemType)) }
    {
        configASSERT(handle != nullptr);
    }

    explicit Queue(size_t queue_length, std::string queue_debug_name)
      : Queue{ queue_length }
    {
        vQueueAddToRegistry(handle, queue_debug_name.c_str());
    }

    ~Queue() noexcept
    {
        configASSERT(handle != nullptr);
        vQueueDelete(handle);
    }

    std::optional<ItemType> Receive(TimeT timeout = portMAX_DELAY)
    {
        std::array<Byte, sizeof(ItemType)> buffer{};

        if (xQueueReceive(handle, buffer.data(), timeout) == pdTRUE)
            return *reinterpret_cast<ItemType *>(buffer.data());
        else
            return std::nullopt;
    }

    bool Send(ItemType const &item, TimeT timeout_ms) const noexcept
    {
        return (xQueueSend(handle, static_cast<const void *>(&item), pdMS_TO_TICKS(timeout_ms)) == pdTRUE) ? true : false;
    }
    bool Send(ItemType const &item) const noexcept
    {
        return (xQueueSend(handle, static_cast<const void *>(&item), portMAX_DELAY) == pdTRUE) ? true : false;
    }
    bool SendImmediate(ItemType const &item) const noexcept { return Send(item, 0); }
    void Flush() noexcept
    {
        ItemType dummy_item;
        while (xQueueReceive(handle, &dummy_item, 0) == pdTRUE) { }
    }

  private:
    QueueHandle_t handle = nullptr;
};

template<typename ItemType>
class Deque {
  public:
  protected:
  private:
    Mutex                pushMutex;
    Mutex                popMutex;
    std::deque<ItemType> queue;
};

template<typename ItemType>
class StreamBuffer {
  public:
    using TimeoutMsec = portTickType;

    StreamBuffer(size_t buffer_items_capacity, size_t receiver_unblock_triggering_size = 1) noexcept
      : handle{ xStreamBufferCreate(buffer_items_capacity * sizeof(ItemType),
                                    receiver_unblock_triggering_size * sizeof(ItemType)) }
    {
        configASSERT(handle != nullptr);
    }

    template<size_t NumberOfItems>
    void Send(std::array<ItemType, NumberOfItems> &buffer, TimeoutMsec timeout) noexcept
    {
        xStreamBufferSend(handle, buffer.data(), sizeof(buffer), timeout);
    }

    void Send(ItemType &&buffer, TimeoutMsec timeout) noexcept
    {
        xStreamBufferSend(handle, &buffer, sizeof(buffer), timeout);
    }

    bool Reset() const noexcept { return (xStreamBufferReset(handle) == pdPASS) ? true : false; }

    template<typename T>
    BaseType_t SendFromISR(T &&buffer) noexcept
    {
        BaseType_t higher_prio_task_voken;
        xStreamBufferSendFromISR(handle, &buffer, sizeof(buffer), &higher_prio_task_voken);
        return higher_prio_task_voken;
    }

    ItemType Receive(TimeoutMsec timeout) noexcept
    {
        ItemType buffer;
        xStreamBufferReceive(handle, &buffer, sizeof(buffer), timeout);
        return buffer;
    }

    template<size_t NumberOfItems>
    std::array<ItemType, NumberOfItems> Receive(TimeoutMsec timeout) noexcept
    {
        std::array<ItemType, NumberOfItems> buffer;
        auto                                received_number_of_bytes =
          xStreamBufferReceive(handle, buffer.data(), buffer.size() * sizeof(ItemType), timeout);

        return buffer;
    }

    template<size_t NumberOfItems>
    std::array<ItemType, NumberOfItems> ReceiveBlocking(TimeoutMsec timeout) noexcept
    {
        std::array<ItemType, NumberOfItems> buffer;
        auto constexpr buffer_size_in_bytes = buffer.size() * sizeof(ItemType);

        for (size_t received_number_of_bytes = 0; received_number_of_bytes != buffer_size_in_bytes;) {
            received_number_of_bytes =
              xStreamBufferReceive(handle, buffer.data(), buffer.size() * sizeof(ItemType), timeout);
        }

        return buffer;
    }

  private:
    StreamBufferHandle_t handle;
};

/**
 * @brief StreamBuffer specifically designed for std::vector<Byte> transfers
 */
class ByteStreamBuffer {
  public:
    using TimeoutMsec = portTickType;
    using Byte        = uint8_t;

    ByteStreamBuffer(size_t capacity, TimeoutMsec config_buffer_operation_retry_timeoutMS = 50) noexcept
      : handle{ xStreamBufferCreate(capacity, UNLOCK_TRIGGER_SIZE) }
      , configBufferOperationTimeout{ config_buffer_operation_retry_timeoutMS }
    {
        configASSERT(handle != nullptr);
    }

    bool Send(const std::vector<Byte> &bytes_to_send, TimeoutMsec timeoutMsec = portMAX_DELAY) noexcept
    {
        if (timeoutMsec != portMAX_DELAY)
            timeoutMsec = pdMS_TO_TICKS(timeoutMsec);

        auto bytes_number = bytes_to_send.size();

        size_t bytes_written_num{};

        bytes_written_num = xStreamBufferSend(handle, &bytes_number, sizeof(bytes_number), timeoutMsec);
        if (bytes_written_num != sizeof(bytes_number))
            return false;

        bytes_written_num = xStreamBufferSend(handle, bytes_to_send.data(), bytes_number, timeoutMsec);

        return (bytes_written_num == bytes_number) ? true : false;
    }

    std::optional<std::vector<Byte>> Receive(TimeoutMsec timeoutMsec = portMAX_DELAY) noexcept
    {
        size_t message_size{};
        size_t bytes_read_number{};

        if (timeoutMsec != portMAX_DELAY)
            timeoutMsec = pdMS_TO_TICKS(timeoutMsec);

        auto deadline = esp_timer_get_time() + timeoutMsec * 1000;

        while (bytes_read_number != sizeof(message_size)) {
            bytes_read_number += xStreamBufferReceive(handle,
                                                      reinterpret_cast<Byte *>(&message_size) + bytes_read_number,
                                                      sizeof(message_size) - bytes_read_number,
                                                      pdMS_TO_TICKS(configBufferOperationTimeout));

            if (deadline < esp_timer_get_time())
                return std::nullopt;
        }

        if (message_size == 0) {
            return std::nullopt;
        }

        bytes_read_number = 0;
        auto data         = std::vector<Byte>(message_size);
        while (bytes_read_number != message_size) {
            bytes_read_number += xStreamBufferReceive(handle,
                                                      data.data() + bytes_read_number,
                                                      message_size - bytes_read_number,
                                                      pdMS_TO_TICKS(configBufferOperationTimeout));

            if (deadline < esp_timer_get_time())
                return std::nullopt;
        }

        return data;
    }

  private:
    constexpr static size_t UNLOCK_TRIGGER_SIZE = 1;
    StreamBufferHandle_t    handle;
    TimeoutMsec             configBufferOperationTimeout;
};