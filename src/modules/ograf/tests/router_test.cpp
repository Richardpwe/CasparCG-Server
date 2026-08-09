#include <boost/test/unit_test.hpp>

#include <modules/ograf/manifest/registry.h>
#include <modules/ograf/service/graphics_service.h>

#include <protocol/ograf/router.h>

#include <boost/json/parse.hpp>

#include <chrono>
#include <filesystem>
#include <string>

namespace {

class empty_graphics_service final : public caspar::ograf::graphics_service_interface
{
  public:
    caspar::ograf::action_result
    load(caspar::ograf::render_target, const std::string&, boost::json::value, std::optional<int>, bool) override
    {
        return {};
    }

    caspar::ograf::action_result play(caspar::ograf::render_target, const std::string&, boost::json::object) override
    {
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
