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
#include <chrono>
#include <map>
#include <set>

namespace caspar::ograf {

namespace {

constexpr std::string_view tombstone_marker = ".casparcg-ograf-tombstone-";

std::string path_key(const std::filesystem::path& path)
{
    auto key = path.generic_string();
#ifdef _WIN32
    std::ranges::transform(
        key, key.begin(), [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
#endif
    return key;
}

bool is_manifest_path(const std::filesystem::path& path)
{
    return boost::algorithm::iends_with(path.filename().string(), ".ograf.json");
}

bool is_tombstone_path(const std::filesystem::path& path)
{
    const auto filename = path.filename().string();
    return boost::algorithm::icontains(filename, ".ograf.json" + std::string(tombstone_marker));
}

std::filesystem::path tombstone_path(const std::filesystem::path& manifest_path)
{
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned int attempt = 0; attempt < 100; ++attempt) {
        auto candidate = manifest_path;
        candidate += std::string(tombstone_marker) + std::to_string(timestamp) + "-" + std::to_string(attempt);
        std::error_code exists_error;
        if (!std::filesystem::exists(candidate, exists_error) && !exists_error) {
            return candidate;
        }
    }
    throw std::runtime_error("Could not allocate a unique OGraf manifest tombstone name");
}

} // namespace

manifest_registry::manifest_registry(std::filesystem::path root)
    : root_(std::move(root))
{
}

void manifest_registry::refresh()
{
    std::lock_guard lock(mutex_);

    std::unordered_map<std::string, std::shared_ptr<const manifest>>    manifests_by_path;
    std::map<std::string, std::vector<std::shared_ptr<const manifest>>> manifests_by_id;
    std::vector<registry_error>                                         scan_errors;
    std::vector<std::filesystem::path>                                  paths;

    std::error_code                               iterator_error;
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

        const auto&     entry = entries.front();
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

    by_id_   = std::move(manifests_by_unique_id);
    by_path_ = std::move(manifests_by_path);
    errors_  = std::move(scan_errors);
}

std::vector<std::shared_ptr<const manifest>> manifest_registry::list()
{
    refresh();

    std::lock_guard                              lock(mutex_);
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

std::optional<manifest_tombstone> manifest_registry::tombstone_manifest(const std::string& graphic_id)
{
    refresh();

    std::lock_guard lock(mutex_);
    if (const auto pending = tombstones_.find(graphic_id); pending != tombstones_.end()) {
        return manifest_tombstone{pending->first, pending->second};
    }
    const auto found = by_id_.find(graphic_id);
    if (found == by_id_.end()) {
        return std::nullopt;
    }

    const auto      source      = found->second->manifest_path;
    const auto      destination = tombstone_path(source);
    std::error_code rename_error;
    std::filesystem::rename(source, destination, rename_error);
    if (rename_error) {
        throw std::runtime_error("Could not tombstone OGraf manifest " + source.string() + ": " +
                                 rename_error.message());
    }

    by_id_.erase(found);
    std::erase_if(by_path_, [&](const auto& entry) { return entry.second->manifest_path == source; });
    tombstones_[graphic_id] = destination;
    return manifest_tombstone{graphic_id, destination};
}

void manifest_registry::remove_unused_tombstones(const std::set<std::string>& live_graphic_ids)
{
    std::lock_guard lock(mutex_);
    for (auto entry = tombstones_.begin(); entry != tombstones_.end();) {
        if (live_graphic_ids.contains(entry->first)) {
            ++entry;
            continue;
        }

        std::error_code remove_error;
        std::filesystem::remove(entry->second, remove_error);
        if (remove_error) {
            throw std::runtime_error("Could not remove OGraf manifest tombstone " + entry->second.string() + ": " +
                                     remove_error.message());
        }
        entry = tombstones_.erase(entry);
    }
}

std::vector<registry_error> manifest_registry::cleanup_orphaned_tombstones()
{
    std::lock_guard                               lock(mutex_);
    std::vector<registry_error>                   result;
    std::error_code                               iterator_error;
    std::filesystem::recursive_directory_iterator iterator(
        root_, std::filesystem::directory_options::skip_permission_denied, iterator_error);
    const std::filesystem::recursive_directory_iterator end;

    while (!iterator_error && iterator != end) {
        std::error_code type_error;
        if (iterator->is_regular_file(type_error) && !type_error && is_tombstone_path(iterator->path())) {
            const auto      path = iterator->path();
            std::error_code remove_error;
            std::filesystem::remove(path, remove_error);
            if (remove_error) {
                result.push_back({path, "Could not remove orphaned OGraf tombstone: " + remove_error.message()});
            }
        }
        iterator.increment(iterator_error);
    }
    if (iterator_error) {
        result.push_back({root_, "Could not scan orphaned OGraf tombstones: " + iterator_error.message()});
    }
    return result;
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
