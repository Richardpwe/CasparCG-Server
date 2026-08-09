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

#include "ograf_cg_proxy.h"

#include <modules/ograf/manifest/registry.h>
#include <modules/ograf/runtime/action_parameters.h>
#include <modules/ograf/service/graphics_service.h>

#include <common/utf.h>

#include <boost/json/serialize.hpp>

#include <utility>

namespace caspar::ograf {

namespace {

core::cg_command_result command_result(const action_result& result)
{
    return {201, u16(boost::json::serialize(result.to_json()))};
}

} // namespace

ograf_cg_proxy::ograf_cg_proxy(const spl::shared_ptr<core::frame_producer>& producer,
                               manifest_registry&                           registry,
                               graphics_service&                            service)
    : producer_(spl::dynamic_pointer_cast<ograf_producer>(producer))
    , registry_(registry)
    , service_(service)
{
}

void ograf_cg_proxy::add(const int           layer,
                         const std::wstring& template_name,
                         const bool          play_on_load,
                         const std::wstring& start_from_label,
                         const std::wstring& data)
{
    add_action(layer, template_name, play_on_load, start_from_label, data);
}

void ograf_cg_proxy::remove(const int layer) { remove_action(layer); }

void ograf_cg_proxy::play(const int layer) { play_action(layer, L""); }

void ograf_cg_proxy::stop(const int layer) { stop_action(layer, L""); }

void ograf_cg_proxy::next(const int layer) { next_action(layer, L""); }

void ograf_cg_proxy::update(const int layer, const std::wstring& data) { update_action(layer, data, L""); }

std::wstring ograf_cg_proxy::invoke(const int layer, const std::wstring& label)
{
    return invoke_action(layer, label, L"").payload;
}

bool ograf_cg_proxy::uses_json_data() const { return true; }

void ograf_cg_proxy::bind_render_target(const int channel, const int layer)
{
    service_.register_target({channel, layer}, producer_);
}

core::cg_command_result ograf_cg_proxy::add_action(const int           layer,
                                                   const std::wstring& template_name,
                                                   const bool          play_on_load,
                                                   const std::wstring& start_from_label,
                                                   const std::wstring& data)
{
    if (!start_from_label.empty()) {
        throw action_error(400, "OGraf does not support Flash STARTLABEL");
    }

    auto graphic = registry_.find(u8(template_name));
    if (!graphic) {
        throw action_error(404, "Could not find OGraf manifest " + u8(template_name));
    }

    auto initial_data = parse_data_parameter(u8(data));
    return command_result(producer_->load(std::move(graphic), std::move(initial_data), layer, play_on_load));
}

core::cg_command_result ograf_cg_proxy::play_action(const int layer, const std::wstring& params)
{
    return command_result(producer_->play(instance_id(layer), play_parameters(u8(params))));
}

core::cg_command_result ograf_cg_proxy::stop_action(const int layer, const std::wstring& params)
{
    return command_result(producer_->stop(instance_id(layer), stop_parameters(u8(params))));
}

core::cg_command_result ograf_cg_proxy::next_action(const int layer, const std::wstring& params)
{
    return command_result(producer_->play(instance_id(layer), next_parameters(u8(params))));
}

core::cg_command_result
ograf_cg_proxy::update_action(const int layer, const std::wstring& data, const std::wstring& params)
{
    return command_result(producer_->update(instance_id(layer), update_parameters(u8(data), u8(params))));
}

core::cg_command_result
ograf_cg_proxy::invoke_action(const int layer, const std::wstring& label, const std::wstring& params)
{
    return command_result(producer_->invoke_custom(instance_id(layer), custom_parameters(u8(label), u8(params))));
}

core::cg_command_result ograf_cg_proxy::remove_action(const int layer)
{
    return command_result(producer_->dispose(instance_id(layer)));
}

std::string ograf_cg_proxy::instance_id(const int layer) const
{
    const auto instance = producer_->find_by_cg_layer(layer);
    if (!instance) {
        throw action_error(404, "No OGraf GraphicInstance on CG layer " + std::to_string(layer));
    }
    return instance->id;
}

} // namespace caspar::ograf
