#include <boost/test/unit_test.hpp>

#include <modules/ograf/manifest/registry.h>
#include <modules/ograf/manifest/schema_validator.h>
#include <modules/ograf/service/graphics_service.h>

#include <protocol/ograf/http_server.h>
#include <protocol/ograf/router.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/json/parse.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <mutex>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

class empty_graphics_service final : public caspar::ograf::graphics_service_interface
{
  public:
    bool                                         clear_called = false;
    std::vector<caspar::ograf::located_instance> located;
    std::string                                  last_operation;
    caspar::ograf::render_target                 last_target;
    std::string                                  last_id;
    boost::json::value                           last_data;
    boost::json::object                          last_params;
    std::function<caspar::ograf::action_result()> play_override;

    caspar::ograf::action_result load(caspar::ograf::render_target target,
                                      const std::string&           graphic_id,
                                      boost::json::value           data,
                                      std::optional<int>,
                                      bool) override
    {
        last_operation = "load";
        last_target    = target;
        last_id        = graphic_id;
        last_data      = std::move(data);
        return success("loaded-instance");
    }

    caspar::ograf::action_result
    play(caspar::ograf::render_target target, const std::string& instance_id, boost::json::object params) override
    {
        if (play_override) {
            return play_override();
        }
        return record("play", target, instance_id, std::move(params));
    }

    caspar::ograf::action_result
    update(caspar::ograf::render_target target, const std::string& instance_id, boost::json::object params) override
    {
        return record("update", target, instance_id, std::move(params));
    }

    caspar::ograf::action_result
    stop(caspar::ograf::render_target target, const std::string& instance_id, boost::json::object params) override
    {
        return record("stop", target, instance_id, std::move(params));
    }

    caspar::ograf::action_result invoke_custom(caspar::ograf::render_target target,
                                               const std::string&           instance_id,
                                               boost::json::object          params) override
    {
        return record("custom", target, instance_id, std::move(params));
    }

    caspar::ograf::action_result dispose(caspar::ograf::render_target, const std::string&) override { return {}; }

    std::vector<caspar::ograf::located_instance> clear(const std::vector<caspar::ograf::graphic_filter>&) override
    {
        clear_called = true;
        return std::exchange(located, {});
    }

    std::vector<caspar::ograf::render_target> targets() override { return {}; }

    std::vector<caspar::ograf::graphic_instance> instances(caspar::ograf::render_target) override { return {}; }

    std::vector<caspar::ograf::located_instance> all_instances() override { return located; }

    boost::json::object render_characteristics(caspar::ograf::render_target) const override { return {}; }

    bool has_target(caspar::ograf::render_target) const override { return false; }

  private:
    static caspar::ograf::action_result success(const std::string& instance_id)
    {
        return {200, "OK", boost::json::object{}, std::nullopt, instance_id};
    }

    caspar::ograf::action_result record(std::string                  operation,
                                        caspar::ograf::render_target target,
                                        std::string                  instance_id,
                                        boost::json::object          params)
    {
        last_operation = std::move(operation);
        last_target    = target;
        last_id        = std::move(instance_id);
        last_params    = std::move(params);
        return success(last_id);
    }
};

std::filesystem::path api_test_root()
{
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    auto       root   = std::filesystem::temp_directory_path() / ("casparcg-ograf-api-" + std::to_string(suffix));
    std::filesystem::create_directories(root);
    return root;
}

void add_api_graphic(const std::filesystem::path& root, const std::string& id)
{
    std::ofstream(root / "graphic.mjs") << "export default class Graphic extends HTMLElement {}";
    std::ofstream(root / "graphic.ograf.json")
        << R"({"$schema":")" << caspar::ograf::GRAPHICS_SCHEMA_V1 << R"(",)"
        << R"("id":")" << id << R"(",)"
        << R"("name":"API test","main":"graphic.mjs","supportsRealTime":true,"supportsNonRealTime":false})";
}

bool has_tombstone(const std::filesystem::path& root)
{
    return std::ranges::any_of(std::filesystem::recursive_directory_iterator(root), [](const auto& entry) {
        return entry.path().filename().string().find(".casparcg-ograf-tombstone-") != std::string::npos;
    });
}

std::set<std::pair<std::string, std::string>> openapi_operations()
{
    std::ifstream stream(OGRAF_OPENAPI_SNAPSHOT);
    if (!stream) {
        throw std::runtime_error("Could not open OGraf OpenAPI snapshot");
    }

    const std::regex                              path_pattern(R"(^  (/[^:]*):\s*$)");
    const std::regex                              operation_pattern(R"(^    (get|post|put|delete):\s*$)");
    std::set<std::pair<std::string, std::string>> result;
    std::string                                   current_path;
    std::string                                   line;
    std::smatch                                   match;
    while (std::getline(stream, line)) {
        if (std::regex_match(line, match, path_pattern)) {
            current_path = match[1].str();
        } else if (!current_path.empty() && std::regex_match(line, match, operation_pattern)) {
            auto method = match[1].str();
            std::ranges::transform(method, method.begin(), [](const unsigned char character) {
                return static_cast<char>(std::toupper(character));
            });
            result.emplace(current_path, std::move(method));
        }
    }
    return result;
}

} // namespace

BOOST_AUTO_TEST_CASE(api_router_exposes_server_graphics_and_renderer_info)
{
    const auto                       root = api_test_root();
    caspar::ograf::manifest_registry registry(root);
    empty_graphics_service           service;
    caspar::protocol::ograf::router  router(registry, service, "/ograf/v1/");

    const auto server = router.route({"GET", "/ograf/v1/", ""});
    BOOST_TEST(server.status == 200);
    const auto server_json = boost::json::parse(server.body).as_object();
    BOOST_TEST(server_json.at("name").as_string() == "CasparCG");

    const auto graphics = router.route({"GET", "/ograf/v1/graphics", ""});
    BOOST_TEST(graphics.status == 200);
    BOOST_TEST(boost::json::parse(graphics.body).as_object().at("graphics").as_array().empty());

    const auto renderers = router.route({"GET", "/ograf/v1/renderers", ""});
    BOOST_TEST(renderers.status == 200);
    BOOST_TEST(boost::json::parse(renderers.body)
                   .as_object()
                   .at("renderers")
                   .as_array()
                   .front()
                   .as_object()
                   .at("id")
                   .as_string() == "casparcg");

    std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_CASE(api_router_returns_rfc7807_errors)
{
    const auto                       root = api_test_root();
    caspar::ograf::manifest_registry registry(root);
    empty_graphics_service           service;
    caspar::protocol::ograf::router  router(registry, service, "/ograf/v1");

    const auto missing = router.route({"GET", "/other", ""});
    BOOST_TEST(missing.status == 404);
    BOOST_TEST(missing.content_type == "application/problem+json");
    const auto problem = boost::json::parse(missing.body).as_object();
    BOOST_TEST(problem.at("status").as_int64() == 404);
    BOOST_TEST(problem.at("instance").as_string() == "/other");

    const auto malformed_target = router.route({"GET", "/ograf/v1/renderers/casparcg/target?renderTarget=%7B", ""});
    BOOST_TEST(malformed_target.status == 400);

    const auto options = router.route({"OPTIONS", "/ograf/v1/graphics", ""});
    BOOST_TEST(options.status == 204);

    std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_CASE(api_delete_defers_tombstone_while_graphic_is_in_use)
{
    const auto root = api_test_root();
    add_api_graphic(root, "com.example.delete");

    caspar::ograf::manifest_registry registry(root);
    const auto                       graphic = registry.find("com.example.delete");
    BOOST_REQUIRE(graphic != nullptr);

    empty_graphics_service service;
    service.located.push_back({{1, 20}, {"instance-1", graphic, std::nullopt, std::nullopt}});
    caspar::protocol::ograf::router router(registry, service, "/ograf/v1");

    const auto response = router.route({"DELETE", "/ograf/v1/graphics/com.example.delete", ""});
    BOOST_TEST(response.status == 200);
    BOOST_TEST(!service.clear_called);
    BOOST_TEST(registry.find("com.example.delete") == nullptr);
    BOOST_TEST(has_tombstone(root));

    service.located.clear();
    registry.remove_unused_tombstones({});
    BOOST_TEST(!has_tombstone(root));
    std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_CASE(api_force_delete_clears_instances_before_removing_tombstone)
{
    const auto root = api_test_root();
    add_api_graphic(root, "com.example.force-delete");

    caspar::ograf::manifest_registry registry(root);
    const auto                       graphic = registry.find("com.example.force-delete");
    BOOST_REQUIRE(graphic != nullptr);

    empty_graphics_service service;
    service.located.push_back({{1, 20}, {"instance-1", graphic, std::nullopt, std::nullopt}});
    caspar::protocol::ograf::router router(registry, service, "/ograf/v1");

    const auto response = router.route({"DELETE", "/ograf/v1/graphics/com.example.force-delete?force=true", ""});
    BOOST_TEST(response.status == 200);
    BOOST_TEST(service.clear_called);
    BOOST_TEST(service.located.empty());
    BOOST_TEST(!has_tombstone(root));
    std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_CASE(api_routes_cover_pinned_openapi_snapshot)
{
    const std::set<std::pair<std::string, std::string>> expected{
        {"/", "GET"},
        {"/graphics", "GET"},
        {"/graphics/{graphicId}", "GET"},
        {"/graphics/{graphicId}", "DELETE"},
        {"/renderers", "GET"},
        {"/renderers/{rendererId}", "GET"},
        {"/renderers/{rendererId}/target", "GET"},
        {"/renderers/{rendererId}/customActions/{customActionId}", "POST"},
        {"/renderers/{rendererId}/target/graphicInstance/clear", "PUT"},
        {"/renderers/{rendererId}/target/graphicInstance/load", "POST"},
        {"/renderers/{rendererId}/target/graphicInstance/updateAction", "POST"},
        {"/renderers/{rendererId}/target/graphicInstance/playAction", "POST"},
        {"/renderers/{rendererId}/target/graphicInstance/stopAction", "POST"},
        {"/renderers/{rendererId}/target/graphicInstance/customActions/{customActionId}", "POST"},
    };
    BOOST_TEST(openapi_operations() == expected);
}

BOOST_AUTO_TEST_CASE(api_control_routes_map_openapi_requests_to_graphics_service)
{
    const auto                       root = api_test_root();
    caspar::ograf::manifest_registry registry(root);
    empty_graphics_service           service;
    caspar::protocol::ograf::router  router(registry, service, "/ograf/v1");

    const auto load = router.route(
        {"POST",
         "/ograf/v1/renderers/casparcg/target/graphicInstance/load",
         R"({"renderTarget":{"channel":1,"layer":20},"graphicId":"simple","params":{"data":{"name":"Ada"}}})"});
    BOOST_TEST(load.status == 200);
    BOOST_TEST(service.last_operation == "load");
    BOOST_TEST(service.last_target.channel == 1);
    BOOST_TEST(service.last_target.layer == 20);
    BOOST_TEST(service.last_id == "simple");
    BOOST_TEST(service.last_data.as_object().at("name").as_string() == "Ada");
    BOOST_TEST(boost::json::parse(load.body).as_object().at("graphicInstanceId").as_string() == "loaded-instance");

    const auto play = router.route(
        {"POST",
         "/ograf/v1/renderers/casparcg/target/graphicInstance/playAction",
         R"({"renderTarget":{"channel":1,"layer":20},"graphicInstanceId":"instance-1","params":{"goto":2,"skipAnimation":true}})"});
    BOOST_TEST(play.status == 200);
    BOOST_TEST(service.last_operation == "play");
    BOOST_TEST(service.last_params.at("goto").as_int64() == 2);
    BOOST_TEST(service.last_params.at("skipAnimation").as_bool());

    const auto update = router.route(
        {"POST",
         "/ograf/v1/renderers/casparcg/target/graphicInstance/updateAction",
         R"({"renderTarget":{"channel":1,"layer":20},"graphicInstanceId":"instance-1","params":{"data":{"name":"Grace"},"skipAnimation":true}})"});
    BOOST_TEST(update.status == 200);
    BOOST_TEST(service.last_operation == "update");
    BOOST_TEST(service.last_params.at("data").as_object().at("name").as_string() == "Grace");

    const auto stop = router.route(
        {"POST",
         "/ograf/v1/renderers/casparcg/target/graphicInstance/stopAction",
         R"({"renderTarget":{"channel":1,"layer":20},"graphicInstanceId":"instance-1","params":{"skipAnimation":true}})"});
    BOOST_TEST(stop.status == 200);
    BOOST_TEST(service.last_operation == "stop");
    BOOST_TEST(service.last_params.at("skipAnimation").as_bool());

    const auto custom = router.route(
        {"POST",
         "/ograf/v1/renderers/casparcg/target/graphicInstance/customActions/highlight",
         R"({"renderTarget":{"channel":1,"layer":20},"graphicInstanceId":"instance-1","params":{"payload":{"color":"red"},"skipAnimation":false}})"});
    BOOST_TEST(custom.status == 200);
    BOOST_TEST(service.last_operation == "custom");
    BOOST_TEST(service.last_params.at("id").as_string() == "highlight");
    BOOST_TEST(service.last_params.at("payload").as_object().at("color").as_string() == "red");

    const auto clear =
        router.route({"PUT",
                      "/ograf/v1/renderers/casparcg/target/graphicInstance/clear",
                      R"({"filters":[{"renderTarget":{"channel":1,"layer":20},"graphicInstanceId":"instance-1"}]})"});
    BOOST_TEST(clear.status == 200);
    BOOST_TEST(service.clear_called);

    std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_CASE(api_http_server_serves_router_over_loopback_with_cors)
{
    namespace asio  = boost::asio;
    namespace beast = boost::beast;
    namespace http  = beast::http;
    using tcp       = asio::ip::tcp;

    const auto                           root = api_test_root();
    caspar::ograf::manifest_registry     registry(root);
    empty_graphics_service               service;
    caspar::protocol::ograf::router      router(registry, service, "/ograf/v1", "test-version");
    caspar::protocol::ograf::http_server server(router, caspar::protocol::ograf::http_server_config{"127.0.0.1", 0});

    asio::io_context  context;
    beast::tcp_stream stream(context);
    stream.connect({asio::ip::make_address("127.0.0.1"), server.port()});

    http::request<http::string_body> request{http::verb::get, "/ograf/v1/", 11};
    request.set(http::field::host, "127.0.0.1");
    http::write(stream, request);

    beast::flat_buffer                buffer;
    http::response<http::string_body> response;
    http::read(stream, buffer, response);

    BOOST_TEST(response.result_int() == 200);
    BOOST_TEST(response[http::field::access_control_allow_origin] == "*");
    BOOST_TEST(boost::json::parse(response.body()).as_object().at("version").as_string() == "test-version");

    boost::system::error_code ignored;
    stream.socket().shutdown(tcp::socket::shutdown_both, ignored);
    std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_CASE(api_http_server_rejects_oversized_bodies_with_problem_response)
{
    namespace asio  = boost::asio;
    namespace beast = boost::beast;
    namespace http  = beast::http;
    using tcp       = asio::ip::tcp;

    const auto                           root = api_test_root();
    caspar::ograf::manifest_registry     registry(root);
    empty_graphics_service               service;
    caspar::protocol::ograf::router      router(registry, service, "/ograf/v1", "test-version");
    caspar::protocol::ograf::http_server server(router, caspar::protocol::ograf::http_server_config{"127.0.0.1", 0});

    asio::io_context  context;
    beast::tcp_stream stream(context);
    stream.connect({asio::ip::make_address("127.0.0.1"), server.port()});

    const std::string target = "/ograf/v1/renderers/casparcg/target/graphicInstance/load";
    const std::string request = "POST " + target + " HTTP/1.1\r\n"
                                "Host: 127.0.0.1\r\n"
                                "Content-Type: application/json\r\n"
                                "Content-Length: 4194305\r\n\r\n";
    asio::write(stream.socket(), asio::buffer(request));

    beast::flat_buffer                buffer;
    http::response<http::string_body> response;
    http::read(stream, buffer, response);

    BOOST_TEST(response.result_int() == 413);
    BOOST_TEST(response[http::field::content_type] == "application/problem+json");
    BOOST_TEST(response[http::field::access_control_allow_origin] == "*");
    const auto problem = boost::json::parse(response.body()).as_object();
    BOOST_TEST(problem.at("title").as_string() == "Payload Too Large");
    BOOST_TEST(problem.at("status").as_int64() == 413);
    BOOST_TEST(problem.at("instance").as_string() == target);

    boost::system::error_code ignored;
    stream.socket().shutdown(tcp::socket::shutdown_both, ignored);
    std::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_CASE(api_http_server_serves_independent_actions_concurrently)
{
    namespace asio  = boost::asio;
    namespace beast = boost::beast;
    namespace http  = beast::http;

    const auto                           root = api_test_root();
    caspar::ograf::manifest_registry     registry(root);
    empty_graphics_service               service;
    caspar::protocol::ograf::router      router(registry, service, "/ograf/v1", "test-version");
    caspar::protocol::ograf::http_server server(router, caspar::protocol::ograf::http_server_config{"127.0.0.1", 0});

    std::mutex              mutex;
    std::condition_variable entered;
    int                     active     = 0;
    bool                    concurrent = false;
    service.play_override = [&] {
        std::unique_lock lock(mutex);
        ++active;
        if (active == 2) {
            concurrent = true;
            entered.notify_all();
        }
        entered.wait_for(lock, std::chrono::seconds(2), [&] { return concurrent; });
        --active;
        return caspar::ograf::action_result{200, "OK", {}, 0, "instance-1"};
    };

    const auto send_play = [port = server.port()] {
        asio::io_context  context;
        beast::tcp_stream stream(context);
        stream.connect({asio::ip::make_address("127.0.0.1"), port});

        http::request<http::string_body> request{
            http::verb::post,
            "/ograf/v1/renderers/casparcg/target/graphicInstance/playAction",
            11};
        request.set(http::field::host, "127.0.0.1");
        request.set(http::field::content_type, "application/json");
        request.body() = R"({"renderTarget":{"channel":1,"layer":20},"graphicInstanceId":"instance-1","params":{"delta":1}})";
        request.prepare_payload();
        http::write(stream, request);

        beast::flat_buffer                buffer;
        http::response<http::string_body> response;
        http::read(stream, buffer, response);
        return response.result_int();
    };

    auto first  = std::async(std::launch::async, send_play);
    auto second = std::async(std::launch::async, send_play);

    BOOST_TEST(first.get() == 200);
    BOOST_TEST(second.get() == 200);
    BOOST_TEST(concurrent);

    std::filesystem::remove_all(root);
}
