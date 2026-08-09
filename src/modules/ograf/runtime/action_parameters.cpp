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

#include "action_parameters.h"

#include "graphic_runtime.h"

#include <boost/json/parse.hpp>

#include <cmath>
#include <utility>

namespace caspar::ograf {

namespace {

boost::json::value
parse_json(const std::string& input, boost::json::value empty_value, const std::string& parameter_name)
{
    if (input.empty()) {
        return empty_value;
    }

    boost::system::error_code error;
    auto                      result = boost::json::parse(input, error);
    if (error) {
        throw action_error(400, "Invalid OGraf " + parameter_name + " JSON: " + error.message());
    }
    return result;
}

boost::json::object parse_object(const std::string& input, const std::string& parameter_name)
{
    auto result = parse_json(input, boost::json::object(), parameter_name);
    if (!result.is_object()) {
        throw action_error(400, "OGraf " + parameter_name + " must be a JSON object");
    }
    return std::move(result.as_object());
}

bool is_integer(const boost::json::value& value)
{
    return value.is_int64() || value.is_uint64() ||
           (value.is_double() && std::isfinite(value.as_double()) &&
            std::floor(value.as_double()) == value.as_double());
}

void validate_skip_animation(const boost::json::object& params)
{
    if (const auto* skip = params.if_contains("skipAnimation"); skip != nullptr && !skip->is_bool()) {
        throw action_error(400, "OGraf skipAnimation must be a boolean");
    }
}

} // namespace

boost::json::value parse_data_parameter(const std::string& input)
{
    return parse_json(input, boost::json::object(), "data");
}

boost::json::object play_parameters(const std::string& input)
{
    auto        params = parse_object(input, "play parameters");
    const auto* go_to  = params.if_contains("goto");
    const auto* delta  = params.if_contains("delta");
    if (go_to != nullptr && delta != nullptr) {
        throw action_error(400, "OGraf playAction accepts either goto or delta, not both");
    }
    if (go_to != nullptr && !is_integer(*go_to)) {
        throw action_error(400, "OGraf playAction goto must be an integer");
    }
    if (delta != nullptr && !is_integer(*delta)) {
        throw action_error(400, "OGraf playAction delta must be an integer");
    }
    if (go_to == nullptr && delta == nullptr) {
        params["delta"] = 1;
    }
    validate_skip_animation(params);
    return params;
}

boost::json::object next_parameters(const std::string& input)
{
    auto params = parse_object(input, "next options");
    if (params.if_contains("goto") != nullptr || params.if_contains("delta") != nullptr) {
        throw action_error(400, "OGraf NEXT options must not contain goto or delta");
    }
    validate_skip_animation(params);
    params["delta"] = 1;
    return params;
}

boost::json::object stop_parameters(const std::string& input)
{
    auto params = parse_object(input, "stop options");
    validate_skip_animation(params);
    return params;
}

boost::json::object update_parameters(const std::string& data, const std::string& options)
{
    auto params    = parse_object(options, "update options");
    params["data"] = parse_data_parameter(data);
    validate_skip_animation(params);
    return params;
}

boost::json::object custom_parameters(const std::string& action_id, const std::string& input)
{
    if (action_id.empty()) {
        throw action_error(400, "OGraf custom action id must not be empty");
    }
    auto params  = parse_object(input, "custom action parameters");
    params["id"] = action_id;
    validate_skip_animation(params);
    return params;
}

} // namespace caspar::ograf
