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

#include "action_dispatcher.h"

#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>

#include <vector>

namespace caspar::ograf {

namespace {

std::string json_string(const boost::json::object& object, const boost::json::string_view key)
{
    const auto* value = object.if_contains(key);
    if (value == nullptr || !value->is_string()) {
        return {};
    }
    return {value->as_string().data(), value->as_string().size()};
}

} // namespace

action_dispatcher::action_dispatcher(send_handler send)
    : send_(std::move(send))
{
}

action_dispatcher::~action_dispatcher() { cancel_all(); }

boost::json::value action_dispatcher::request(boost::json::object request, const std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    const auto id      = std::to_string(++next_request_id_);
    auto       pending = std::make_shared<pending_request>();
    auto       future  = pending->promise.get_future();
    request["requestId"] = id;

    {
        std::unique_lock lock(mutex_);
        if (!ready_condition_.wait_until(lock, deadline, [this] { return ready_ || closed_; })) {
            throw bridge_timeout("OGraf browser host did not become ready");
        }
        if (closed_) {
            throw bridge_error("OGraf browser bridge closed");
        }
        pending_.emplace(id, pending);
    }

    try {
        send_(boost::json::serialize(request));
    } catch (...) {
        std::lock_guard lock(mutex_);
        pending_.erase(id);
        throw;
    }

    if (future.wait_until(deadline) != std::future_status::ready) {
        std::lock_guard lock(mutex_);
        pending_.erase(id);
        throw bridge_timeout("OGraf browser request " + id + " timed out");
    }

    return future.get();
}

bool action_dispatcher::handle_message(const std::string& message)
{
    boost::system::error_code error;
    const auto                parsed = boost::json::parse(message, error);
    if (error || !parsed.is_object()) {
        return false;
    }

    const auto& response = parsed.as_object();
    if (json_string(response, "type") == "ready") {
        {
            std::lock_guard lock(mutex_);
            if (closed_) {
                return false;
            }
            ready_ = true;
        }
        ready_condition_.notify_all();
        return true;
    }

    const auto  id       = json_string(response, "requestId");
    const auto* ok       = response.if_contains("ok");
    if (id.empty() || ok == nullptr || !ok->is_bool()) {
        return false;
    }

    std::shared_ptr<pending_request> pending;
    {
        std::lock_guard lock(mutex_);
        const auto      found = pending_.find(id);
        if (found == pending_.end()) {
            return false;
        }
        pending = found->second;
        pending_.erase(found);
    }

    if (ok->as_bool()) {
        const auto* value = response.if_contains("value");
        pending->promise.set_value(value != nullptr ? *value : boost::json::value());
    } else {
        std::string message_text = "OGraf browser request failed";
        if (const auto* error_value = response.if_contains("error"); error_value != nullptr && error_value->is_object()) {
            const auto detail = json_string(error_value->as_object(), "message");
            if (!detail.empty()) {
                message_text += ": " + detail;
            }
        }
        pending->promise.set_exception(std::make_exception_ptr(bridge_error(message_text)));
    }
    return true;
}

void action_dispatcher::cancel_all()
{
    std::vector<std::shared_ptr<pending_request>> pending;
    {
        std::lock_guard lock(mutex_);
        closed_ = true;
        for (const auto& [id, request] : pending_) {
            pending.push_back(request);
        }
        pending_.clear();
    }
    ready_condition_.notify_all();

    for (const auto& request : pending) {
        request->promise.set_exception(std::make_exception_ptr(bridge_error("OGraf browser bridge closed")));
    }
}

} // namespace caspar::ograf
