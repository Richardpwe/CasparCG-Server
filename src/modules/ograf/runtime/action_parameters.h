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

#include <boost/json/object.hpp>
#include <boost/json/value.hpp>

#include <string>

namespace caspar::ograf {

boost::json::value  parse_data_parameter(const std::string& input);
boost::json::object play_parameters(const std::string& input);
boost::json::object next_parameters(const std::string& input);
boost::json::object stop_parameters(const std::string& input);
boost::json::object update_parameters(const std::string& data, const std::string& options);
boost::json::object custom_parameters(const std::string& action_id, const std::string& input);

} // namespace caspar::ograf
