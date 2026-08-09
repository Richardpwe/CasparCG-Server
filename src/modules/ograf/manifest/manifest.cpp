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

#include "manifest.h"

#include "schema_validator.h"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/json/parse.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <sstream>

namespace caspar::ograf {

namespace {

using boost::json::object;
using boost::json::value;

std::string json_string(const object& input, const boost::json::string_view key)
{
    const auto* item = input.if_contains(key);
    return item != nullptr && item->is_string()
               ? std::string(item->as_string().data(), item->as_string().size())
               : std::string();
}

double json_number(const value& input)
{
    if (input.is_int64())
        return static_cast<double>(input.as_int64());
    if (input.is_uint64())
        return static_cast<double>(input.as_uint64());
    return input.as_double();
}

bool matches_number_constraint(const double actual, const object& constraint)
{
    if (const auto* exact = constraint.if_contains("exact"); exact != nullptr &&
        std::abs(actual - json_number(*exact)) > 0.000001) {
        return false;
    }
    if (const auto* minimum = constraint.if_contains("min"); minimum != nullptr && actual < json_number(*minimum)) {
        return false;
    }
    if (const auto* maximum = constraint.if_contains("max"); maximum != nullptr && actual > json_number(*maximum)) {
        return false;
    }
    return true;
}

std::vector<std::uint64_t> version_parts(const std::string& version)
{
    std::vector<std::uint64_t> parts;
    std::uint64_t              current  = 0;
    bool                       in_number = false;

    for (const auto character : version) {
        if (character >= '0' && character <= '9') {
            in_number = true;
            current   = current * 10 + static_cast<std::uint64_t>(character - '0');
        } else if (in_number) {
            parts.push_back(current);
            current   = 0;
            in_number = false;
        }
    }
    if (in_number) {
        parts.push_back(current);
    }
    return parts;
}

bool version_at_least(const std::string& actual, const std::string& minimum)
{
    const auto actual_parts  = version_parts(actual);
    const auto minimum_parts = version_parts(minimum);
    if (actual_parts.empty() || minimum_parts.empty()) {
        return actual >= minimum;
    }

    const auto count = std::max(actual_parts.size(), minimum_parts.size());
    for (std::size_t index = 0; index < count; ++index) {
        const auto actual_part  = index < actual_parts.size() ? actual_parts[index] : 0;
        const auto minimum_part = index < minimum_parts.size() ? minimum_parts[index] : 0;
        if (actual_part != minimum_part) {
            return actual_part > minimum_part;
        }
    }
    return true;
}

bool matches_engine_requirement(const boost::json::array& requirements,
                                const std::vector<engine_capability>& engines)
{
    if (requirements.empty()) {
        return false;
    }

    for (const auto& requirement_value : requirements) {
        const auto& requirement = requirement_value.as_object();
        const auto  type        = json_string(requirement, "type");
        const auto& version     = requirement.at("version").as_object();
        const auto  minimum     = json_string(version, "min");

        for (const auto& engine : engines) {
            if (boost::iequals(engine.type, type) && version_at_least(engine.version, minimum)) {
                return true;
            }
        }
    }
    return false;
}

bool matches_render_requirement(const object& requirement, const renderer_capabilities& capabilities)
{
    if (const auto* resolution = requirement.if_contains("resolution"); resolution != nullptr) {
        const auto& constraints = resolution->as_object();
        if (const auto* width = constraints.if_contains("width");
            width != nullptr && !matches_number_constraint(capabilities.width, width->as_object())) {
            return false;
        }
        if (const auto* height = constraints.if_contains("height");
            height != nullptr && !matches_number_constraint(capabilities.height, height->as_object())) {
            return false;
        }
    }

    if (const auto* frame_rate = requirement.if_contains("frameRate");
        frame_rate != nullptr && !matches_number_constraint(capabilities.frame_rate, frame_rate->as_object())) {
        return false;
    }

    if (const auto* internet = requirement.if_contains("accessToPublicInternet"); internet != nullptr) {
        const auto& constraint = internet->as_object();
        if (const auto* exact = constraint.if_contains("exact");
            exact != nullptr && capabilities.access_to_public_internet != exact->as_bool()) {
            return false;
        }
    }

    if (const auto* engines = requirement.if_contains("engine");
        engines != nullptr && !matches_engine_requirement(engines->as_array(), capabilities.engines)) {
        return false;
    }

    return true;
}

bool path_is_inside(const std::filesystem::path& parent, const std::filesystem::path& child)
{
    const auto relative = child.lexically_relative(parent);
    if (relative.empty() || relative.is_absolute()) {
        return false;
    }

    const auto first = relative.begin();
    return first != relative.end() && *first != "..";
}

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw manifest_error("Could not open OGraf manifest: " + path.string());
    }
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

} // namespace

bool manifest::supports(const renderer_capabilities& capabilities) const
{
    const auto* requirements = raw.if_contains("renderRequirements");
    if (requirements == nullptr || requirements->as_array().empty()) {
        return true;
    }

    return std::ranges::any_of(requirements->as_array(), [&](const auto& requirement) {
        return matches_render_requirement(requirement.as_object(), capabilities);
    });
}

manifest load_manifest(const std::filesystem::path& path)
{
    if (!boost::algorithm::iends_with(path.filename().string(), ".ograf.json")) {
        throw manifest_error("OGraf manifest filename must end with .ograf.json");
    }

    boost::system::error_code parse_error;
    auto                      document = boost::json::parse(read_file(path), parse_error);
    if (parse_error) {
        throw manifest_error("Invalid OGraf manifest JSON: " + parse_error.message());
    }

    const auto schema_errors = v1_schema_validator().validate(document, std::string(GRAPHICS_SCHEMA_V1));
    if (!schema_errors.empty()) {
        throw manifest_error("Invalid OGraf v1 manifest: " + schema_errors.front());
    }

    auto& input = document.as_object();
    manifest result;
    result.manifest_path         = path;
    result.id                    = json_string(input, "id");
    result.version               = json_string(input, "version");
    result.name                  = json_string(input, "name");
    result.description           = json_string(input, "description");
    result.supports_real_time     = input.at("supportsRealTime").as_bool();
    result.supports_non_real_time = input.at("supportsNonRealTime").as_bool();

    if (result.id.empty() || result.id.find('/') != std::string::npos) {
        throw manifest_error("OGraf manifest id must be non-empty and must not contain '/'");
    }
    if (!result.supports_real_time) {
        throw manifest_error("OGraf graphic does not support real-time rendering");
    }

    if (const auto* step_count = input.if_contains("stepCount"); step_count != nullptr) {
        if (!step_count->is_int64() && !step_count->is_uint64()) {
            throw manifest_error("OGraf stepCount must be an integer");
        }
        result.step_count =
            step_count->is_int64() ? step_count->as_int64() : static_cast<std::int64_t>(step_count->as_uint64());
    }

    const auto main = std::filesystem::path(json_string(input, "main"));
    if (main.empty() || main.is_absolute()) {
        throw manifest_error("OGraf main must be a relative path");
    }

    std::error_code filesystem_error;
    const auto      graphic_directory = std::filesystem::weakly_canonical(path.parent_path(), filesystem_error);
    if (filesystem_error) {
        throw manifest_error("Could not resolve OGraf graphic directory: " + filesystem_error.message());
    }
    result.main_path = std::filesystem::weakly_canonical(graphic_directory / main, filesystem_error);
    if (filesystem_error || !path_is_inside(graphic_directory, result.main_path)) {
        throw manifest_error("OGraf main resolves outside the graphic directory");
    }
    if (!std::filesystem::is_regular_file(result.main_path, filesystem_error) || filesystem_error) {
        throw manifest_error("OGraf main does not reference a readable file");
    }

    if (const auto* actions = input.if_contains("customActions"); actions != nullptr) {
        for (const auto& action_value : actions->as_array()) {
            const auto& action = action_value.as_object();
            const auto* schema = action.if_contains("schema");
            result.custom_actions.push_back(
                {json_string(action, "id"), json_string(action, "name"), schema != nullptr ? *schema : value()});
        }
    }

    result.raw = std::move(input);
    return result;
}

} // namespace caspar::ograf
