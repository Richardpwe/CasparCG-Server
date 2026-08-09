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

#include <boost/json/value.hpp>

#include <memory>
#include <string>
#include <vector>

namespace caspar::ograf {

inline constexpr auto GRAPHICS_SCHEMA_V1 =
    "https://ograf.ebu.io/v1/specification/json-schemas/graphics/schema.json";

class schema_validator
{
  public:
    schema_validator();

    std::vector<std::string> validate(const boost::json::value& instance, const std::string& schema_id) const;

  private:
    class implementation;
    std::shared_ptr<const implementation> impl_;
};

const schema_validator& v1_schema_validator();

} // namespace caspar::ograf
