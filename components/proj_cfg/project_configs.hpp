#pragma once
#include <cstdlib>

namespace ProjCfg
{
enum BluetoothCfg {
    SppQueueLen            = 10,
    SppTaskStackSize       = 4000,
    SppTaskPrio            = 10,
    SppSendTimeoutMs       = 10,
    SppReaderTaskStackSize = 3096,
    SppReaderTaskPrio      = 5,
    SppWriterTaskStackSize = 3048,
    SppWriterTaskPrio      = 4,
};

enum BoardsConfigs {
    IICSpeedHz                                             = 150000,
    SDA_Pin                                                = 33,
    SCL_Pin                                                = 32,
    NumberOfPins                                           = 32,
    MinAddress                                             = 1,
    MaxAddress                                             = 127,
    DelayBeforeCheckOfInternalCounterAfterInitializationMs = 100,
    PinConnectionsCheckRetryCount                          = 5,
    DelayBeforeAcknowledgeCheckMs                          = 1,
    DelayAfterPinVoltageSetMs                              = 1,
    DelayBeforeReadAllPinsVoltagesResult                   = 11,
    DelayBeforeRetryCommandSendMs                          = 50,
    DisableOutputRetryTimes                                = 5
};

constexpr float LOW_OUTPUT_VOLTAGE_VALUE     = 0.693f;
constexpr float HIGH_OUTPUT_VOLTAGE_VALUE    = 0.92f;
constexpr float DEFAULT_OUTPUT_VOLTAGE_VALUE = LOW_OUTPUT_VOLTAGE_VALUE;

enum Tasks {
    DefaultTasksCore           = 0,
    VoltageCheckTaskPio        = 7,
    VoltageCheckTaskStackSize  = 4096,
    CommunicatorWritePrio      = 5,
    CommunicatorWriteStackSize = 4096,
    CommunicatorWriteTaskCore  = 0,
    CommunicatorReadPrio       = 6,
    CommunicatorReadTaskSize   = 8192,
    CommunicatorReadTaskCore   = 1,
    MainStackSize              = 4096,
    MainPrio                   = 1,
    CommandManagerStackSize    = 4096,
    CommandManagerPrio         = 5,
};

enum class EnableLogForComponent : bool {
    IIC                = false,
    IOBoards           = false,
    Main               = true,
    BluetoothSPP       = false,
    BluettothMain      = false,
    CommandInterpreter = false,
    Socket             = false,
    ResetReason        = true
};
enum Log {
    LogAllErrors = true
};

enum TimeoutMs {
    VoltagesQueueReceive = 1000
};

enum Socket {
    EntryPortNumber = 1500
};

enum FailHandle {
    GetAllVoltagesRetryTimes     = 3,
    CommandToBoardAttemptsNumber = 3,
};

uint8_t const static high_voltage_reference_select_pin = 20;
uint8_t const static low_voltage_reference_select_pin  = 21;

}