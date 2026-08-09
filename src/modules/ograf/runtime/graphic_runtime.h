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

#include <modules/ograf/manifest/manifest.h>

#include <boost/json/object.hpp>
#include <boost/json/value.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace caspar::ograf {

struct action_result
{
    int                         status_code = 200;
    std::string                 status_message;
    boost::json::value          result;
    std::optional<std::int64_t> current_step;
    std::string                 graphic_instance_id;

    boost::json::object to_json() const;
};

class action_error : public std::runtime_error
{
  public:
    action_error(int status_code, std::string status_message);

    int status_code() const noexcept;

  private:
    int status_code_;
};

struct graphic_instance
{
    std::string                     id;
    std::shared_ptr<const manifest> graphic;
    std::optional<int>              cg_layer;
    std::optional<std::int64_t>     current_step;
};

class graphic_runtime
{
  public:
    using request_handler =
        std::function<boost::json::value(boost::json::object, std::chrono::milliseconds)>;

    graphic_runtime(request_handler          request,
                    std::chrono::milliseconds action_timeout,
                    std::chrono::milliseconds dispose_timeout);

    action_result load(std::shared_ptr<const manifest> graphic,
                       std::string                     module_url,
                       boost::json::value              data,
                       boost::json::object             render_characteristics,
                       std::optional<int>              cg_layer,
                       bool                            play_on_load);

    action_result play(const std::string& graphic_instance_id, boost::json::object params);
    action_result update(const std::string& graphic_instance_id, boost::json::object params);
    action_result stop(const std::string& graphic_instance_id, boost::json::object params);
    action_result invoke_custom(const std::string& graphic_instance_id, boost::json::object params);
    action_result dispose(const std::string& graphic_instance_id);

    std::optional<graphic_instance>              find(const std::string& graphic_instance_id) const;
    std::optional<graphic_instance>              find_by_cg_layer(int cg_layer) const;
    std::vector<graphic_instance>                list() const;

  private:
    action_result invoke(const std::string& graphic_instance_id,
                         const std::string& operation,
                         boost::json::object params);
    action_result request_action(const std::string&        graphic_instance_id,
                                 const std::string&        operation,
                                 boost::json::object       params,
                                 std::chrono::milliseconds timeout);
    void          discard(const std::string& graphic_instance_id) noexcept;
    void          erase(const std::string& graphic_instance_id);
    void          validate_data(const manifest& graphic, const boost::json::value& data) const;
    void          validate_update_data(const manifest& graphic, const boost::json::value& data) const;
    void          validate_custom_action(const manifest& graphic, const boost::json::object& params) const;

    request_handler request_;
    std::chrono::milliseconds action_timeout_;
    std::chrono::milliseconds dispose_timeout_;

    mutable std::mutex                                mutex_;
    std::unordered_map<std::string, graphic_instance> instances_;
    std::set<int>                                     loading_layers_;
};

} // namespace caspar::ograf
