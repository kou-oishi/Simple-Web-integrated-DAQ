#pragma once

#include <string>
#include <vector>

#include "core/device_frontend.hpp"

const IDeviceFrontend* FindDeviceFrontend(const std::string& frontend_id);
std::vector<std::string> ListDeviceFrontendIds();
