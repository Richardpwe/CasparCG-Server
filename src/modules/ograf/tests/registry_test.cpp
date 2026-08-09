#include <boost/test/unit_test.hpp>

#include <modules/ograf/manifest/registry.h>
#include <modules/ograf/manifest/schema_validator.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

class registry_fixture
{
    static std::atomic_uint counter_;

  public:
    std::filesystem::path root;

    registry_fixture()
        : root(std::filesystem::temp_directory_path() /
               ("casparcg-ograf-registry-" + std::to_string(++counter_)))
    {
        std::filesystem::create_directories(root);
    }

    ~registry_fixture()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    static void write(const std::filesystem::path& path, const std::string& contents)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        stream << contents;
    }

    void add_graphic(const std::string& directory,
                     const std::string& manifest_name,
                     const std::string& id) const
    {
        const auto graphic = root / directory;
        write(graphic / "graphic.mjs", "export default class Graphic extends HTMLElement {}");
        write(graphic / manifest_name,
              R"({"$schema":")" + std::string(caspar::ograf::GRAPHICS_SCHEMA_V1) + R"(",)"
                  R"("id":")" + id + R"(",)"
                  R"("name":"Registry test",)"
                  R"("main":"graphic.mjs",)"
                  R"("supportsRealTime":true,)"
                  R"("supportsNonRealTime":false})");
    }
};

std::atomic_uint registry_fixture::counter_ = 0;

} // namespace

BOOST_AUTO_TEST_CASE(scan_manifests_recursively_by_id_and_path)
{
    registry_fixture fixture;
    fixture.add_graphic("package/nested", "lower-third.ograf.json", "com.example.lower-third");

    caspar::ograf::manifest_registry registry(fixture.root);
    const auto                       entries = registry.list();

    BOOST_TEST(entries.size() == 1);
    BOOST_TEST(registry.find("com.example.lower-third") != nullptr);
    BOOST_TEST(registry.find("package/nested/lower-third.ograf.json") != nullptr);
    BOOST_TEST(registry.find("package/nested/lower-third") != nullptr);
}

BOOST_AUTO_TEST_CASE(reject_duplicate_manifest_ids)
{
    registry_fixture fixture;
    fixture.add_graphic("first", "first.ograf.json", "com.example.duplicate");
    fixture.add_graphic("second", "second.ograf.json", "com.example.duplicate");

    caspar::ograf::manifest_registry registry(fixture.root);

    BOOST_TEST(registry.list().empty());
    BOOST_TEST(registry.find("com.example.duplicate") == nullptr);
    BOOST_TEST(registry.errors().size() == 2);
}

BOOST_AUTO_TEST_CASE(refresh_changed_manifest_on_lookup)
{
    registry_fixture fixture;
    fixture.add_graphic("graphic", "graphic.ograf.json", "com.example.before");

    caspar::ograf::manifest_registry registry(fixture.root);
    BOOST_TEST(registry.find("com.example.before") != nullptr);

    fixture.add_graphic("graphic", "graphic.ograf.json", "com.example.after");

    BOOST_TEST(registry.find("com.example.before") == nullptr);
    BOOST_TEST(registry.find("com.example.after") != nullptr);
}

BOOST_AUTO_TEST_CASE(ignore_tombstone_files)
{
    registry_fixture fixture;
    fixture.add_graphic("graphic", "graphic.ograf.json.tombstone", "com.example.deleted");

    caspar::ograf::manifest_registry registry(fixture.root);

    BOOST_TEST(registry.list().empty());
    BOOST_TEST(registry.errors().empty());
}

BOOST_AUTO_TEST_CASE(prefer_html_for_extensionless_conflict)
{
    registry_fixture fixture;
    fixture.add_graphic("", "lower-third.ograf.json", "com.example.lower-third");
    registry_fixture::write(fixture.root / "lower-third.html", "<html></html>");

    caspar::ograf::manifest_registry registry(fixture.root);

    BOOST_TEST(static_cast<int>(caspar::ograf::resolve_template(registry, "lower-third")) ==
               static_cast<int>(caspar::ograf::template_kind::html));
    BOOST_TEST(static_cast<int>(caspar::ograf::resolve_template(registry, "lower-third.ograf.json")) ==
               static_cast<int>(caspar::ograf::template_kind::ograf));

    std::filesystem::remove(fixture.root / "lower-third.html");
    BOOST_TEST(static_cast<int>(caspar::ograf::resolve_template(registry, "lower-third")) ==
               static_cast<int>(caspar::ograf::template_kind::ograf));
}
