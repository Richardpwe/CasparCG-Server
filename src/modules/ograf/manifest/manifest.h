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

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace caspar::ograf {

struct engine_capability
{
    std::string type;
    std::string version;
};

struct renderer_capabilities
{
    double                         width                     = 0;
    double                         height                    = 0;
    double                         frame_rate                = 0;
    bool                           access_to_public_internet = false;
    std::vector<engine_capability> engines;
};

struct custom_action
{
    std::string id;
    std::string name;
};

struct manifest
{
    std::filesystem::path       manifest_path;
    std::filesystem::path       main_path;
    std::string                 id;
    std::string                 version;
    std::string                 name;
    std::string                 description;
    bool                        supports_real_time     = false;
    bool                        supports_non_real_time = false;
    std::int64_t                step_count             = 1;
    std::vector<custom_action>  custom_actions;
    boost::json::object         raw;

    bool supports(const renderer_capabilities& capabilities) const;
};

class manifest_error : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

manifest load_manifest(const std::filesystem::path& path);

} // namespace caspar::ograf
