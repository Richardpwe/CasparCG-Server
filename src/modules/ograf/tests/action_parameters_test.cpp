#include <boost/test/unit_test.hpp>

#include <modules/ograf/runtime/action_parameters.h>
#include <modules/ograf/runtime/graphic_runtime.h>

BOOST_AUTO_TEST_CASE(action_parameters_map_play_and_next)
{
    const auto default_play = caspar::ograf::play_parameters("");
    BOOST_TEST(default_play.at("delta").as_int64() == 1);

    const auto goto_play = caspar::ograf::play_parameters(R"({"goto":2,"skipAnimation":true})");
    BOOST_TEST(goto_play.at("goto").as_int64() == 2);
    BOOST_TEST(goto_play.at("skipAnimation").as_bool());

    const auto next = caspar::ograf::next_parameters(R"({"skipAnimation":true})");
    BOOST_TEST(next.at("delta").as_int64() == 1);
    BOOST_TEST(next.at("skipAnimation").as_bool());
}

BOOST_AUTO_TEST_CASE(action_parameters_reject_invalid_play_options)
{
    BOOST_CHECK_THROW(caspar::ograf::play_parameters(R"({"goto":1,"delta":1})"), caspar::ograf::action_error);
    BOOST_CHECK_THROW(caspar::ograf::play_parameters(R"({"goto":"1"})"), caspar::ograf::action_error);
    BOOST_CHECK_THROW(caspar::ograf::next_parameters(R"({"delta":2})"), caspar::ograf::action_error);
    BOOST_CHECK_THROW(caspar::ograf::stop_parameters(R"({"skipAnimation":1})"), caspar::ograf::action_error);
}

BOOST_AUTO_TEST_CASE(action_parameters_map_update_and_custom_action)
{
    const auto update = caspar::ograf::update_parameters(R"({"name":"Ada"})", R"({"skipAnimation":true})");
    BOOST_TEST(update.at("data").as_object().at("name").as_string() == "Ada");
    BOOST_TEST(update.at("skipAnimation").as_bool());

    const auto custom =
        caspar::ograf::custom_parameters("accent", R"({"payload":{"color":"red"},"skipAnimation":false})");
    BOOST_TEST(custom.at("id").as_string() == "accent");
    BOOST_TEST(custom.at("payload").as_object().at("color").as_string() == "red");
    BOOST_TEST(!custom.at("skipAnimation").as_bool());
}

BOOST_AUTO_TEST_CASE(action_parameters_reject_invalid_json_shapes)
{
    BOOST_CHECK_THROW(caspar::ograf::play_parameters("[]"), caspar::ograf::action_error);
    BOOST_CHECK_THROW(caspar::ograf::parse_data_parameter("{"), caspar::ograf::action_error);
    BOOST_CHECK_THROW(caspar::ograf::custom_parameters("", "{}"), caspar::ograf::action_error);
}
