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

#include "request_policy.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>

namespace caspar::web {

namespace {

std::string lower(std::string_view value)
{
    std::string result(value);
    std::ranges::transform(result, result.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

std::optional<std::string> url_host(const std::string_view url, const std::size_t scheme_end)
{
    if (url.substr(scheme_end + 1, 2) != "//") {
        return std::nullopt;
    }

    const auto authority_start = scheme_end + 3;
    const auto authority_end   = url.find_first_of("/?#", authority_start);
    auto       authority       = url.substr(authority_start,
                                authority_end == std::string_view::npos ? url.size() - authority_start
                                                                                    : authority_end - authority_start);
    if (const auto credentials = authority.rfind('@'); credentials != std::string_view::npos) {
        authority.remove_prefix(credentials + 1);
    }

    if (authority.starts_with('[')) {
        const auto bracket = authority.find(']');
        if (bracket == std::string_view::npos) {
            return std::nullopt;
        }
        return lower(authority.substr(1, bracket - 1));
    }

    if (const auto port = authority.rfind(':'); port != std::string_view::npos) {
        authority = authority.substr(0, port);
    }
    return lower(authority);
}

bool is_ipv4_loopback(const std::string_view host)
{
    std::array<int, 4> octets{};
    std::size_t        start = 0;
    for (std::size_t index = 0; index < octets.size(); ++index) {
        const auto end  = host.find('.', start);
        const auto part = host.substr(start, end == std::string_view::npos ? host.size() - start : end - start);
        if (part.empty() || part.size() > 3 ||
            !std::ranges::all_of(part, [](const unsigned char value) { return std::isdigit(value) != 0; })) {
            return false;
        }
        octets[index] = std::stoi(std::string(part));
        if (octets[index] > 255 || (index < 3 && end == std::string_view::npos) ||
            (index == 3 && end != std::string_view::npos)) {
            return false;
        }
        start = end + 1;
    }
    return octets[0] == 127;
}

bool is_loopback_host(const std::string_view host)
{
    return host == "localhost" || host.ends_with(".localhost") || host == "::1" || host == "0:0:0:0:0:0:0:1" ||
           is_ipv4_loopback(host);
}

} // namespace

bool is_request_url_allowed(const std::string_view url, const bool access_to_public_internet)
{
    if (access_to_public_internet) {
        return true;
    }

    const auto scheme_end = url.find(':');
    if (scheme_end == std::string_view::npos) {
        return true;
    }

    const auto scheme = lower(url.substr(0, scheme_end));
    if (scheme == "file" || scheme == "data" || scheme == "blob" || scheme == "about" || scheme == "devtools" ||
        scheme == "chrome" || scheme == "chrome-extension") {
        return true;
    }

    if (scheme == "http" || scheme == "https" || scheme == "ws" || scheme == "wss") {
        const auto host = url_host(url, scheme_end);
        return host && is_loopback_host(*host);
    }

    return false;
}

} // namespace caspar::web
