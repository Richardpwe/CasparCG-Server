#include <boost/test/unit_test.hpp>

#include <modules/ograf/runtime/action_dispatcher.h>
#include <modules/ograf/runtime/graphic_runtime.h>

#include <boost/json/object.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

using namespace std::chrono_literals;

namespace {

std::shared_ptr<caspar::ograf::manifest> test_manifest()
{
    auto graphic                = std::make_shared<caspar::ograf::manifest>();
    graphic->id                 = "test.graphic";
    graphic->supports_real_time = true;
    graphic->raw = {
        {"schema",
         {
             {"type", "object"},
             {"properties", {{"name", {{"type", "string"}}}}},
             {"required", {"name"}},
             {"additionalProperties", false},
         }},
    };
    graphic->custom_actions.push_back(
        {"accent",
         "Accent",
         {
             {"type", "object"},
             {"properties", {{"color", {{"type", "string"}}}}},
             {"required", {"color"}},
             {"additionalProperties", false},
         }});
    return graphic;
}

boost::json::value successful_response(const boost::json::object& request)
{
    if (request.at("operation") == "playAction") {
        return {{"statusCode", 200}, {"statusMessage", "played"}, {"currentStep", 0}};
    }
    return {{"statusCode", 200}};
}

} // namespace

BOOST_AUTO_TEST_CASE(runtime_loads_and_plays_after_load_resolves)
{
    std::vector<std::string> operations;
    caspar::ograf::graphic_runtime runtime(
        [&](boost::json::object request, std::chrono::milliseconds) {
            operations.emplace_back(request.at("operation").as_string().c_str());
            return successful_response(request);
        },
        30s,
        5s);

    const auto result = runtime.load(
        test_manifest(), "file:///graphic/main.mjs", {{"name", "Ada"}}, {{"frameRate", 50}}, 7, true);

    BOOST_REQUIRE_EQUAL(operations.size(), 2);
    BOOST_TEST(operations[0] == "load");
    BOOST_TEST(operations[1] == "playAction");
    BOOST_TEST(result.current_step.value() == 0);
    BOOST_TEST(!result.graphic_instance_id.empty());

    const auto instance = runtime.find_by_cg_layer(7);
    BOOST_REQUIRE(instance);
    BOOST_TEST(instance->id == result.graphic_instance_id);
    BOOST_TEST(instance->current_step.value() == 0);
}

BOOST_AUTO_TEST_CASE(runtime_keeps_instance_after_action_timeout)
{
    caspar::ograf::graphic_runtime runtime(
        [&](boost::json::object request, std::chrono::milliseconds) -> boost::json::value {
            if (request.at("operation") == "playAction") {
                throw caspar::ograf::bridge_timeout("timed out");
            }
            return successful_response(request);
        },
        30s,
        5s);

    const auto loaded =
        runtime.load(test_manifest(), "file:///graphic/main.mjs", {{"name", "Ada"}}, {}, {}, false);

    BOOST_CHECK_THROW(runtime.play(loaded.graphic_instance_id, {{"delta", 1}}), caspar::ograf::bridge_timeout);
    BOOST_TEST(runtime.find(loaded.graphic_instance_id).has_value());
}

BOOST_AUTO_TEST_CASE(runtime_discards_failed_load)
{
    std::vector<std::string> operations;
    caspar::ograf::graphic_runtime runtime(
        [&](boost::json::object request, std::chrono::milliseconds) -> boost::json::value {
            const std::string operation = request.at("operation").as_string().c_str();
            operations.push_back(operation);
            if (operation == "load") {
                throw caspar::ograf::bridge_timeout("timed out");
            }
            return successful_response(request);
        },
        30s,
        5s);

    BOOST_CHECK_THROW(
        runtime.load(test_manifest(), "file:///graphic/main.mjs", {{"name", "Ada"}}, {}, {}, false),
        caspar::ograf::bridge_timeout);
    BOOST_REQUIRE_EQUAL(operations.size(), 2);
    BOOST_TEST(operations[0] == "load");
    BOOST_TEST(operations[1] == "discard");
    BOOST_TEST(runtime.list().empty());
}

BOOST_AUTO_TEST_CASE(runtime_disposes_previous_instance_on_same_cg_layer)
{
    std::vector<std::string> operations;
    caspar::ograf::graphic_runtime runtime(
        [&](boost::json::object request, std::chrono::milliseconds) {
            operations.emplace_back(request.at("operation").as_string().c_str());
            return successful_response(request);
        },
        30s,
        5s);

    const auto first =
        runtime.load(test_manifest(), "file:///graphic/main.mjs", {{"name", "One"}}, {}, 12, false);
    const auto second =
        runtime.load(test_manifest(), "file:///graphic/main.mjs", {{"name", "Two"}}, {}, 12, false);

    BOOST_TEST(first.graphic_instance_id != second.graphic_instance_id);
    BOOST_REQUIRE_EQUAL(operations.size(), 3);
    BOOST_TEST(operations[0] == "load");
    BOOST_TEST(operations[1] == "dispose");
    BOOST_TEST(operations[2] == "load");
    BOOST_TEST(!runtime.find(first.graphic_instance_id).has_value());
    BOOST_TEST(runtime.find(second.graphic_instance_id).has_value());
}

BOOST_AUTO_TEST_CASE(runtime_validates_data_and_custom_action_payloads)
{
    caspar::ograf::graphic_runtime runtime(
        [](boost::json::object request, std::chrono::milliseconds) { return successful_response(request); },
        30s,
        5s);

    BOOST_CHECK_THROW(
        runtime.load(test_manifest(), "file:///graphic/main.mjs", {{"name", 42}}, {}, {}, false),
        caspar::ograf::action_error);

    const auto loaded =
        runtime.load(test_manifest(), "file:///graphic/main.mjs", {{"name", "Ada"}}, {}, {}, false);
    BOOST_CHECK_NO_THROW(runtime.update(loaded.graphic_instance_id, {{"data", boost::json::object()}}));
    BOOST_CHECK_THROW(
        runtime.invoke_custom(
            loaded.graphic_instance_id, {{"id", "accent"}, {"payload", {{"color", 42}}}}),
        caspar::ograf::action_error);
    BOOST_CHECK_NO_THROW(runtime.invoke_custom(
        loaded.graphic_instance_id, {{"id", "accent"}, {"payload", {{"color", "red"}}}}));
}

BOOST_AUTO_TEST_CASE(runtime_removes_instance_even_when_dispose_fails)
{
    caspar::ograf::graphic_runtime runtime(
        [&](boost::json::object request, std::chrono::milliseconds) -> boost::json::value {
            if (request.at("operation") == "dispose") {
                return {{"statusCode", 500}, {"statusMessage", "dispose failed"}};
            }
            return successful_response(request);
        },
        30s,
        5s);

    const auto loaded =
        runtime.load(test_manifest(), "file:///graphic/main.mjs", {{"name", "Ada"}}, {}, {}, false);
    BOOST_CHECK_THROW(runtime.dispose(loaded.graphic_instance_id), caspar::ograf::action_error);
    BOOST_TEST(!runtime.find(loaded.graphic_instance_id).has_value());
}
