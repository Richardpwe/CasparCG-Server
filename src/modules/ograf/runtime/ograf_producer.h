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

#include "graphic_runtime.h"

#include <common/memory.h>

#include <core/producer/frame_producer.h>

#include <boost/json/object.hpp>
#include <boost/json/value.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace caspar::web {
class producer;
}

namespace caspar::ograf {

class action_dispatcher;

class ograf_producer final : public core::frame_producer
{
  public:
    ograf_producer(const spl::shared_ptr<core::frame_factory>& frame_factory,
                   const core::video_format_desc&              format_desc);
    ~ograf_producer() override;

    action_result load(std::shared_ptr<const manifest> graphic,
                       boost::json::value              data,
                       std::optional<int>              cg_layer,
                       bool                            play_on_load);
    action_result play(const std::string& graphic_instance_id, boost::json::object params);
    action_result update(const std::string& graphic_instance_id, boost::json::object params);
    action_result stop(const std::string& graphic_instance_id, boost::json::object params);
    action_result invoke_custom(const std::string& graphic_instance_id, boost::json::object params);
    action_result dispose(const std::string& graphic_instance_id);

    std::optional<graphic_instance> find(const std::string& graphic_instance_id) const;
    std::optional<graphic_instance> find_by_cg_layer(int cg_layer) const;
    std::vector<graphic_instance>   instances() const;

    core::draw_frame receive_impl(core::video_field field, int nb_samples) override;
    core::draw_frame first_frame(core::video_field field) override;
    core::draw_frame last_frame(core::video_field field) override;
    std::future<std::wstring> call(const std::vector<std::wstring>& params) override;
    core::monitor::state state() const override;
    std::wstring         print() const override;
    std::wstring         name() const override;
    bool                 is_ready() override;

  private:
    core::video_format_desc                  format_desc_;
    renderer_capabilities                   capabilities_;
    std::shared_ptr<action_dispatcher>       dispatcher_;
    std::shared_ptr<web::producer>           web_;
    std::unique_ptr<graphic_runtime>         runtime_;
};

spl::shared_ptr<ograf_producer> create_ograf_producer(
    const spl::shared_ptr<core::frame_factory>& frame_factory,
    const core::video_format_desc&              format_desc);

} // namespace caspar::ograf
