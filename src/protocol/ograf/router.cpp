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

#include "router.h"

#include <modules/ograf/manifest/registry.h>
#include <modules/ograf/runtime/action_dispatcher.h>
#include <modules/ograf/runtime/action_parameters.h>
#include <modules/ograf/service/graphics_service.h>

#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace caspar::protocol::ograf {

namespace {

using caspar::ograf::action_error;
using caspar::ograf::graphic_filter;
using caspar::ograf::manifest;
using caspar::ograf::render_target;

class api_error : public std::runtime_error
{
  public:
    api_error(const unsigned int status, std::string message)
        : std::runtime_error(std::move(message))
        , status_(status)
    {
    }

    unsigned int status() const noexcept { return status_; }

  private:
    unsigned int status_;
};

std::string normalize_base_path(std::string path)
{
    if (path.empty() || path.front() != '/') {
        path.insert(path.begin(), '/');
    }
    while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
    }
    return path;
}

int hex_value(const char character)
{
    if (character >= '0' && character <= '9')
        return character - '0';
    if (character >= 'a' && character <= 'f')
        return character - 'a' + 10;
    if (character >= 'A' && character <= 'F')
        return character - 'A' + 10;
    return -1;
}

std::string url_decode(const std::string_view input, const bool plus_is_space = false)
{
    std::string result;
    result.reserve(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
        if (input[index] == '%' && index + 2 < input.size()) {
            const auto high = hex_value(input[index + 1]);
            const auto low  = hex_value(input[index + 2]);
            if (high < 0 || low < 0) {
                throw api_error(400, "Invalid URL encoding");
            }
            result.push_back(static_cast<char>((high << 4) | low));
            index += 2;
        } else if (plus_is_space && input[index] == '+') {
            result.push_back(' ');
        } else {
            result.push_back(input[index]);
        }
    }
    return result;
}

struct parsed_target
{
    std::string                                      path;
    std::vector<std::pair<std::string, std::string>> query;
};

parsed_target parse_request_target(const std::string& target)
{
    parsed_target result;
    const auto    query_start = target.find('?');
    result.path               = url_decode(std::string_view(target).substr(0, query_start));
    if (query_start == std::string::npos) {
        return result;
    }

    const auto  query = std::string_view(target).substr(query_start + 1);
    std::size_t start = 0;
    while (start <= query.size()) {
        const auto end       = query.find('&', start);
        const auto parameter = query.substr(start, end == std::string_view::npos ? query.size() - start : end - start);
        const auto equals    = parameter.find('=');
        result.query.emplace_back(url_decode(parameter.substr(0, equals), true),
                                  equals == std::string_view::npos ? std::string()
                                                                   : url_decode(parameter.substr(equals + 1), true));
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return result;
}

std::optional<std::string> query_value(const parsed_target& target, const std::string& name)
{
    const auto found = std::ranges::find_if(target.query, [&](const auto& entry) { return entry.first == name; });
    return found != target.query.end() ? std::optional(found->second) : std::nullopt;
}

bool query_boolean(const parsed_target& target, const std::string& name, const bool fallback)
{
    const auto value = query_value(target, name);
    if (!value) {
        return fallback;
    }
    if (*value == "true") {
        return true;
    }
    if (*value == "false") {
        return false;
    }
    throw api_error(400, name + " query parameter must be true or false");
}

boost::json::value parse_json(const std::string& input, const std::string& description)
{
    boost::system::error_code error;
    auto                      result = boost::json::parse(input, error);
    if (error) {
        throw api_error(400, "Invalid " + description + " JSON: " + error.message());
    }
    return result;
}

const boost::json::object& require_object(const boost::json::value& value, const std::string& description)
{
    if (!value.is_object()) {
        throw api_error(400, description + " must be a JSON object");
    }
    return value.as_object();
}

const boost::json::value& require_member(const boost::json::object& object, const boost::json::string_view name)
{
    const auto* result = object.if_contains(name);
    if (result == nullptr) {
        throw api_error(400, "Required property " + std::string(name) + " is missing");
    }
    return *result;
}

std::string require_string(const boost::json::object& object, const boost::json::string_view name)
{
    const auto& value = require_member(object, name);
    if (!value.is_string()) {
        throw api_error(400, "Property " + std::string(name) + " must be a string");
    }
    return {value.as_string().data(), value.as_string().size()};
}

int require_integer(const boost::json::object& object, const boost::json::string_view name)
{
    const auto& value = require_member(object, name);
    if (value.is_int64() && value.as_int64() >= std::numeric_limits<int>::min() &&
        value.as_int64() <= std::numeric_limits<int>::max()) {
        return static_cast<int>(value.as_int64());
    }
    if (value.is_uint64() && value.as_uint64() <= static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        return static_cast<int>(value.as_uint64());
    }
    throw api_error(400, "Property " + std::string(name) + " must be an integer");
}

render_target parse_render_target(const boost::json::value& value)
{
    const auto& object = require_object(value, "renderTarget");
    return {require_integer(object, "channel"), require_integer(object, "layer")};
}

boost::json::object render_target_json(const render_target target)
{
    return {{"channel", target.channel}, {"layer", target.layer}};
}

boost::json::object graphic_list_info(const manifest& graphic)
{
    return {
        {"id", graphic.id},
        {"name", graphic.name},
        {"description", graphic.description},
    };
}

std::string iso_file_time(const std::filesystem::path& path)
{
    std::error_code error;
    const auto      file_time = std::filesystem::last_write_time(path, error);
    if (error) {
        return {};
    }
    const auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        file_time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    const auto seconds = std::chrono::system_clock::to_time_t(system_time);
    std::tm    utc{};
#ifdef _WIN32
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    std::ostringstream result;
    result << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return result.str();
}

boost::json::object instance_json(const caspar::ograf::graphic_instance& instance)
{
    return {
        {"graphicInstanceId", instance.id},
        {"graphic", graphic_list_info(*instance.graphic)},
    };
}

boost::json::object target_info(caspar::ograf::graphics_service_interface& service, const render_target target)
{
    boost::json::array instances;
    for (const auto& instance : service.instances(target)) {
        instances.emplace_back(instance_json(instance));
    }
    return {
        {"renderTarget", render_target_json(target)},
        {"name", "Channel " + std::to_string(target.channel) + " layer " + std::to_string(target.layer)},
        {"description", "CasparCG stage layer"},
        {"graphicInstances", std::move(instances)},
    };
}

boost::json::object renderer_info(caspar::ograf::graphics_service_interface& service)
{
    boost::json::array targets;
    for (const auto target : service.targets()) {
        targets.emplace_back(target_info(service, target));
    }

    boost::json::object result{
        {"id", "casparcg"},
        {"name", "CasparCG"},
        {"description", "CasparCG native OGraf v1 real-time renderer"},
        {"customActions", boost::json::array()},
        {"renderTargetSchema",
         {
             {"type", "object"},
             {"properties",
              {
                  {"channel", {{"type", "integer"}}},
                  {"layer", {{"type", "integer"}}},
              }},
             {"required", {"channel", "layer"}},
             {"additionalProperties", false},
         }},
        {"status", {{"status", "OK"}, {"message", "Renderer is running"}}},
        {"renderTargets", std::move(targets)},
    };
    if (service.has_target({1, 0})) {
        result["renderCharacteristics"] = service.render_characteristics({1, 0});
    }
    return result;
}

api_response json_response(const boost::json::value& body, const unsigned int status = 200)
{
    return {status, "application/json", boost::json::serialize(body)};
}

std::string status_title(const unsigned int status)
{
    switch (status) {
        case 400:
            return "Bad Request";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 409:
            return "Conflict";
        case 413:
            return "Payload Too Large";
        case 422:
            return "Unprocessable Content";
        case 500:
            return "Internal Server Error";
        case 550:
            return "Graphic Execution Failed";
        default:
            return "Request Failed";
    }
}

void require_renderer(const std::string& renderer_id)
{
    if (renderer_id != "casparcg") {
        throw api_error(404, "Unknown OGraf renderer " + renderer_id);
    }
}

struct action_request
{
    render_target       target;
    std::string         instance_id;
    boost::json::object params;
};

action_request parse_action_request(const std::string& body)
{
    const auto  document = parse_json(body, "request body");
    const auto& object   = require_object(document, "Request body");
    return {
        parse_render_target(require_member(object, "renderTarget")),
        require_string(object, "graphicInstanceId"),
        require_object(require_member(object, "params"), "params"),
    };
}

std::vector<graphic_filter> parse_filters(const boost::json::value& value)
{
    if (!value.is_array()) {
        throw api_error(400, "filters must be an array");
    }
    std::vector<graphic_filter> result;
    for (const auto& item : value.as_array()) {
        const auto&    object = require_object(item, "GraphicFilter");
        graphic_filter filter;
        if (const auto* target = object.if_contains("renderTarget"); target != nullptr) {
            filter.target = parse_render_target(*target);
        }
        if (const auto* graphic = object.if_contains("graphicId"); graphic != nullptr) {
            if (!graphic->is_string())
                throw api_error(400, "GraphicFilter.graphicId must be a string");
            filter.graphic_id = std::string(graphic->as_string());
        }
        if (const auto* instance = object.if_contains("graphicInstanceId"); instance != nullptr) {
            if (!instance->is_string())
                throw api_error(400, "GraphicFilter.graphicInstanceId must be a string");
            filter.graphic_instance_id = std::string(instance->as_string());
        }
        result.push_back(std::move(filter));
    }
    return result;
}

} // namespace

api_response make_problem_response(const unsigned int status,
                                   const std::string& detail,
                                   const std::string& instance)
{
    return {
        status,
        "application/problem+json",
        boost::json::serialize(boost::json::object{
            {"type", "about:blank"},
            {"title", status_title(status)},
            {"status", status},
            {"detail", detail},
            {"instance", instance},
        }),
    };
}

router::router(caspar::ograf::manifest_registry&          registry,
               caspar::ograf::graphics_service_interface& service,
               std::string                                base_path,
               std::string                                server_version)
    : registry_(registry)
    , service_(service)
    , base_path_(normalize_base_path(std::move(base_path)))
    , server_version_(std::move(server_version))
{
}

api_response router::route(const api_request& request) const noexcept
{
    try {
        const auto target = parse_request_target(request.target);
        if (target.path != base_path_ && !target.path.starts_with(base_path_ + "/")) {
            throw api_error(404, "Path is outside the OGraf API base path");
        }

        auto path = target.path.substr(base_path_.size());
        if (path.empty())
            path = "/";

        if (request.method == "OPTIONS") {
            return {204, "application/json", ""};
        }

        if (path == "/" && request.method == "GET") {
            return json_response(boost::json::object{
                {"name", "CasparCG"},
                {"description", "CasparCG Server with native OGraf v1 real-time rendering"},
                {"author", {{"name", "CasparCG contributors"}, {"url", "https://casparcg.com/"}}},
                {"version", server_version_},
            });
        }

        if (path == "/graphics" && request.method == "GET") {
            boost::json::array graphics;
            for (const auto& graphic : registry_.list()) {
                graphics.emplace_back(graphic_list_info(*graphic));
            }
            return json_response(boost::json::object{{"graphics", std::move(graphics)}});
        }

        constexpr std::string_view graphics_prefix = "/graphics/";
        if (path.starts_with(graphics_prefix)) {
            const auto graphic_id = path.substr(graphics_prefix.size());
            if (graphic_id.empty() || graphic_id.find('/') != std::string::npos) {
                throw api_error(404, "Unknown OGraf graphic");
            }
            if (request.method == "DELETE") {
                const auto force     = query_boolean(target, "force", false);
                const auto tombstone = registry_.tombstone_manifest(graphic_id);
                if (!tombstone) {
                    throw api_error(404, "Unknown OGraf graphic " + graphic_id);
                }

                if (force) {
                    service_.clear({graphic_filter{std::nullopt, graphic_id, std::nullopt}});
                }

                std::set<std::string> live_graphic_ids;
                for (const auto& instance : service_.all_instances()) {
                    live_graphic_ids.insert(instance.instance.graphic->id);
                }
                registry_.remove_unused_tombstones(live_graphic_ids);
                return json_response(boost::json::object{});
            }
            if (request.method != "GET") {
                throw api_error(405, "Method is not supported for this graphics resource");
            }
            const auto graphic = registry_.find(graphic_id);
            if (!graphic) {
                throw api_error(404, "Unknown OGraf graphic " + graphic_id);
            }
            const auto          modified = iso_file_time(graphic->manifest_path);
            boost::json::object metadata{{"createdAt", modified}};
            if (!modified.empty()) {
                metadata["updatedAt"] = modified;
            }
            return json_response(boost::json::object{{"graphic", graphic->raw}, {"metadata", std::move(metadata)}});
        }

        if (path == "/renderers" && request.method == "GET") {
            return json_response(boost::json::object{
                {"renderers",
                 boost::json::array{
                     boost::json::object{
                         {"id", "casparcg"},
                         {"name", "CasparCG"},
                         {"description", "CasparCG native OGraf v1 real-time renderer"},
                     },
                 }},
            });
        }

        constexpr std::string_view renderer_prefix = "/renderers/";
        if (!path.starts_with(renderer_prefix)) {
            throw api_error(404, "Unknown OGraf API endpoint");
        }
        const auto renderer_path = path.substr(renderer_prefix.size());
        const auto slash         = renderer_path.find('/');
        const auto renderer_id   = renderer_path.substr(0, slash);
        const auto remainder     = slash == std::string::npos ? std::string() : renderer_path.substr(slash);
        require_renderer(renderer_id);

        if (remainder.empty() && request.method == "GET") {
            return json_response(boost::json::object{{"renderer", renderer_info(service_)}});
        }

        if (remainder == "/target" && request.method == "GET") {
            const auto serialized_target = query_value(target, "renderTarget");
            if (!serialized_target) {
                throw api_error(400, "renderTarget query parameter is required");
            }
            const auto target_value = parse_json(*serialized_target, "renderTarget");
            const auto parsed       = parse_render_target(target_value);
            if (!service_.has_target(parsed)) {
                throw api_error(404, "Unknown RenderTarget");
            }
            return json_response(target_info(service_, parsed));
        }

        if (remainder == "/target/graphicInstance/load" && request.method == "POST") {
            const auto  document      = parse_json(request.body, "request body");
            const auto& object        = require_object(document, "Request body");
            const auto  parsed_target = parse_render_target(require_member(object, "renderTarget"));
            const auto  graphic_id    = require_string(object, "graphicId");
            const auto& params        = require_object(require_member(object, "params"), "params");
            const auto  data          = require_member(params, "data");
            return json_response(service_.load(parsed_target, graphic_id, data).to_json());
        }

        if (remainder == "/target/graphicInstance/playAction" && request.method == "POST") {
            auto action   = parse_action_request(request.body);
            action.params = caspar::ograf::play_parameters(boost::json::serialize(action.params));
            return json_response(service_.play(action.target, action.instance_id, std::move(action.params)).to_json());
        }

        if (remainder == "/target/graphicInstance/updateAction" && request.method == "POST") {
            auto action = parse_action_request(request.body);
            if (action.params.if_contains("data") == nullptr) {
                throw api_error(400, "updateAction params.data is required");
            }
            if (const auto* skip = action.params.if_contains("skipAnimation"); skip != nullptr && !skip->is_bool()) {
                throw api_error(400, "skipAnimation must be a boolean");
            }
            return json_response(
                service_.update(action.target, action.instance_id, std::move(action.params)).to_json());
        }

        if (remainder == "/target/graphicInstance/stopAction" && request.method == "POST") {
            auto action   = parse_action_request(request.body);
            action.params = caspar::ograf::stop_parameters(boost::json::serialize(action.params));
            return json_response(service_.stop(action.target, action.instance_id, std::move(action.params)).to_json());
        }

        constexpr std::string_view custom_prefix = "/target/graphicInstance/customActions/";
        if (remainder.starts_with(custom_prefix) && request.method == "POST") {
            const auto custom_id = remainder.substr(custom_prefix.size());
            if (custom_id.empty() || custom_id.find('/') != std::string::npos) {
                throw api_error(404, "Unknown custom action");
            }
            auto action   = parse_action_request(request.body);
            action.params = caspar::ograf::custom_parameters(custom_id, boost::json::serialize(action.params));
            return json_response(
                service_.invoke_custom(action.target, action.instance_id, std::move(action.params)).to_json());
        }

        if (remainder == "/target/graphicInstance/clear" && request.method == "PUT") {
            const auto         document = parse_json(request.body, "request body");
            const auto&        object   = require_object(document, "Request body");
            const auto         filters  = parse_filters(require_member(object, "filters"));
            boost::json::array cleared;
            for (const auto& instance : service_.clear(filters)) {
                cleared.emplace_back(boost::json::object{
                    {"renderTarget", render_target_json(instance.target)},
                    {"graphicInstanceId", instance.instance.id},
                });
            }
            return json_response(boost::json::object{{"graphicInstances", std::move(cleared)}});
        }

        constexpr std::string_view renderer_custom_prefix = "/customActions/";
        if (remainder.starts_with(renderer_custom_prefix)) {
            throw api_error(404, "CasparCG declares no renderer custom actions");
        }

        throw api_error(404, "Unknown OGraf API endpoint");
    } catch (const api_error& error) {
        return make_problem_response(error.status(), error.what(), request.target);
    } catch (const action_error& error) {
        const auto status = error.status_code() >= 400 && error.status_code() < 500
                                ? static_cast<unsigned int>(error.status_code())
                                : 500U;
        return make_problem_response(status, error.what(), request.target);
    } catch (const caspar::ograf::bridge_timeout& error) {
        return make_problem_response(500, error.what(), request.target);
    } catch (const caspar::ograf::bridge_error& error) {
        return make_problem_response(550, error.what(), request.target);
    } catch (const std::exception& error) {
        return make_problem_response(500, error.what(), request.target);
    } catch (...) {
        return make_problem_response(500, "Unknown internal error", request.target);
    }
}

} // namespace caspar::protocol::ograf
