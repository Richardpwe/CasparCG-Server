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

#include "html_producer.h"

#include <common/env.h>
#include <common/os/filesystem.h>
#include <common/utf.h>

#include <core/producer/frame_producer.h>
#include <core/video_format.h>

#include <web/producer/web_producer.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/regex.hpp>

#include <optional>

namespace caspar::html {

spl::shared_ptr<core::frame_producer> create_cg_producer(const core::frame_producer_dependencies& dependencies,
                                                         const std::vector<std::wstring>&         params)
{
    const auto html_prefix    = boost::iequals(params.at(0), L"[HTML]");
    const auto param_url      = html_prefix ? params.at(1) : params.at(0);
    const auto filename       = env::template_folder() + param_url + L".html";
    const auto found_filename = find_case_insensitive(filename);
    const auto http_prefix =
        boost::algorithm::istarts_with(param_url, L"http:") || boost::algorithm::istarts_with(param_url, L"https:");

    if (!found_filename && !http_prefix && !html_prefix) {
        return core::frame_producer::empty();
    }

    const auto url = found_filename ? L"file://" + *found_filename : param_url;

    std::optional<int> width;
    std::optional<int> height;
    {
        const auto url_utf8 = u8(url);

        boost::smatch match;
        if (boost::regex_search(url_utf8, match, boost::regex("width=([0-9]+)"))) {
            width = std::stoi(match[1].str());
        }

        if (boost::regex_search(url_utf8, match, boost::regex("height=([0-9]+)"))) {
            height = std::stoi(match[1].str());
        }
    }

    auto format_desc = dependencies.format_desc;
    if (width && height) {
        format_desc.width         = *width;
        format_desc.square_width  = *width;
        format_desc.height        = *height;
        format_desc.square_height = *height;
    }

    return web::create_producer(dependencies.frame_factory, format_desc, url, L"html");
}

spl::shared_ptr<core::frame_producer> create_producer(const core::frame_producer_dependencies& dependencies,
                                                      const std::vector<std::wstring>&         params)
{
    return create_cg_producer(dependencies, params);
}

} // namespace caspar::html
