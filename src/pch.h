#pragma once

#define NOMINMAX

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <spdlog/sinks/basic_file_sink.h>

#include <atomic>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace logs = SKSE::log;
using namespace std::literals;
