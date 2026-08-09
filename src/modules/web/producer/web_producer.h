/*
 * Copyright 2013 Sveriges Television AB http://casparcg.com/
 *
 * This file is part of CasparCG (www.casparcg.com).
 *
 * CasparCG is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <common/memory.h>

#include <core/fwd.h>

#include <string>

namespace caspar::web {

spl::shared_ptr<core::frame_producer> create_producer(const spl::shared_ptr<core::frame_factory>& frame_factory,
                                                      const core::video_format_desc&              format_desc,
                                                      std::wstring                                url,
                                                      std::wstring                                name);

} // namespace caspar::web
