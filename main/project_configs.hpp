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
    DelayBeforeCheckOfInternalCounterAfterInitializationMs = 200,
    CommandSendRetryNumber                                 = 3,
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
    VoltageCheckTaskPio           = 6,
    VoltageCheckTaskStackSize     = 4000,
    MainMeasurementsTaskPrio      = 5,
    MainMeasurementsTaskStackSize = 5000,
    CommunicatorWritePrio         = 5,
    CommunicatorReadPrio          = 5,
    CommunicatorWriteStackSize    = 3000,
    CommunicatorReadTaskSize      = 6000,
    MainStackSize = 4096,
    MainPrio = 1,
};

enum class EnableLogForComponent : bool {
    IIC                = false,
    IOBoards           = false,
    Main               = true,
    BluetoothSPP       = false,
    BluettothMain      = false,
    CommandInterpreter = false,
    Socket             = true,
    ResetReason = true
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

uint8_t const static high_voltage_reference_select_pin = 20;
uint8_t const static low_voltage_reference_select_pin  = 21;



}