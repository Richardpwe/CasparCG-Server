#include <boost/test/unit_test.hpp>

#include <core/producer/cg_proxy.h>

namespace {

class legacy_cg_proxy final : public caspar::core::cg_proxy
{
  public:
    void add(const int layer,
             const std::wstring& template_name,
             const bool play_on_load,
             const std::wstring& start_from_label,
             const std::wstring& data) override
    {
        last_layer = layer;
        last_template = template_name;
        last_play_on_load = play_on_load;
        last_label = start_from_label;
        last_data = data;
        ++add_count;
    }

    void remove(const int layer) override
    {
        last_layer = layer;
        ++remove_count;
    }

    void play(const int layer) override
    {
        last_layer = layer;
        ++play_count;
    }

    void stop(const int layer) override
    {
        last_layer = layer;
        ++stop_count;
    }

    void next(const int layer) override
    {
        last_layer = layer;
        ++next_count;
    }

    void update(const int layer, const std::wstring& data) override
    {
        last_layer = layer;
        last_data = data;
        ++update_count;
    }

    std::wstring invoke(const int layer, const std::wstring& label) override
    {
        last_layer = layer;
        last_label = label;
        ++invoke_count;
        return L"legacy-result:" + label;
    }

    int add_count = 0;
    int remove_count = 0;
    int play_count = 0;
    int stop_count = 0;
    int next_count = 0;
    int update_count = 0;
    int invoke_count = 0;
    int last_layer = 0;
    bool last_play_on_load = false;
    std::wstring last_template;
    std::wstring last_label;
    std::wstring last_data;
};

void check_default_reply(const caspar::core::cg_command_result& result)
{
    BOOST_TEST(result.response_code == 202u);
    BOOST_TEST(result.payload.empty());
}

} // namespace

BOOST_AUTO_TEST_CASE(legacy_cg_proxy_preserves_html_action_behavior)
{
    legacy_cg_proxy proxy;

    BOOST_TEST(!proxy.uses_json_data());
    check_default_reply(proxy.add_action(7, L"news/lower-third", true, L"intro", L"<templateData/>"));
    BOOST_TEST(proxy.add_count == 1);
    BOOST_TEST(proxy.last_layer == 7);
    BOOST_TEST(proxy.last_template.compare(L"news/lower-third") == 0);
    BOOST_TEST(proxy.last_play_on_load);
    BOOST_TEST(proxy.last_label.compare(L"intro") == 0);
    BOOST_TEST(proxy.last_data.compare(L"<templateData/>") == 0);

    check_default_reply(proxy.play_action(7, L"not-json-and-still-valid-for-html"));
    check_default_reply(proxy.next_action(7, L"ignored"));
    check_default_reply(proxy.update_action(7, L"<componentData/>", L"ignored"));
    check_default_reply(proxy.stop_action(7, L"ignored"));
    check_default_reply(proxy.remove_action(7));

    BOOST_TEST(proxy.play_count == 1);
    BOOST_TEST(proxy.next_count == 1);
    BOOST_TEST(proxy.update_count == 1);
    BOOST_TEST(proxy.stop_count == 1);
    BOOST_TEST(proxy.remove_count == 1);
    BOOST_TEST(proxy.last_data.compare(L"<componentData/>") == 0);

    const auto invoke = proxy.invoke_action(7, L"continue", L"plain html payload");
    BOOST_TEST(invoke.response_code == 201u);
    BOOST_TEST(invoke.payload.compare(L"legacy-result:continue") == 0);
    BOOST_TEST(proxy.invoke_count == 1);
}
