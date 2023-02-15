#include "esp_logger.hpp"
#include <memory>

std::shared_ptr<EspLogger> EspLogger::_this = nullptr;