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

#include <string>

namespace caspar::ograf {
class graphics_service_interface;
class manifest_registry;
} // namespace caspar::ograf

namespace caspar::protocol::ograf {

struct api_request
{
    std::string method;
    std::string target;
    std::string body;
};

struct api_response
{
    unsigned int status = 200;
    std::string  content_type{"application/json"};
    std::string  body;
};

api_response make_problem_response(unsigned int       status,
                                   const std::string& detail,
                                   const std::string& instance);

class router
{
  public:
    router(caspar::ograf::manifest_registry&          registry,
           caspar::ograf::graphics_service_interface& service,
           std::string                                base_path,
           std::string                                server_version = "unknown");

    api_response route(const api_request& request) const noexcept;

  private:
    caspar::ograf::manifest_registry&          registry_;
    caspar::ograf::graphics_service_interface& service_;
    std::string                                base_path_;
    std::string                                server_version_;
};

} // namespace caspar::protocol::ograf
