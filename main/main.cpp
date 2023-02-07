#include "nvs_flash.h"

#include "task.hpp"
#include "bluetooth.hpp"
#include "protocol_examples_common.h"

#include "main_apparatus.hpp"
#include "communicator.hpp"

#include "message.hpp"
#include "esp_wifi.h"

#include "application.hpp"

std::shared_ptr<Application> Application::_this = nullptr;

extern "C" void
app_main()
{
    Application::Run();
}
