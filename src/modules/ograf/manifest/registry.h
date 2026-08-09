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

#include "manifest.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace caspar::ograf {

struct registry_error
{
    std::filesystem::path path;
    std::string           message;
};

enum class template_kind
{
    none,
    html,
    ograf
};

class manifest_registry
{
  public:
    explicit manifest_registry(std::filesystem::path root);

    void refresh();

    std::vector<std::shared_ptr<const manifest>> list();
    std::shared_ptr<const manifest>               find(const std::string& id_or_path);
    std::vector<registry_error>                   errors() const;

    const std::filesystem::path& root() const;

  private:
    std::filesystem::path root_;
    mutable std::mutex    mutex_;

    std::unordered_map<std::string, std::shared_ptr<const manifest>> by_id_;
    std::unordered_map<std::string, std::shared_ptr<const manifest>> by_path_;
    std::vector<registry_error>                                      errors_;
};

template_kind resolve_template(manifest_registry& registry, const std::string& name);

} // namespace caspar::ograf
