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

#include <cstdint>
#include <memory>
#include <string>

namespace caspar::protocol::ograf {

class router;

struct http_server_config
{
    std::string   host{"127.0.0.1"};
    std::uint16_t port{8080};
};

class http_server
{
  public:
    http_server(router& request_router, http_server_config config);
    ~http_server();

    http_server(const http_server&)            = delete;
    http_server& operator=(const http_server&) = delete;

    std::uint16_t port() const noexcept;
    bool          is_local() const noexcept;

  private:
    class impl;
    std::unique_ptr<impl> impl_;
};

} // namespace caspar::protocol::ograf
