#include <memory>
#include "application.hpp"
#include <protocol_examples_common.h>

std::shared_ptr<Application> Application::_this = nullptr;

extern "C" void
app_main()
{
    Application::Run();
}
