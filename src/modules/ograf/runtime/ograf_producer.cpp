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

#include "ograf_producer.h"

#include "action_dispatcher.h"

#include <web/producer/web_producer.h>
#include <web/web.h>

#include "ograf_host.h"

#include <common/env.h>
#include <common/utf.h>

#include <boost/json/array.hpp>
#include <boost/log/trivial.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string_view>
#include <thread>
#include <utility>

namespace caspar::ograf {

namespace {

bool is_unreserved_url_character(const unsigned char character)
{
    return std::isalnum(character) != 0 || character == '-' || character == '.' || character == '_' ||
           character == '~' || character == '/' || character == ':';
}

std::string file_url(const std::filesystem::path& path)
{
    auto input = u8(path.wstring());
    std::ranges::replace(input, '\\', '/');

    static constexpr auto hex = "0123456789ABCDEF";
    std::string           encoded;
    encoded.reserve(input.size() + 16);
    for (const auto character : input) {
        const auto byte = static_cast<unsigned char>(character);
        if (is_unreserved_url_character(byte)) {
            encoded.push_back(character);
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[byte >> 4]);
            encoded.push_back(hex[byte & 0x0f]);
        }
    }

#ifdef _WIN32
    return "file:///" + encoded;
#else
    return "file://" + encoded;
#endif
}

std::string content_hash(const std::string_view content)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto character : content) {
        hash ^= static_cast<unsigned char>(character);
        hash *= 1099511628211ULL;
    }

    static constexpr auto digits = "0123456789abcdef";
    std::string           result(16, '0');
    for (auto index = result.size(); index > 0; --index) {
        result[index - 1] = digits[hash & 0x0f];
        hash >>= 4;
    }
    return result;
}

bool file_has_content(const std::filesystem::path& path, const std::string_view expected)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    const std::string actual(std::istreambuf_iterator<char>(input), {});
    return actual == expected;
}

std::wstring embedded_host_url()
{
    static const auto url = [] {
        const std::string_view content(
            reinterpret_cast<const char*>(host::document), sizeof(host::document) - 1);
        const auto directory = std::filesystem::path(env::data_folder()) / L"ograf-runtime";
        const auto target    = directory / u16("host-" + content_hash(content) + ".html");

        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) {
            throw bridge_error("Could not create OGraf runtime directory: " + error.message());
        }

        if (!std::filesystem::exists(target, error)) {
            if (error) {
                throw bridge_error("Could not inspect OGraf host resource: " + error.message());
            }

            auto temporary = target;
            temporary += u16(".tmp-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));

            {
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                output.write(content.data(), static_cast<std::streamsize>(content.size()));
                output.close();
                if (!output) {
                    std::filesystem::remove(temporary, error);
                    throw bridge_error("Could not write embedded OGraf host resource");
                }
            }

            std::filesystem::rename(temporary, target, error);
            if (error) {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                if (!file_has_content(target, content)) {
                    throw bridge_error("Could not publish embedded OGraf host resource: " + error.message());
                }
            }
        } else if (!file_has_content(target, content)) {
            throw bridge_error("Embedded OGraf host resource is corrupt");
        }

        return u16(file_url(target));
    }();
    return url;
}

std::chrono::milliseconds configured_timeout(const std::wstring& key, const int fallback)
{
    return std::chrono::milliseconds(std::max(1, env::properties().get<int>(key, fallback)));
}

boost::json::object render_characteristics(const core::video_format_desc& format_desc,
                                           const bool                     access_to_public_internet)
{
    boost::json::array engines;
    engines.emplace_back(boost::json::object{
        {"type", "CEF"},
        {"version", web::browser_engine_version()},
    });

    return {
        {"resolution", {{"width", format_desc.square_width}, {"height", format_desc.square_height}}},
        {"frameRate", format_desc.fps},
        {"accessToPublicInternet", access_to_public_internet},
        {"engine", std::move(engines)},
    };
}

} // namespace

ograf_producer::ograf_producer(const spl::shared_ptr<core::frame_factory>& frame_factory,
                               const core::video_format_desc&              format_desc)
    : format_desc_(format_desc)
{
    const auto access_to_public_internet =
        env::properties().get(L"configuration.ograf.access-to-public-internet", false);
    capabilities_ = {
        static_cast<double>(format_desc.square_width),
        static_cast<double>(format_desc.square_height),
        format_desc.fps,
        access_to_public_internet,
        {{"CEF", web::browser_engine_version()}},
    };

    dispatcher_ = std::make_shared<action_dispatcher>([this](const std::string& request) {
        if (!web_) {
            throw bridge_error("OGraf browser bridge is closed");
        }
        web_->execute_javascript(L"window.__casparOgraphDispatch(" + u16(request) + L");");
    });

    std::weak_ptr<action_dispatcher> weak_dispatcher(dispatcher_);
    auto rendered = web::create_producer(
        frame_factory,
        format_desc,
        embedded_host_url(),
        L"ograf",
        [weak_dispatcher](const std::string& message) {
            if (const auto dispatcher = weak_dispatcher.lock()) {
                dispatcher->handle_message(message);
            }
        },
        access_to_public_internet);
    web_ = std::shared_ptr<web::producer>(rendered);

    const auto action_timeout =
        configured_timeout(L"configuration.ograf.action-timeout-ms", 30000);
    const auto dispose_timeout =
        configured_timeout(L"configuration.ograf.dispose-timeout-ms", 5000);
    runtime_ = std::make_unique<graphic_runtime>(
        [dispatcher = dispatcher_](boost::json::object request, const std::chrono::milliseconds timeout) {
            return dispatcher->request(std::move(request), timeout);
        },
        action_timeout,
        dispose_timeout);
}

ograf_producer::~ograf_producer()
{
    if (runtime_) {
        for (const auto& instance : runtime_->list()) {
            try {
                runtime_->dispose(instance.id);
            } catch (const std::exception& error) {
                CASPAR_LOG(warning) << L"[ograf] Failed to dispose GraphicInstance " << u16(instance.id)
                                    << L" while closing render target: " << u16(error.what());
            }
        }
        runtime_.reset();
    }
    web_.reset();
    if (dispatcher_) {
        dispatcher_->cancel_all();
    }
}

action_result ograf_producer::load(std::shared_ptr<const manifest> graphic,
                                   boost::json::value              data,
                                   const std::optional<int>        cg_layer,
                                   const bool                      play_on_load)
{
    if (!graphic) {
        throw action_error(404, "OGraf manifest was not found");
    }
    if (!graphic->supports(capabilities_)) {
        throw action_error(422, "OGraf renderRequirements cannot be satisfied by this render target");
    }

    const auto access_to_public_internet = capabilities_.access_to_public_internet;
    return runtime_->load(
        graphic,
        file_url(graphic->main_path),
        std::move(data),
        render_characteristics(format_desc_, access_to_public_internet),
        cg_layer,
        play_on_load);
}

action_result ograf_producer::play(const std::string& graphic_instance_id, boost::json::object params)
{
    return runtime_->play(graphic_instance_id, std::move(params));
}

action_result ograf_producer::update(const std::string& graphic_instance_id, boost::json::object params)
{
    return runtime_->update(graphic_instance_id, std::move(params));
}

action_result ograf_producer::stop(const std::string& graphic_instance_id, boost::json::object params)
{
    return runtime_->stop(graphic_instance_id, std::move(params));
}

action_result ograf_producer::invoke_custom(const std::string& graphic_instance_id, boost::json::object params)
{
    return runtime_->invoke_custom(graphic_instance_id, std::move(params));
}

action_result ograf_producer::dispose(const std::string& graphic_instance_id)
{
    return runtime_->dispose(graphic_instance_id);
}

std::optional<graphic_instance> ograf_producer::find(const std::string& graphic_instance_id) const
{
    return runtime_->find(graphic_instance_id);
}

std::optional<graphic_instance> ograf_producer::find_by_cg_layer(const int cg_layer) const
{
    return runtime_->find_by_cg_layer(cg_layer);
}

std::vector<graphic_instance> ograf_producer::instances() const { return runtime_->list(); }

core::draw_frame ograf_producer::receive_impl(const core::video_field field, const int nb_samples)
{
    return web_->receive(field, nb_samples);
}

core::draw_frame ograf_producer::first_frame(const core::video_field field) { return web_->first_frame(field); }

core::draw_frame ograf_producer::last_frame(const core::video_field field) { return web_->last_frame(field); }

std::future<std::wstring> ograf_producer::call(const std::vector<std::wstring>& params)
{
    return web_->call(params);
}

core::monitor::state ograf_producer::state() const { return web_->state(); }

std::wstring ograf_producer::print() const { return L"ograf"; }

std::wstring ograf_producer::name() const { return L"ograf"; }

bool ograf_producer::is_ready() { return web_->is_ready(); }

spl::shared_ptr<ograf_producer> create_ograf_producer(
    const spl::shared_ptr<core::frame_factory>& frame_factory,
    const core::video_format_desc&              format_desc)
{
    return spl::make_shared<ograf_producer>(frame_factory, format_desc);
}

} // namespace caspar::ograf
