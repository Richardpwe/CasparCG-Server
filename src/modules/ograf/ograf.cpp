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

#include <common/env.h>
#include <common/utf.h>

#include <boost/log/trivial.hpp>

#include <memory>

namespace caspar::ograf {

namespace {

std::unique_ptr<manifest_registry> registry_;
std::unique_ptr<graphics_service>  graphics_;

} // namespace

void init(const core::module_dependencies& dependencies)
{
    registry_ = std::make_unique<manifest_registry>(std::filesystem::path(env::template_folder()));
    registry_->refresh();
    graphics_ = std::make_unique<graphics_service>(dependencies.channels, *registry_);

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
    graphics_.reset();
    registry_.reset();
}

manifest_registry& registry() { return *registry_; }
graphics_service&  graphics() { return *graphics_; }

} // namespace caspar::ograf
