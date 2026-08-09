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

#include "ograf.h"

#include "manifest/registry.h"
#include "producer/ograf_cg_proxy.h"
#include "runtime/ograf_producer.h"
#include "service/graphics_service.h"

#include <protocol/ograf/http_server.h>
#include <protocol/ograf/router.h>

#include <common/env.h>
#include <common/utf.h>

#include <boost/log/trivial.hpp>
#include <boost/property_tree/ptree.hpp>

#include <memory>
#include <stdexcept>

namespace caspar::ograf {

namespace {

std::unique_ptr<manifest_registry>            registry_;
std::unique_ptr<graphics_service>             graphics_;
std::unique_ptr<protocol::ograf::router>      api_router_;
std::unique_ptr<protocol::ograf::http_server> http_server_;

} // namespace

void init(const core::module_dependencies& dependencies)
{
    registry_ = std::make_unique<manifest_registry>(std::filesystem::path(env::template_folder()));
    registry_->refresh();
    graphics_ = std::make_unique<graphics_service>(dependencies.channels, *registry_);

    if (env::properties().get(L"configuration.ograf.server.enabled", false)) {
        const auto host      = u8(env::properties().get(L"configuration.ograf.server.host", L"127.0.0.1"));
        const auto port      = env::properties().get(L"configuration.ograf.server.port", 8080);
        const auto base_path = u8(env::properties().get(L"configuration.ograf.server.base-path", L"/ograf/v1"));
        if (port < 0 || port > 65535) {
            throw std::invalid_argument("OGraf server port must be between 0 and 65535");
        }

        api_router_  = std::make_unique<protocol::ograf::router>(*registry_, *graphics_, base_path, u8(env::version()));
        http_server_ = std::make_unique<protocol::ograf::http_server>(
            *api_router_, protocol::ograf::http_server_config{host, static_cast<std::uint16_t>(port)});

        CASPAR_LOG(info) << L"[ograf] Server API listening on " << u16(host) << L":"
                         << static_cast<unsigned int>(http_server_->port()) << L" at " << u16(base_path);
        if (!http_server_->is_local()) {
            CASPAR_LOG(warning)
                << L"[ograf] SECURITY WARNING: Server API is bound to a non-loopback address without authentication.";
        }
    }

    for (const auto& error : registry_->errors()) {
        CASPAR_LOG(warning) << L"[ograf] Ignoring manifest " << error.path.wstring() << L": " << u16(error.message);
    }

    dependencies.cg_registry->register_cg_producer(
        L"ograf",
        {L".ograf.json"},
        [](const spl::shared_ptr<core::frame_producer>& producer) {
            return spl::make_shared<ograf_cg_proxy>(producer, registry(), graphics());
        },
        [](const core::frame_producer_dependencies& dependencies, const std::wstring&) {
            return create_ograf_producer(dependencies.frame_factory, dependencies.format_desc);
        },
        true,
        [](const std::wstring& filename) { return registry().find(u8(filename)) != nullptr; });
}

void uninit()
{
    http_server_.reset();
    api_router_.reset();
    graphics_.reset();
    registry_.reset();
}

manifest_registry& registry() { return *registry_; }
graphics_service&  graphics() { return *graphics_; }

} // namespace caspar::ograf
