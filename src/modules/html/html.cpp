/*
 * Copyright 2013 Sveriges Television AB http://casparcg.com/
 *
 * This file is part of CasparCG (www.casparcg.com).
 *
 * CasparCG is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CasparCG is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CasparCG. If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Robert Nagy, ronag89@gmail.com
 */

#include "html.h"

#include "producer/html_cg_proxy.h"
#include "producer/html_producer.h"

#include <core/producer/cg_proxy.h>

namespace caspar::html {

void init(const core::module_dependencies& dependencies)
{
    dependencies.producer_registry->register_producer_factory(L"HTML Producer", html::create_producer);

    dependencies.cg_registry->register_cg_producer(
        L"html",
        {L".html"},
        [](const spl::shared_ptr<core::frame_producer>& producer) { return spl::make_shared<html_cg_proxy>(producer); },
        [](const core::frame_producer_dependencies& dependencies, const std::wstring& filename) {
            return html::create_cg_producer(dependencies, {filename});
        },
        false);
}

} // namespace caspar::html
