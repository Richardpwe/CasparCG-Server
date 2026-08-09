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

#include "graphic_runtime.h"

#include <modules/ograf/manifest/schema_validator.h>

#include <boost/json/serialize.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace caspar::ograf {

namespace {

std::string json_string(const boost::json::object& object, const boost::json::string_view key)
{
    const auto* value = object.if_contains(key);
    if (value == nullptr || !value->is_string()) {
        return {};
    }
    return {value->as_string().data(), value->as_string().size()};
}

int status_code_from(const boost::json::value& value)
{
    std::int64_t status_code = 0;
    if (value.is_int64()) {
        status_code = value.as_int64();
    } else if (value.is_uint64() &&
               value.as_uint64() <= static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        status_code = static_cast<std::int64_t>(value.as_uint64());
    } else if (value.is_double() && std::isfinite(value.as_double()) &&
               std::floor(value.as_double()) == value.as_double() &&
               value.as_double() >= static_cast<double>(std::numeric_limits<int>::min()) &&
               value.as_double() <= static_cast<double>(std::numeric_limits<int>::max())) {
        status_code = static_cast<std::int64_t>(value.as_double());
    } else {
        throw action_error(500, "OGraf ReturnPayload.statusCode must be an integer");
    }

    if (status_code < 100 || status_code > 599) {
        throw action_error(500, "OGraf ReturnPayload.statusCode must be a valid HTTP status code");
    }
    return static_cast<int>(status_code);
}

std::optional<std::int64_t> current_step_from(const boost::json::object& object)
{
    const auto* value = object.if_contains("currentStep");
    if (value == nullptr || value->is_null()) {
        return {};
    }
    if (value->is_int64()) {
        return value->as_int64();
    }
    if (value->is_uint64() &&
        value->as_uint64() <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return static_cast<std::int64_t>(value->as_uint64());
    }
    if (value->is_double() && std::isfinite(value->as_double()) &&
        std::floor(value->as_double()) == value->as_double() &&
        value->as_double() >= static_cast<double>(std::numeric_limits<std::int64_t>::min()) &&
        value->as_double() <= static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
        return static_cast<std::int64_t>(value->as_double());
    }
    throw action_error(500, "OGraf ReturnPayload.currentStep must be an integer or null");
}

action_result parse_result(const boost::json::value& value, const std::string& graphic_instance_id)
{
    if (!value.is_object()) {
        throw action_error(500, "OGraf method did not return a ReturnPayload object");
    }

    const auto& object      = value.as_object();
    const auto* status_code = object.if_contains("statusCode");
    if (status_code == nullptr) {
        throw action_error(500, "OGraf ReturnPayload.statusCode is missing");
    }

    action_result result;
    result.status_code         = status_code_from(*status_code);
    result.status_message      = json_string(object, "statusMessage");
    result.result              = object.if_contains("result") != nullptr ? object.at("result") : boost::json::value();
    result.current_step        = current_step_from(object);
    result.graphic_instance_id = graphic_instance_id;

    if (result.status_code < 200 || result.status_code >= 300) {
        throw action_error(
            result.status_code,
            result.status_message.empty() ? "OGraf action returned status " + std::to_string(result.status_code)
                                          : result.status_message);
    }
    return result;
}

class layer_reservation
{
  public:
    layer_reservation(std::mutex& mutex, std::set<int>& layers, const std::optional<int> layer)
        : mutex_(mutex)
        , layers_(layers)
        , layer_(layer)
    {
    }

    ~layer_reservation()
    {
        if (layer_) {
            std::lock_guard lock(mutex_);
            layers_.erase(*layer_);
        }
    }

  private:
    std::mutex&        mutex_;
    std::set<int>&     layers_;
    std::optional<int> layer_;
};

void make_partial_schema(boost::json::value& schema)
{
    if (schema.is_array()) {
        for (auto& item : schema.as_array()) {
            make_partial_schema(item);
        }
        return;
    }
    if (!schema.is_object()) {
        return;
    }

    auto& object = schema.as_object();
    object.erase("required");
    for (auto& member : object) {
        make_partial_schema(member.value());
    }
}

} // namespace

boost::json::object action_result::to_json() const
{
    return {
        {"statusCode", status_code},
        {"statusMessage", status_message},
        {"result", result},
        {"currentStep", current_step ? boost::json::value(*current_step) : boost::json::value()},
        {"graphicInstanceId", graphic_instance_id},
    };
}

action_error::action_error(const int status_code, std::string status_message)
    : std::runtime_error(std::move(status_message))
    , status_code_(status_code)
{
}

int action_error::status_code() const noexcept { return status_code_; }

graphic_runtime::graphic_runtime(request_handler                 request,
                                 const std::chrono::milliseconds action_timeout,
                                 const std::chrono::milliseconds dispose_timeout)
    : request_(std::move(request))
    , action_timeout_(action_timeout)
    , dispose_timeout_(dispose_timeout)
{
}

action_result graphic_runtime::load(std::shared_ptr<const manifest> graphic,
                                    std::string                     module_url,
                                    boost::json::value              data,
                                    boost::json::object             render_characteristics,
                                    const std::optional<int>        cg_layer,
                                    const bool                      play_on_load)
{
    if (!graphic) {
        throw action_error(404, "OGraf manifest was not found");
    }
    validate_data(*graphic, data);

    std::optional<std::string> previous_instance;
    {
        std::lock_guard lock(mutex_);
        if (cg_layer && !loading_layers_.insert(*cg_layer).second) {
            throw action_error(409, "Another OGraf GraphicInstance is loading on this CG layer");
        }
        if (cg_layer) {
            for (const auto& [id, instance] : instances_) {
                if (instance.cg_layer == cg_layer) {
                    previous_instance = id;
                    break;
                }
            }
        }
    }
    layer_reservation reservation(mutex_, loading_layers_, cg_layer);

    if (previous_instance) {
        dispose(*previous_instance);
    }

    const auto graphic_instance_id = boost::uuids::to_string(boost::uuids::random_generator()());
    boost::json::object params{
        {"data", std::move(data)},
        {"renderType", "realtime"},
        {"renderCharacteristics", std::move(render_characteristics)},
    };
    boost::json::object request{
        {"operation", "load"},
        {"graphicInstanceId", graphic_instance_id},
        {"moduleUrl", std::move(module_url)},
        {"params", std::move(params)},
    };
    if (cg_layer) {
        request["cgLayer"] = *cg_layer;
    }

    action_result result;
    try {
        result = parse_result(request_(std::move(request), action_timeout_), graphic_instance_id);
    } catch (...) {
        discard(graphic_instance_id);
        throw;
    }

    {
        std::lock_guard lock(mutex_);
        instances_.emplace(
            graphic_instance_id,
            graphic_instance{graphic_instance_id, std::move(graphic), cg_layer, result.current_step});
    }

    if (play_on_load) {
        return play(graphic_instance_id, {{"delta", 1}});
    }
    return result;
}

action_result graphic_runtime::play(const std::string& graphic_instance_id, boost::json::object params)
{
    return invoke(graphic_instance_id, "playAction", std::move(params));
}

action_result graphic_runtime::update(const std::string& graphic_instance_id, boost::json::object params)
{
    const auto instance = find(graphic_instance_id);
    if (!instance) {
        throw action_error(404, "Unknown OGraf GraphicInstance " + graphic_instance_id);
    }
    if (const auto* data = params.if_contains("data"); data != nullptr) {
        validate_update_data(*instance->graphic, *data);
    } else {
        throw action_error(400, "OGraf updateAction requires data");
    }
    return invoke(graphic_instance_id, "updateAction", std::move(params));
}

action_result graphic_runtime::stop(const std::string& graphic_instance_id, boost::json::object params)
{
    return invoke(graphic_instance_id, "stopAction", std::move(params));
}

action_result graphic_runtime::invoke_custom(const std::string& graphic_instance_id, boost::json::object params)
{
    const auto instance = find(graphic_instance_id);
    if (!instance) {
        throw action_error(404, "Unknown OGraf GraphicInstance " + graphic_instance_id);
    }
    validate_custom_action(*instance->graphic, params);
    return invoke(graphic_instance_id, "customAction", std::move(params));
}

action_result graphic_runtime::dispose(const std::string& graphic_instance_id)
{
    if (!find(graphic_instance_id)) {
        throw action_error(404, "Unknown OGraf GraphicInstance " + graphic_instance_id);
    }

    try {
        auto result = request_action(graphic_instance_id, "dispose", {}, dispose_timeout_);
        erase(graphic_instance_id);
        return result;
    } catch (...) {
        erase(graphic_instance_id);
        discard(graphic_instance_id);
        throw;
    }
}

std::optional<graphic_instance> graphic_runtime::find(const std::string& graphic_instance_id) const
{
    std::lock_guard lock(mutex_);
    const auto      found = instances_.find(graphic_instance_id);
    return found != instances_.end() ? std::optional(found->second) : std::nullopt;
}

std::optional<graphic_instance> graphic_runtime::find_by_cg_layer(const int cg_layer) const
{
    std::lock_guard lock(mutex_);
    for (const auto& [id, instance] : instances_) {
        if (instance.cg_layer == cg_layer) {
            return instance;
        }
    }
    return {};
}

std::vector<graphic_instance> graphic_runtime::list() const
{
    std::lock_guard lock(mutex_);
    std::vector<graphic_instance> result;
    result.reserve(instances_.size());
    for (const auto& [id, instance] : instances_) {
        result.push_back(instance);
    }
    return result;
}

action_result graphic_runtime::invoke(const std::string& graphic_instance_id,
                                      const std::string& operation,
                                      boost::json::object params)
{
    if (!find(graphic_instance_id)) {
        throw action_error(404, "Unknown OGraf GraphicInstance " + graphic_instance_id);
    }
    auto result = request_action(graphic_instance_id, operation, std::move(params), action_timeout_);
    {
        std::lock_guard lock(mutex_);
        const auto      found = instances_.find(graphic_instance_id);
        if (found != instances_.end() && operation == "playAction") {
            found->second.current_step = result.current_step;
        }
    }
    return result;
}

action_result graphic_runtime::request_action(const std::string&        graphic_instance_id,
                                              const std::string&        operation,
                                              boost::json::object       params,
                                              const std::chrono::milliseconds timeout)
{
    return parse_result(
        request_(
            {
                {"operation", operation},
                {"graphicInstanceId", graphic_instance_id},
                {"params", std::move(params)},
            },
            timeout),
        graphic_instance_id);
}

void graphic_runtime::discard(const std::string& graphic_instance_id) noexcept
{
    try {
        request_action(graphic_instance_id, "discard", {}, dispose_timeout_);
    } catch (...) {
    }
}

void graphic_runtime::erase(const std::string& graphic_instance_id)
{
    std::lock_guard lock(mutex_);
    instances_.erase(graphic_instance_id);
}

void graphic_runtime::validate_data(const manifest& graphic, const boost::json::value& data) const
{
    const auto* schema = graphic.raw.if_contains("schema");
    if (schema == nullptr) {
        return;
    }
    const auto errors = v1_schema_validator().validate(data, *schema);
    if (!errors.empty()) {
        throw action_error(400, "OGraf data does not match the manifest schema: " + errors.front());
    }
}

void graphic_runtime::validate_update_data(const manifest& graphic, const boost::json::value& data) const
{
    const auto* schema = graphic.raw.if_contains("schema");
    if (schema == nullptr) {
        return;
    }
    auto partial_schema = *schema;
    make_partial_schema(partial_schema);
    const auto errors = v1_schema_validator().validate(data, partial_schema);
    if (!errors.empty()) {
        throw action_error(400, "OGraf update data does not match the manifest schema: " + errors.front());
    }
}

void graphic_runtime::validate_custom_action(const manifest& graphic, const boost::json::object& params) const
{
    const auto id = json_string(params, "id");
    if (id.empty()) {
        throw action_error(400, "OGraf customAction requires an id");
    }

    const auto action = std::ranges::find_if(graphic.custom_actions, [&](const auto& candidate) {
        return candidate.id == id;
    });
    if (action == graphic.custom_actions.end()) {
        throw action_error(404, "OGraf manifest does not declare custom action " + id);
    }

    if (!action->schema.is_object()) {
        return;
    }
    const auto* payload = params.if_contains("payload");
    const auto  errors  = v1_schema_validator().validate(
        payload != nullptr ? *payload : boost::json::value(), action->schema);
    if (!errors.empty()) {
        throw action_error(400, "OGraf custom action payload does not match its schema: " + errors.front());
    }
}

} // namespace caspar::ograf
