#pragma once

namespace ProjCfg
{
enum BluetoothCfg {
    SppQueueLen            = 10,
    SppTaskStackSize       = 4000,
    SppTaskPrio            = 10,
    SppSendTimeoutMs       = 10,
    SppReaderTaskStackSize = 2048,
    SppReaderTaskPrio      = 5,
};

enum class Tasks {
};
}