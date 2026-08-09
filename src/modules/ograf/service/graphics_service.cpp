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

#include "graphics_service.h"

#include <modules/ograf/manifest/registry.h>
#include <modules/ograf/runtime/ograf_producer.h>

#include <web/web.h>

#include <common/env.h>
#include <common/utf.h>

#include <core/producer/stage.h>
#include <core/video_channel.h>

#include <boost/json/array.hpp>
#include <boost/log/trivial.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <exception>
#include <set>
#include <utility>

namespace caspar::ograf {

namespace {

bool matches(const graphic_filter& filter, const located_instance& candidate)
{
    return (!filter.target || *filter.target == candidate.target) &&
           (!filter.graphic_id || *filter.graphic_id == candidate.instance.graphic->id) &&
           (!filter.graphic_instance_id || *filter.graphic_instance_id == candidate.instance.id);
}

} // namespace

graphics_service::graphics_service(std::vector<spl::shared_ptr<core::video_channel>> channels,
                                   manifest_registry&                                registry)
    : channels_(std::move(channels))
    , registry_(registry)
{
}

void graphics_service::register_target(const render_target target, const spl::shared_ptr<ograf_producer>& producer)
{
    std::lock_guard lock(mutex_);
    producers_[target] = std::shared_ptr<ograf_producer>(producer);
}

action_result graphics_service::load(const render_target      target,
                                     const std::string&       graphic_id,
                                     boost::json::value       data,
                                     const std::optional<int> cg_layer,
                                     const bool               play_on_load)
{
    auto graphic = registry_.find(graphic_id);
    if (!graphic) {
        throw action_error(404, "Could not find OGraf manifest " + graphic_id);
    }
    return producer(target, true)->load(std::move(graphic), std::move(data), cg_layer, play_on_load);
}

action_result
graphics_service::play(const render_target target, const std::string& instance_id, boost::json::object params)
{
    return require_instance(target, instance_id)->play(instance_id, std::move(params));
}

action_result
graphics_service::update(const render_target target, const std::string& instance_id, boost::json::object params)
{
    return require_instance(target, instance_id)->update(instance_id, std::move(params));
}

action_result
graphics_service::stop(const render_target target, const std::string& instance_id, boost::json::object params)
{
    return require_instance(target, instance_id)->stop(instance_id, std::move(params));
}

action_result
graphics_service::invoke_custom(const render_target target, const std::string& instance_id, boost::json::object params)
{
    return require_instance(target, instance_id)->invoke_custom(instance_id, std::move(params));
}

action_result graphics_service::dispose(const render_target target, const std::string& instance_id)
{
    return require_instance(target, instance_id)->dispose(instance_id);
}

std::vector<located_instance> graphics_service::clear(const std::vector<graphic_filter>& filters)
{
    auto                          candidates = all_instances();
    std::vector<located_instance> cleared;
    for (const auto& candidate : candidates) {
        const bool selected = filters.empty() || std::ranges::any_of(filters, [&](const auto& filter) {
                                  return matches(filter, candidate);
                              });
        if (!selected) {
            continue;
        }
        try {
            dispose(candidate.target, candidate.instance.id);
        } catch (const std::exception& error) {
            // graphic_runtime removes a failed instance before rethrowing.
            // A batch clear must therefore continue with the remaining
            // selected instances instead of leaving the target half-cleared.
            CASPAR_LOG(warning)
                << L"[ograf] GraphicInstance " << u16(candidate.instance.id)
                << L" reported an error while being cleared; continuing the batch: "
                << u16(error.what());
        } catch (...) {
            CASPAR_LOG(warning)
                << L"[ograf] GraphicInstance " << u16(candidate.instance.id)
                << L" reported an unknown error while being cleared; continuing the batch";
        }
        cleared.push_back(candidate);
    }
    return cleared;
}

std::vector<render_target> graphics_service::targets()
{
    std::lock_guard            lock(mutex_);
    std::vector<render_target> result;
    for (auto entry = producers_.begin(); entry != producers_.end();) {
        if (entry->second.expired()) {
            entry = producers_.erase(entry);
        } else {
            result.push_back(entry->first);
            ++entry;
        }
    }
    return result;
}

std::vector<graphic_instance> graphics_service::instances(const render_target target)
{
    const auto current = producer(target, false);
    return current ? current->instances() : std::vector<graphic_instance>();
}

std::vector<located_instance> graphics_service::all_instances()
{
    std::vector<located_instance> result;
    for (const auto target : targets()) {
        for (auto& instance : instances(target)) {
            result.push_back({target, std::move(instance)});
        }
    }
    return result;
}

boost::json::object graphics_service::render_characteristics(const render_target target) const
{
    const auto         format   = channel(target.channel)->stage()->video_format_desc();
    const auto         internet = env::properties().get(L"configuration.ograf.access-to-public-internet", false);
    boost::json::array engines;
    engines.emplace_back(boost::json::object{
        {"type", "CEF"},
        {"version", web::browser_engine_version()},
    });
    return {
        {"resolution", {{"width", format.square_width}, {"height", format.square_height}}},
        {"frameRate", format.fps},
        {"accessToPublicInternet", internet},
        {"engine", std::move(engines)},
    };
}

bool graphics_service::has_target(const render_target target) const
{
    return target.layer >= 0 && target.channel > 0 && static_cast<std::size_t>(target.channel) <= channels_.size();
}

spl::shared_ptr<core::video_channel> graphics_service::channel(const int index) const
{
    if (index <= 0 || static_cast<std::size_t>(index) > channels_.size()) {
        throw action_error(404, "Unknown CasparCG channel " + std::to_string(index));
    }
    return channels_[static_cast<std::size_t>(index - 1)];
}

std::shared_ptr<ograf_producer> graphics_service::producer(const render_target target, const bool create)
{
    if (target.layer < 0) {
        throw action_error(404, "Invalid CasparCG layer " + std::to_string(target.layer));
    }

    const auto target_channel = channel(target.channel);
    auto       current_base   = target_channel->stage()->foreground(target.layer).get();
    auto       current        = std::dynamic_pointer_cast<ograf_producer>(current_base);
    if (current) {
        std::lock_guard lock(mutex_);
        producers_[target] = current;
        return current;
    }
    if (!create) {
        return {};
    }

    std::lock_guard creation_lock(producer_creation_mutex_);

    current_base = target_channel->stage()->foreground(target.layer).get();
    current      = std::dynamic_pointer_cast<ograf_producer>(current_base);
    if (current) {
        std::lock_guard lock(mutex_);
        producers_[target] = current;
        return current;
    }

    auto created_spl =
        create_ograf_producer(target_channel->frame_factory(), target_channel->stage()->video_format_desc());
    current = std::shared_ptr<ograf_producer>(created_spl);
    target_channel->stage()->load(target.layer, created_spl).get();
    target_channel->stage()->play(target.layer).get();
    {
        std::lock_guard lock(mutex_);
        producers_[target] = current;
    }
    return current;
}

std::shared_ptr<ograf_producer> graphics_service::require_instance(const render_target target,
                                                                   const std::string&  instance_id)
{
    auto current = producer(target, false);
    if (!current || !current->find(instance_id)) {
        throw action_error(404,
                           "No OGraf GraphicInstance " + instance_id + " on channel " + std::to_string(target.channel) +
                               " layer " + std::to_string(target.layer));
    }
    return current;
}

} // namespace caspar::ograf
