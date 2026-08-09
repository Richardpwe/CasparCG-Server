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

#include <modules/ograf/manifest/manifest.h>
#include <modules/ograf/runtime/graphic_runtime.h>

#include <common/memory.h>

#include <boost/json/object.hpp>
#include <boost/json/value.hpp>

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace caspar::core {
class video_channel;
}

namespace caspar::ograf {

class manifest_registry;
class ograf_producer;

struct render_target
{
    int channel = 0;
    int layer   = 0;

    auto operator<=>(const render_target&) const = default;
};

struct graphic_filter
{
    std::optional<render_target> target;
    std::optional<std::string>   graphic_id;
    std::optional<std::string>   graphic_instance_id;
};

struct located_instance
{
    render_target    target;
    graphic_instance instance;
};

class graphics_service_interface
{
  public:
    virtual ~graphics_service_interface() = default;

    virtual action_result load(render_target      target,
                               const std::string& graphic_id,
                               boost::json::value data,
                               std::optional<int> cg_layer     = {},
                               bool               play_on_load = false)                                                          = 0;
    virtual action_result play(render_target target, const std::string& instance_id, boost::json::object params)   = 0;
    virtual action_result update(render_target target, const std::string& instance_id, boost::json::object params) = 0;
    virtual action_result stop(render_target target, const std::string& instance_id, boost::json::object params)   = 0;
    virtual action_result
    invoke_custom(render_target target, const std::string& instance_id, boost::json::object params) = 0;
    virtual action_result dispose(render_target target, const std::string& instance_id)             = 0;

    virtual std::vector<located_instance> clear(const std::vector<graphic_filter>& filters) = 0;

    virtual std::vector<render_target>    targets()                                          = 0;
    virtual std::vector<graphic_instance> instances(render_target target)                    = 0;
    virtual std::vector<located_instance> all_instances()                                    = 0;
    virtual boost::json::object           render_characteristics(render_target target) const = 0;
    virtual bool                          has_target(render_target target) const             = 0;
};

class graphics_service final : public graphics_service_interface
{
  public:
    graphics_service(std::vector<spl::shared_ptr<core::video_channel>> channels, manifest_registry& registry);

    void register_target(render_target target, const spl::shared_ptr<ograf_producer>& producer);

    action_result load(render_target      target,
                       const std::string& graphic_id,
                       boost::json::value data,
                       std::optional<int> cg_layer     = {},
                       bool               play_on_load = false) override;
    action_result play(render_target target, const std::string& instance_id, boost::json::object params) override;
    action_result update(render_target target, const std::string& instance_id, boost::json::object params) override;
    action_result stop(render_target target, const std::string& instance_id, boost::json::object params) override;
    action_result
    invoke_custom(render_target target, const std::string& instance_id, boost::json::object params) override;
    action_result dispose(render_target target, const std::string& instance_id) override;

    std::vector<located_instance> clear(const std::vector<graphic_filter>& filters) override;

    std::vector<render_target>    targets() override;
    std::vector<graphic_instance> instances(render_target target) override;
    std::vector<located_instance> all_instances() override;
    boost::json::object           render_characteristics(render_target target) const override;
    bool                          has_target(render_target target) const override;

  private:
    spl::shared_ptr<core::video_channel> channel(int index) const;
    std::shared_ptr<ograf_producer>      producer(render_target target, bool create);
    std::shared_ptr<ograf_producer>      require_instance(render_target target, const std::string& instance_id);

    std::vector<spl::shared_ptr<core::video_channel>> channels_;
    manifest_registry&                                registry_;

    mutable std::mutex                                     producer_creation_mutex_;
    mutable std::mutex                                     mutex_;
    std::map<render_target, std::weak_ptr<ograf_producer>> producers_;
};

} // namespace caspar::ograf
