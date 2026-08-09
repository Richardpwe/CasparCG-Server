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

#include "registry.h"

#include <boost/algorithm/string/predicate.hpp>

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace caspar::ograf {

namespace {

std::string path_key(const std::filesystem::path& path)
{
    auto key = path.generic_string();
#ifdef _WIN32
    std::ranges::transform(key, key.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
#endif
    return key;
}

bool is_manifest_path(const std::filesystem::path& path)
{
    return boost::algorithm::iends_with(path.filename().string(), ".ograf.json");
}

} // namespace

manifest_registry::manifest_registry(std::filesystem::path root)
    : root_(std::move(root))
{
}

void manifest_registry::refresh()
{
    std::lock_guard lock(mutex_);

    std::unordered_map<std::string, std::shared_ptr<const manifest>> manifests_by_path;
    std::map<std::string, std::vector<std::shared_ptr<const manifest>>> manifests_by_id;
    std::vector<registry_error>                                         scan_errors;
    std::vector<std::filesystem::path>                                  paths;

    std::error_code iterator_error;
    std::filesystem::recursive_directory_iterator iterator(
        root_, std::filesystem::directory_options::skip_permission_denied, iterator_error);
    const std::filesystem::recursive_directory_iterator end;

    while (!iterator_error && iterator != end) {
        std::error_code type_error;
        if (iterator->is_regular_file(type_error) && !type_error && is_manifest_path(iterator->path())) {
            paths.push_back(iterator->path());
        }
        iterator.increment(iterator_error);
    }
    if (iterator_error) {
        scan_errors.push_back({root_, "Could not scan OGraf manifests: " + iterator_error.message()});
    }

    std::ranges::sort(paths);
    for (const auto& path : paths) {
        try {
            auto parsed = std::make_shared<const manifest>(load_manifest(path));
            manifests_by_id[parsed->id].push_back(parsed);
        } catch (const std::exception& error) {
            scan_errors.push_back({path, error.what()});
        }
    }

    std::unordered_map<std::string, std::shared_ptr<const manifest>> manifests_by_unique_id;
    for (const auto& [id, entries] : manifests_by_id) {
        if (entries.size() != 1) {
            for (const auto& entry : entries) {
                scan_errors.push_back({entry->manifest_path, "Duplicate OGraf manifest id: " + id});
            }
            continue;
        }

        const auto& entry = entries.front();
        std::error_code relative_error;
        const auto      relative_path = std::filesystem::relative(entry->manifest_path, root_, relative_error);
        if (relative_error) {
            scan_errors.push_back(
                {entry->manifest_path, "Could not make OGraf manifest path relative: " + relative_error.message()});
            continue;
        }

        manifests_by_unique_id.emplace(id, entry);
        manifests_by_path.emplace(path_key(relative_path), entry);
    }

    by_id_  = std::move(manifests_by_unique_id);
    by_path_ = std::move(manifests_by_path);
    errors_  = std::move(scan_errors);
}

std::vector<std::shared_ptr<const manifest>> manifest_registry::list()
{
    refresh();

    std::lock_guard lock(mutex_);
    std::vector<std::shared_ptr<const manifest>> result;
    result.reserve(by_id_.size());
    for (const auto& [id, entry] : by_id_) {
        result.push_back(entry);
    }
    std::ranges::sort(result, [](const auto& left, const auto& right) { return left->id < right->id; });
    return result;
}

std::shared_ptr<const manifest> manifest_registry::find(const std::string& id_or_path)
{
    refresh();

    std::lock_guard lock(mutex_);
    if (const auto by_id = by_id_.find(id_or_path); by_id != by_id_.end()) {
        return by_id->second;
    }

    auto key = path_key(std::filesystem::path(id_or_path).lexically_normal());
    if (const auto by_path = by_path_.find(key); by_path != by_path_.end()) {
        return by_path->second;
    }

    if (!boost::algorithm::iends_with(key, ".ograf.json")) {
        key += ".ograf.json";
        if (const auto by_path = by_path_.find(key); by_path != by_path_.end()) {
            return by_path->second;
        }
    }

    return {};
}

std::vector<registry_error> manifest_registry::errors() const
{
    std::lock_guard lock(mutex_);
    return errors_;
}

const std::filesystem::path& manifest_registry::root() const { return root_; }

template_kind resolve_template(manifest_registry& registry, const std::string& name)
{
    if (boost::algorithm::iends_with(name, ".ograf.json")) {
        return registry.find(name) != nullptr ? template_kind::ograf : template_kind::none;
    }
    if (boost::algorithm::iends_with(name, ".html")) {
        std::error_code error;
        return std::filesystem::is_regular_file(registry.root() / name, error) && !error ? template_kind::html
                                                                                        : template_kind::none;
    }

    std::error_code error;
    if (std::filesystem::is_regular_file(registry.root() / (name + ".html"), error) && !error) {
        return template_kind::html;
    }
    return registry.find(name) != nullptr ? template_kind::ograf : template_kind::none;
}

} // namespace caspar::ograf
