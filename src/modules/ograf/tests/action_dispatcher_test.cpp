#include <boost/test/unit_test.hpp>

#include <modules/ograf/runtime/action_dispatcher.h>

#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

void mark_ready(caspar::ograf::action_dispatcher& dispatcher)
{
    BOOST_TEST(dispatcher.handle_message(R"({"type":"ready"})"));
}

} // namespace

BOOST_AUTO_TEST_CASE(dispatch_correlates_successful_response)
{
    caspar::ograf::action_dispatcher* dispatcher = nullptr;
    caspar::ograf::action_dispatcher instance([&](const std::string& message) {
        const auto  document = boost::json::parse(message);
        const auto& request  = document.as_object();
        boost::json::object response{
            {"requestId", request.at("requestId")},
            {"ok", true},
            {"value", {{"statusCode", 200}, {"currentStep", 2}}},
        };
        BOOST_TEST(dispatcher->handle_message(boost::json::serialize(response)));
    });
    dispatcher = &instance;
    mark_ready(instance);

    const auto result = instance.request({{"operation", "playAction"}}, 100ms);

    BOOST_TEST(result.as_object().at("statusCode").as_int64() == 200);
    BOOST_TEST(result.as_object().at("currentStep").as_int64() == 2);
}

BOOST_AUTO_TEST_CASE(dispatch_propagates_browser_exception)
{
    caspar::ograf::action_dispatcher* dispatcher = nullptr;
    caspar::ograf::action_dispatcher instance([&](const std::string& message) {
        const auto  document = boost::json::parse(message);
        const auto& request  = document.as_object();
        boost::json::object response{
            {"requestId", request.at("requestId")},
            {"ok", false},
            {"error", {{"message", "animation failed"}}},
        };
        dispatcher->handle_message(boost::json::serialize(response));
    });
    dispatcher = &instance;
    mark_ready(instance);

    BOOST_CHECK_THROW(instance.request({{"operation", "playAction"}}, 100ms), caspar::ograf::bridge_error);
}

BOOST_AUTO_TEST_CASE(dispatch_times_out_and_discards_late_response)
{
    std::string captured;
    caspar::ograf::action_dispatcher dispatcher([&](const std::string& message) { captured = message; });
    mark_ready(dispatcher);

    BOOST_CHECK_THROW(dispatcher.request({{"operation", "updateAction"}}, 1ms), caspar::ograf::bridge_timeout);

    const auto  document = boost::json::parse(captured);
    const auto& request  = document.as_object();
    boost::json::object response{
        {"requestId", request.at("requestId")},
        {"ok", true},
        {"value", {{"statusCode", 200}}},
    };
    BOOST_TEST(!dispatcher.handle_message(boost::json::serialize(response)));
}

BOOST_AUTO_TEST_CASE(dispatch_accepts_parallel_in_flight_requests)
{
    std::vector<std::string> requests;
    std::mutex               mutex;
    std::condition_variable  received;
    caspar::ograf::action_dispatcher dispatcher([&](const std::string& message) {
        {
            std::lock_guard lock(mutex);
            requests.push_back(message);
        }
        received.notify_one();
    });
    mark_ready(dispatcher);

    auto first = std::async(std::launch::async, [&] {
        return dispatcher.request({{"operation", "playAction"}}, 1s);
    });
    auto second = std::async(std::launch::async, [&] {
        return dispatcher.request({{"operation", "updateAction"}}, 1s);
    });

    {
        std::unique_lock lock(mutex);
        BOOST_REQUIRE(received.wait_for(lock, 1s, [&] { return requests.size() == 2; }));
    }

    for (const auto& message : requests) {
        const auto  document = boost::json::parse(message);
        const auto& request  = document.as_object();
        const boost::json::object response{
            {"requestId", request.at("requestId")},
            {"ok", true},
            {"value", {{"statusCode", 200}}},
        };
        dispatcher.handle_message(boost::json::serialize(response));
    }

    BOOST_TEST(first.get().as_object().at("statusCode").as_int64() == 200);
    BOOST_TEST(second.get().as_object().at("statusCode").as_int64() == 200);
}

BOOST_AUTO_TEST_CASE(dispatch_waits_for_explicit_host_readiness)
{
    std::atomic_bool sent = false;
    caspar::ograf::action_dispatcher* dispatcher = nullptr;
    caspar::ograf::action_dispatcher instance([&](const std::string& message) {
        sent = true;
        const auto  document = boost::json::parse(message);
        const auto& request  = document.as_object();
        dispatcher->handle_message(boost::json::serialize(boost::json::object{
            {"requestId", request.at("requestId")},
            {"ok", true},
            {"value", {{"statusCode", 200}}},
        }));
    });
    dispatcher = &instance;

    auto result = std::async(std::launch::async, [&] {
        return instance.request({{"operation", "load"}}, 1s);
    });
    std::this_thread::sleep_for(10ms);
    BOOST_TEST(!sent.load());

    mark_ready(instance);
    BOOST_TEST(result.get().as_object().at("statusCode").as_int64() == 200);
    BOOST_TEST(sent.load());
}

BOOST_AUTO_TEST_CASE(dispatch_uses_request_timeout_while_waiting_for_host)
{
    caspar::ograf::action_dispatcher dispatcher([](const std::string&) {});

    BOOST_CHECK_EXCEPTION(
        dispatcher.request({{"operation", "load"}}, 1ms),
        caspar::ograf::bridge_timeout,
        [](const caspar::ograf::bridge_timeout& error) {
            return std::string(error.what()).find("did not become ready") != std::string::npos;
        });
}

BOOST_AUTO_TEST_CASE(dispatch_cancel_wakes_requests_waiting_for_host)
{
    caspar::ograf::action_dispatcher dispatcher([](const std::string&) {});
    auto result = std::async(std::launch::async, [&] {
        return dispatcher.request({{"operation", "load"}}, 1s);
    });
    std::this_thread::sleep_for(10ms);

    dispatcher.cancel_all();
    BOOST_CHECK_THROW(result.get(), caspar::ograf::bridge_error);
}
