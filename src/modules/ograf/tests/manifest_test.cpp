#define BOOST_TEST_MODULE ograf_manifest
#include <boost/test/included/unit_test.hpp>

#include <modules/ograf/manifest/manifest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

class manifest_fixture
{
    static std::atomic_uint counter_;

  public:
    std::filesystem::path root;

    manifest_fixture()
        : root(std::filesystem::temp_directory_path() /
               ("casparcg-ograf-manifest-" + std::to_string(++counter_)))
    {
        std::filesystem::create_directories(root / "graphic");
        write(root / "graphic" / "graphic.mjs", "export default class Graphic extends HTMLElement {}");
    }

    ~manifest_fixture()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    static void write(const std::filesystem::path& path, const std::string& contents)
    {
        std::ofstream stream(path, std::ios::binary);
        stream << contents;
    }

    std::filesystem::path write_manifest(const std::string& contents) const
    {
        const auto path = root / "graphic" / "test.ograf.json";
        write(path, contents);
        return path;
    }
};

std::atomic_uint manifest_fixture::counter_ = 0;

const std::string valid_manifest = R"({
    "$schema": "https://ograf.ebu.io/v1/specification/json-schemas/graphics/schema.json",
    "id": "com.example.lower-third",
    "version": "1.0.0",
    "name": "Lower Third",
    "main": "graphic.mjs",
    "supportsRealTime": true,
    "supportsNonRealTime": false,
    "stepCount": 2,
    "renderRequirements": [{
        "resolution": {
            "width": { "min": 1280 },
            "height": { "exact": 1080 }
        },
        "frameRate": { "min": 50 },
        "accessToPublicInternet": { "exact": false },
        "engine": [{
            "type": "CEF",
            "version": { "min": "120" }
        }]
    }]
})";

const std::string official_minimal_manifest = R"({
    "$schema": "https://ograf.ebu.io/v1/specification/json-schemas/graphics/schema.json",
    "name": "Minimal Test Graphic",
    "description": "This Graphic includes the bare minimum required to be a valid OGraf Graphic. It displays a 'Hello World!' message.",
    "id": "minimal-example",
    "main": "graphic.mjs",
    "supportsRealTime": true,
    "supportsNonRealTime": false,
    "schema": {
        "type": "object",
        "properties": {
            "message": {
                "type": "string",
                "default": "Hello World!",
                "gddType": "color-rrggbb",
                "pattern": "^#[0-9a-f]{6}$",
                "hidden": true
            }
        }
    }
})";

} // namespace

BOOST_AUTO_TEST_CASE(load_valid_realtime_manifest)
{
    manifest_fixture fixture;
    const auto       parsed = caspar::ograf::load_manifest(fixture.write_manifest(valid_manifest));

    BOOST_TEST(parsed.id == "com.example.lower-third");
    BOOST_TEST(parsed.version == "1.0.0");
    BOOST_TEST(parsed.step_count == 2);
    BOOST_TEST(parsed.main_path.filename().string() == "graphic.mjs");
}

BOOST_AUTO_TEST_CASE(match_render_requirements)
{
    manifest_fixture fixture;
    const auto       parsed = caspar::ograf::load_manifest(fixture.write_manifest(valid_manifest));

    caspar::ograf::renderer_capabilities supported{
        1920, 1080, 50, false, {{"CEF", "139.0.1"}},
    };
    caspar::ograf::renderer_capabilities unsupported{
        1920, 1080, 25, false, {{"CEF", "139.0.1"}},
    };

    BOOST_TEST(parsed.supports(supported));
    BOOST_TEST(!parsed.supports(unsupported));
}

BOOST_AUTO_TEST_CASE(load_official_minimal_manifest)
{
    manifest_fixture fixture;
    const auto       parsed =
        caspar::ograf::load_manifest(fixture.write_manifest(official_minimal_manifest));

    BOOST_TEST(parsed.id == "minimal-example");
    BOOST_TEST(parsed.step_count == 1);
}

BOOST_AUTO_TEST_CASE(reject_missing_required_property)
{
    manifest_fixture fixture;
    const auto path = fixture.write_manifest(R"({
        "$schema": "https://ograf.ebu.io/v1/specification/json-schemas/graphics/schema.json",
        "id": "com.example.invalid",
        "name": "Invalid",
        "main": "graphic.mjs",
        "supportsRealTime": true
    })");

    BOOST_CHECK_THROW(caspar::ograf::load_manifest(path), caspar::ograf::manifest_error);
}

BOOST_AUTO_TEST_CASE(reject_unknown_non_vendor_property)
{
    manifest_fixture fixture;
    auto             invalid = valid_manifest;
    invalid.insert(invalid.rfind('}'), R"(, "unexpected": true)");

    BOOST_CHECK_THROW(caspar::ograf::load_manifest(fixture.write_manifest(invalid)),
                      caspar::ograf::manifest_error);
}

BOOST_AUTO_TEST_CASE(accept_vendor_property)
{
    manifest_fixture fixture;
    auto             extended = valid_manifest;
    extended.insert(extended.rfind('}'), R"(, "v_casparcg_test": true)");

    BOOST_CHECK_NO_THROW(caspar::ograf::load_manifest(fixture.write_manifest(extended)));
}

BOOST_AUTO_TEST_CASE(reject_non_realtime_graphic)
{
    manifest_fixture fixture;
    auto             non_realtime = valid_manifest;
    const auto       flag = non_realtime.find("\"supportsRealTime\": true");
    non_realtime.replace(flag, std::string("\"supportsRealTime\": true").size(), "\"supportsRealTime\": false");

    BOOST_CHECK_THROW(caspar::ograf::load_manifest(fixture.write_manifest(non_realtime)),
                      caspar::ograf::manifest_error);
}

BOOST_AUTO_TEST_CASE(reject_main_path_traversal)
{
    manifest_fixture fixture;
    auto             traversal = valid_manifest;
    const auto       main      = traversal.find("\"main\": \"graphic.mjs\"");
    traversal.replace(main, std::string("\"main\": \"graphic.mjs\"").size(), "\"main\": \"../outside.mjs\"");

    BOOST_CHECK_THROW(caspar::ograf::load_manifest(fixture.write_manifest(traversal)),
                      caspar::ograf::manifest_error);
}
