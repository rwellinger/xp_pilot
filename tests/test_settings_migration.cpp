#include "../src/settings_migration.hpp"
#include <catch_amalgamated.hpp>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace
{

// Mirrors the real layout: the old file under x_pilot_reports/, the new one under
// preferences/, which may or may not exist yet.
struct TempTree
{
    fs::path root;

    explicit TempTree(const std::string &name) : root(fs::temp_directory_path() / ("xp_pilot_prefs_" + name))
    {
        fs::remove_all(root);
        fs::create_directories(root / "x_pilot_reports");
    }
    ~TempTree() { fs::remove_all(root); }

    fs::path old_file() const { return root / "x_pilot_reports" / "settings.json"; }
    fs::path new_file() const { return root / "preferences" / "xp_pilot.prf"; }

    void write(const fs::path &file, const std::string &content) const
    {
        fs::create_directories(file.parent_path());
        std::ofstream(file) << content;
    }

    std::string read(const fs::path &file) const
    {
        std::ifstream f(file);
        return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
};

using Settings::MigrationOutcome;

const std::string SETTINGS_JSON = R"({"auto_qnh":true,"ui_scale":1.25,"aircraft_profiles":{"DA42":"medium_ga"}})";

} // namespace

TEST_CASE("Settings migration moves the old file and preserves every setting")
{
    TempTree tree("migrate");
    tree.write(tree.old_file(), SETTINGS_JSON);

    REQUIRE(Settings::migrate_settings_file(tree.old_file(), tree.new_file()) == MigrationOutcome::Migrated);
    REQUIRE(fs::exists(tree.new_file()));
    REQUIRE(tree.read(tree.new_file()) == SETTINGS_JSON);
    REQUIRE_FALSE(fs::exists(tree.old_file()));
}

TEST_CASE("Settings migration creates the preferences directory")
{
    TempTree tree("mkdir");
    tree.write(tree.old_file(), SETTINGS_JSON);
    REQUIRE_FALSE(fs::exists(tree.new_file().parent_path()));

    REQUIRE(Settings::migrate_settings_file(tree.old_file(), tree.new_file()) == MigrationOutcome::Migrated);
    REQUIRE(fs::is_directory(tree.new_file().parent_path()));
}

TEST_CASE("A fresh install with no old file has nothing to migrate")
{
    TempTree tree("fresh");

    REQUIRE(Settings::migrate_settings_file(tree.old_file(), tree.new_file()) == MigrationOutcome::NothingToMigrate);
    REQUIRE_FALSE(fs::exists(tree.new_file()));
}

TEST_CASE("An existing new file wins and the old one is left alone")
{
    TempTree tree("both");
    tree.write(tree.old_file(), R"({"auto_qnh":false})");
    tree.write(tree.new_file(), SETTINGS_JSON);

    REQUIRE(Settings::migrate_settings_file(tree.old_file(), tree.new_file()) == MigrationOutcome::NothingToMigrate);
    REQUIRE(tree.read(tree.new_file()) == SETTINGS_JSON);
    REQUIRE(fs::exists(tree.old_file()));
}

TEST_CASE("A malformed old file is reported and left untouched")
{
    TempTree tree("malformed");
    tree.write(tree.old_file(), R"({"auto_qnh": tr)");

    REQUIRE(Settings::migrate_settings_file(tree.old_file(), tree.new_file()) == MigrationOutcome::OldFileMalformed);
    REQUIRE(fs::exists(tree.old_file()));
    REQUIRE_FALSE(fs::exists(tree.new_file()));
}

TEST_CASE("Migration runs only once — a second start finds nothing to do")
{
    TempTree tree("once");
    tree.write(tree.old_file(), SETTINGS_JSON);

    REQUIRE(Settings::migrate_settings_file(tree.old_file(), tree.new_file()) == MigrationOutcome::Migrated);
    REQUIRE(Settings::migrate_settings_file(tree.old_file(), tree.new_file()) == MigrationOutcome::NothingToMigrate);
    REQUIRE(tree.read(tree.new_file()) == SETTINGS_JSON);
}
