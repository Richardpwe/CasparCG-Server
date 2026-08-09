#include <boost/test/unit_test.hpp>

#include <modules/ograf/manifest/registry.h>
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

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <string>

namespace {

class empty_graphics_service final : public caspar::ograf::graphics_service_interface
{
  public:
    std::function<caspar::ograf::action_result()> play_override;

    caspar::ograf::action_result
    load(caspar::ograf::render_target, const std::string&, boost::json::value, std::optional<int>, bool) override
    {
        return {};
    }

    caspar::ograf::action_result play(caspar::ograf::render_target, const std::string&, boost::json::object) override
    {
        if (play_override) {
            return play_override();
        }
        return {};
    }

    caspar::ograf::action_result update(caspar::ograf::render_target, const std::string&, boost::json::object) override
    {
        return {};
    }

    caspar::ograf::action_result stop(caspar::ograf::render_target, const std::string&, boost::json::object) override
    {
        return {};
    }

    caspar::ograf::action_result
    invoke_custom(caspar::ograf::render_target, const std::string&, boost::json::object) override
    {
        return {};
    }

    caspar::ograf::action_result dispose(caspar::ograf::render_target, const std::string&) override { return {}; }

    std::vector<caspar::ograf::located_instance> clear(const std::vector<caspar::ograf::graphic_filter>&) override
    {
        return {};
    }

    std::vector<caspar::ograf::render_target> targets() override { return {}; }

    std::vector<caspar::ograf::graphic_instance> instances(caspar::ograf::render_target) override { return {}; }

    std::vector<caspar::ograf::located_instance> all_instances() override { return {}; }

    boost::json::object render_characteristics(caspar::ograf::render_target) const override { return {}; }

    bool has_target(caspar::ograf::render_target) const override { return false; }
};

std::filesystem::path api_test_root()
{
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    auto       root   = std::filesystem::temp_directory_path() / ("casparcg-ograf-api-" + std::to_string(suffix));
    std::filesystem::create_directories(root);
    return root;
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
