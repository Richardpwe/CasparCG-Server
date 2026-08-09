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

#include <boost/json/object.hpp>
#include <boost/json/value.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace caspar::ograf {

class bridge_error : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

class bridge_timeout : public bridge_error
{
  public:
    using bridge_error::bridge_error;
};

class action_dispatcher
{
  public:
    using send_handler = std::function<void(const std::string&)>;

    explicit action_dispatcher(send_handler send);
    ~action_dispatcher();

    boost::json::value request(boost::json::object request, std::chrono::milliseconds timeout);
    bool               handle_message(const std::string& message);
    void               cancel_all();

  private:
    struct pending_request
    {
        std::promise<boost::json::value> promise;
    };

    send_handler send_;
    std::atomic_uint64_t next_request_id_{0};
    std::mutex mutex_;
    std::condition_variable ready_condition_;
    bool ready_  = false;
    bool closed_ = false;
    std::unordered_map<std::string, std::shared_ptr<pending_request>> pending_;
};

} // namespace caspar::ograf
