#include <boost/test/unit_test.hpp>

#include <web/producer/request_policy.h>

BOOST_AUTO_TEST_CASE(ograf_request_policy_allows_embedded_and_loopback_resources)
{
    BOOST_TEST(caspar::web::is_request_url_allowed("file:///template/graphic.mjs", false));
    BOOST_TEST(caspar::web::is_request_url_allowed("data:text/javascript,export%20default%201", false));
    BOOST_TEST(caspar::web::is_request_url_allowed("blob:file:///generated", false));
    BOOST_TEST(caspar::web::is_request_url_allowed("http://localhost:8080/asset", false));
    BOOST_TEST(caspar::web::is_request_url_allowed("https://renderer.localhost/asset", false));
    BOOST_TEST(caspar::web::is_request_url_allowed("ws://127.0.0.2:9000/socket", false));
    BOOST_TEST(caspar::web::is_request_url_allowed("http://[::1]/asset", false));
}

BOOST_AUTO_TEST_CASE(ograf_request_policy_blocks_external_network_by_default)
{
    BOOST_TEST(!caspar::web::is_request_url_allowed("https://example.com/asset", false));
    BOOST_TEST(!caspar::web::is_request_url_allowed("ws://192.168.1.20/socket", false));
    BOOST_TEST(!caspar::web::is_request_url_allowed("ftp://localhost/asset", false));
    BOOST_TEST(!caspar::web::is_request_url_allowed("custom-network:resource", false));
}

BOOST_AUTO_TEST_CASE(ograf_request_policy_can_enable_public_internet)
{
    BOOST_TEST(caspar::web::is_request_url_allowed("https://example.com/asset", true));
    BOOST_TEST(caspar::web::is_request_url_allowed("custom-network:resource", true));
}
