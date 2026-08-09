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

#include <common/env.h>
#include <common/utf.h>

#include <boost/log/trivial.hpp>

#include <memory>

namespace caspar::ograf {

namespace {

std::unique_ptr<manifest_registry> registry_;

} // namespace

void init(const core::module_dependencies&)
{
    registry_ = std::make_unique<manifest_registry>(std::filesystem::path(env::template_folder()));
    registry_->refresh();

    for (const auto& error : registry_->errors()) {
        CASPAR_LOG(warning) << L"[ograf] Ignoring manifest " << error.path.wstring() << L": "
                            << u16(error.message);
    }
}

manifest_registry& registry() { return *registry_; }

} // namespace caspar::ograf
