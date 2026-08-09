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

#include "schema_validator.h"

#include "ograf_schema_action.h"
#include "ograf_schema_boolean_constraint.h"
#include "ograf_schema_gdd_basic_types.h"
#include "ograf_schema_gdd_object.h"
#include "ograf_schema_gdd_playout_options.h"
#include "ograf_schema_gdd_types.h"
#include "ograf_schema_graphics.h"
#include "ograf_schema_number_constraint.h"
#include "ograf_schema_string_constraint.h"

#include <boost/json/parse.hpp>

#include <cmath>
#include <regex>
#include <stdexcept>
#include <unordered_map>

namespace caspar::ograf {

namespace {

using boost::json::array;
using boost::json::object;
using boost::json::value;

std::string to_string(const boost::json::string& input)
{
    return {input.data(), input.size()};
}

const value* find(const object& input, const boost::json::string_view key)
{
    const auto item = input.find(key);
    return item == input.end() ? nullptr : &item->value();
}

bool is_number(const value& input)
{
    return input.is_int64() || input.is_uint64() || input.is_double();
}

double as_number(const value& input)
{
    if (input.is_int64()) {
        return static_cast<double>(input.as_int64());
    }
    if (input.is_uint64()) {
        return static_cast<double>(input.as_uint64());
    }
    return input.as_double();
}

bool matches_type(const value& input, const boost::json::string_view type)
{
    if (type == "array")
        return input.is_array();
    if (type == "boolean")
        return input.is_bool();
    if (type == "integer")
        return input.is_int64() || input.is_uint64();
    if (type == "null")
        return input.is_null();
    if (type == "number")
        return is_number(input);
    if (type == "object")
        return input.is_object();
    if (type == "string")
        return input.is_string();
    return false;
}

std::string child_path(const std::string& path, const boost::json::string_view child)
{
    return path + "/" + std::string(child.data(), child.size());
}

} // namespace

class schema_validator::implementation
{
    std::unordered_map<std::string, value> schemas_;

    void add_schema(const std::uint8_t* source)
    {
        boost::system::error_code error;
        auto                      schema = boost::json::parse(reinterpret_cast<const char*>(source), error);
        if (error || !schema.is_object()) {
            throw std::runtime_error("Invalid embedded OGraf JSON schema");
        }

        const auto* id = find(schema.as_object(), "$id");
        if (id == nullptr || !id->is_string()) {
            throw std::runtime_error("Embedded OGraf JSON schema has no $id");
        }

        schemas_.emplace(to_string(id->as_string()), std::move(schema));
    }

    bool validate_type(const value& instance,
                       const value& type_schema,
                       const std::string& path,
                       std::vector<std::string>& errors) const
    {
        bool matches = false;
        if (type_schema.is_string()) {
            matches = matches_type(instance, type_schema.as_string());
        } else if (type_schema.is_array()) {
            for (const auto& candidate : type_schema.as_array()) {
                if (candidate.is_string() && matches_type(instance, candidate.as_string())) {
                    matches = true;
                    break;
                }
            }
        }

        if (!matches) {
            errors.push_back(path + ": value does not match the required JSON type");
        }
        return matches;
    }

    bool validate_schema(const value& instance,
                         const value& schema,
                         const std::string& path,
                         std::vector<std::string>& errors,
                         const unsigned int depth) const
    {
        if (depth > 128) {
            errors.push_back(path + ": JSON schema recursion limit exceeded");
            return false;
        }
        if (schema.is_bool()) {
            if (!schema.as_bool()) {
                errors.push_back(path + ": rejected by JSON schema");
            }
            return schema.as_bool();
        }
        if (!schema.is_object()) {
            errors.push_back(path + ": invalid JSON schema node");
            return false;
        }

        const auto& schema_object = schema.as_object();
        bool        valid         = true;

        if (const auto* ref = find(schema_object, "$ref"); ref != nullptr && ref->is_string()) {
            const auto reference = to_string(ref->as_string());
            if (reference.starts_with("https://json-schema.org/")) {
                // The embedded OGraf schemas use the official meta-schema to
                // identify nested GDD schemas. The schema itself is trusted;
                // validation continues with all local sibling constraints.
            } else {
                const auto referenced_schema = schemas_.find(reference);
                if (referenced_schema == schemas_.end()) {
                    errors.push_back(path + ": unresolved JSON schema reference " + reference);
                    valid = false;
                } else {
                    valid =
                        validate_schema(instance, referenced_schema->second, path, errors, depth + 1) && valid;
                }
            }
        }

        if (const auto* all_of = find(schema_object, "allOf"); all_of != nullptr && all_of->is_array()) {
            for (const auto& candidate : all_of->as_array()) {
                valid = validate_schema(instance, candidate, path, errors, depth + 1) && valid;
            }
        }

        if (const auto* one_of = find(schema_object, "oneOf"); one_of != nullptr && one_of->is_array()) {
            std::size_t matches = 0;
            for (const auto& candidate : one_of->as_array()) {
                std::vector<std::string> candidate_errors;
                if (validate_schema(instance, candidate, path, candidate_errors, depth + 1)) {
                    ++matches;
                }
            }
            if (matches != 1) {
                errors.push_back(path + ": value must match exactly one JSON schema alternative");
                valid = false;
            }
        }

        if (const auto* condition = find(schema_object, "if"); condition != nullptr) {
            std::vector<std::string> condition_errors;
            if (validate_schema(instance, *condition, path, condition_errors, depth + 1)) {
                if (const auto* then_schema = find(schema_object, "then"); then_schema != nullptr) {
                    valid = validate_schema(instance, *then_schema, path, errors, depth + 1) && valid;
                }
            }
        }

        if (const auto* type = find(schema_object, "type"); type != nullptr) {
            if (!validate_type(instance, *type, path, errors)) {
                return false;
            }
        }

        if (const auto* constant = find(schema_object, "const"); constant != nullptr && instance != *constant) {
            errors.push_back(path + ": value does not match the required constant");
            valid = false;
        }

        if (const auto* enumeration = find(schema_object, "enum");
            enumeration != nullptr && enumeration->is_array()) {
            bool found = false;
            for (const auto& candidate : enumeration->as_array()) {
                if (instance == candidate) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                errors.push_back(path + ": value is not part of the allowed enumeration");
                valid = false;
            }
        }

        if (is_number(instance)) {
            if (const auto* minimum = find(schema_object, "minimum"); minimum != nullptr && is_number(*minimum) &&
                as_number(instance) < as_number(*minimum)) {
                errors.push_back(path + ": number is below the minimum");
                valid = false;
            }
        }

        if (instance.is_string()) {
            if (const auto* pattern = find(schema_object, "pattern"); pattern != nullptr && pattern->is_string()) {
                const std::regex expression(to_string(pattern->as_string()));
                if (!std::regex_search(to_string(instance.as_string()), expression)) {
                    errors.push_back(path + ": string does not match the required pattern");
                    valid = false;
                }
            }
        }

        if (instance.is_array()) {
            const auto& input_array = instance.as_array();
            if (const auto* min_items = find(schema_object, "minItems"); min_items != nullptr &&
                min_items->is_uint64() && input_array.size() < min_items->as_uint64()) {
                errors.push_back(path + ": array has too few items");
                valid = false;
            }
            if (const auto* items = find(schema_object, "items"); items != nullptr) {
                for (std::size_t index = 0; index < input_array.size(); ++index) {
                    valid = validate_schema(
                                input_array[index], *items, path + "/" + std::to_string(index), errors, depth + 1) &&
                            valid;
                }
            }
        }

        if (!instance.is_object()) {
            return valid;
        }

        const auto& input_object = instance.as_object();
        if (const auto* required = find(schema_object, "required"); required != nullptr && required->is_array()) {
            for (const auto& required_name : required->as_array()) {
                if (required_name.is_string() && find(input_object, required_name.as_string()) == nullptr) {
                    errors.push_back(
                        child_path(path, required_name.as_string()) + ": required property is missing");
                    valid = false;
                }
            }
        }

        const auto* properties = find(schema_object, "properties");
        if (properties != nullptr && properties->is_object()) {
            for (const auto& property : properties->as_object()) {
                if (const auto* property_value = find(input_object, property.key()); property_value != nullptr) {
                    valid = validate_schema(
                                *property_value, property.value(), child_path(path, property.key()), errors, depth + 1) &&
                            valid;
                }
            }
        }

        const auto* pattern_properties = find(schema_object, "patternProperties");
        for (const auto& property : input_object) {
            bool known = properties != nullptr && properties->is_object() &&
                         find(properties->as_object(), property.key()) != nullptr;
            bool matched_pattern = false;

            if (pattern_properties != nullptr && pattern_properties->is_object()) {
                for (const auto& pattern_property : pattern_properties->as_object()) {
                    const std::regex expression(std::string(pattern_property.key()));
                    if (std::regex_search(std::string(property.key()), expression)) {
                        matched_pattern = true;
                        valid          = validate_schema(property.value(),
                                                pattern_property.value(),
                                                child_path(path, property.key()),
                                                errors,
                                                depth + 1) &&
                                valid;
                    }
                }
            }

            if (known || matched_pattern) {
                continue;
            }

            if (const auto* additional = find(schema_object, "additionalProperties"); additional != nullptr) {
                if (additional->is_bool() && !additional->as_bool()) {
                    errors.push_back(child_path(path, property.key()) + ": additional property is not allowed");
                    valid = false;
                } else if (additional->is_object()) {
                    valid = validate_schema(
                                property.value(), *additional, child_path(path, property.key()), errors, depth + 1) &&
                            valid;
                }
            }
        }

        if (const auto* property_names = find(schema_object, "propertyNames"); property_names != nullptr) {
            for (const auto& property : input_object) {
                const value property_name(std::string(property.key()));
                valid = validate_schema(
                            property_name, *property_names, child_path(path, property.key()), errors, depth + 1) &&
                        valid;
            }
        }

        return valid;
    }

  public:
    implementation()
    {
        add_schema(schemas::graphics);
        add_schema(schemas::action);
        add_schema(schemas::boolean_constraint);
        add_schema(schemas::number_constraint);
        add_schema(schemas::string_constraint);
        add_schema(schemas::gdd_basic_types);
        add_schema(schemas::gdd_types);
        add_schema(schemas::gdd_object);
        add_schema(schemas::gdd_playout_options);
    }

    std::vector<std::string> validate(const value& instance, const std::string& schema_id) const
    {
        std::vector<std::string> errors;
        const auto               schema = schemas_.find(schema_id);
        if (schema == schemas_.end()) {
            errors.push_back("$: unknown JSON schema " + schema_id);
            return errors;
        }

        validate_schema(instance, schema->second, "$", errors, 0);
        return errors;
    }

    std::vector<std::string> validate(const value& instance, const value& schema) const
    {
        std::vector<std::string> errors;
        validate_schema(instance, schema, "$", errors, 0);
        return errors;
    }
};

schema_validator::schema_validator()
    : impl_(std::make_shared<const implementation>())
{
}

std::vector<std::string> schema_validator::validate(const boost::json::value& instance,
                                                    const std::string&        schema_id) const
{
    return impl_->validate(instance, schema_id);
}

std::vector<std::string> schema_validator::validate(const boost::json::value& instance,
                                                    const boost::json::value& schema) const
{
    return impl_->validate(instance, schema);
}

const schema_validator& v1_schema_validator()
{
    static const schema_validator instance;
    return instance;
}

} // namespace caspar::ograf
