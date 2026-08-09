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

#include <core/producer/cg_proxy.h>

#include <modules/ograf/runtime/ograf_producer.h>

namespace caspar::ograf {

class manifest_registry;

class ograf_cg_proxy final : public core::cg_proxy
{
  public:
    ograf_cg_proxy(const spl::shared_ptr<core::frame_producer>& producer, manifest_registry& registry);

    void         add(int                 layer,
                     const std::wstring& template_name,
                     bool                play_on_load,
                     const std::wstring& start_from_label,
                     const std::wstring& data) override;
    void         remove(int layer) override;
    void         play(int layer) override;
    void         stop(int layer) override;
    void         next(int layer) override;
    void         update(int layer, const std::wstring& data) override;
    std::wstring invoke(int layer, const std::wstring& label) override;

    bool uses_json_data() const override;

    core::cg_command_result add_action(int                 layer,
                                       const std::wstring& template_name,
                                       bool                play_on_load,
                                       const std::wstring& start_from_label,
                                       const std::wstring& data) override;
    core::cg_command_result play_action(int layer, const std::wstring& params) override;
    core::cg_command_result stop_action(int layer, const std::wstring& params) override;
    core::cg_command_result next_action(int layer, const std::wstring& params) override;
    core::cg_command_result update_action(int layer, const std::wstring& data, const std::wstring& params) override;
    core::cg_command_result invoke_action(int layer, const std::wstring& label, const std::wstring& params) override;
    core::cg_command_result remove_action(int layer) override;

  private:
    std::string instance_id(int layer) const;

    spl::shared_ptr<ograf_producer> producer_;
    manifest_registry&              registry_;
};

} // namespace caspar::ograf
