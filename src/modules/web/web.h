/*
 * Copyright (c) 2026 CasparCG contributors
 *
 * This file is part of CasparCG (www.casparcg.com).
 *
 * CasparCG is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <core/module_dependencies.h>

#include <functional>
#include <future>
#include <string>
#include <utility>

namespace caspar::web {

inline const std::string REMOVE_MESSAGE_NAME = "CasparCGRemove";
inline const std::string LOG_MESSAGE_NAME    = "CasparCGLog";
inline const std::string WEB_MESSAGE_NAME    = "CasparCGWebMessage";

bool intercept_command_line(int argc, char** argv);
void init(const core::module_dependencies& dependencies);
void uninit();

void              invoke(const std::function<void()>& func);
std::future<void> begin_invoke(const std::function<void()>& func);

std::pair<bool, bool> is_gpu_shared_texture_enabled();
std::string           browser_engine_version();

} // namespace caspar::web
