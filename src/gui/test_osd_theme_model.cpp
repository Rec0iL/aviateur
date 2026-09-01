// Aviateur's theme model against the themes msposd ships.
//
// Two things here are easy to get wrong in a way nothing else notices. A theme
// that inherits - which is what the whole minimal-* family is - would open in
// the editor as a colour block over defaults if the chain were ignored, and it
// would look almost right. And picking a theme copies its file somewhere else,
// which breaks a relative `inherit` unless it is rewritten on the way.
//
// Standalone, because Aviateur has no test framework and inventing one for a
// single file would be worse than a documented command:
//
//   MSPOSD=../msposd            # wherever the msposd checkout is
//   g++ -Wall -std=c++17 -I 3rd/mINI/src -I src/gui
//       -o /tmp/test_theme_model src/gui/test_osd_theme_model.cpp
//       src/gui/osd_theme_model.cpp src/gui/osd_link_writer.cpp   (one line)
//   MSPOSD_THEMES=$MSPOSD/themes /tmp/test_theme_model
#include "osd_theme_model.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static int fails = 0;
static void ck(const char *name, bool ok) {
    printf("  %-52s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) fails++;
}
static void ck_eq(const char *name, const std::string &got, const std::string &want) {
    const bool ok = got == want;
    printf("  %-52s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) {
        printf("      got  '%s'\n      want '%s'\n", got.c_str(), want.c_str());
        fails++;
    }
}

static void write_file(const fs::path &path, const std::string &text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::trunc);
    out << text;
}

static std::string read_file(const fs::path &path) {
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int main() {
    const char *env = getenv("MSPOSD_THEMES");
    const fs::path themes = env && env[0] ? env : "../msposd/themes";
    if (!fs::is_directory(themes)) {
        printf("no themes at %s - set MSPOSD_THEMES\n", themes.string().c_str());
        return 2;
    }

    const fs::path work = fs::temp_directory_path() / "aviateur-theme-test";
    fs::remove_all(work);
    fs::create_directories(work);

    // --- inheritance ---------------------------------------------------------
    printf("inheritance\n");
    {
        OsdTheme base;
        ck("the family base loads", base.load((themes / "minimal-orchid/theme.ini").string()));

        OsdTheme teal;
        ck("a variant loads", teal.load((themes / "minimal-teal/theme.ini").string()));
        ck_eq("the variant names itself", teal.name, "Minimal Teal");
        // The point of the whole exercise: a variant is a colour block, and
        // everything else has to arrive from its parent.
        ck_eq("layout comes from the parent", teal.panel_shape, base.panel_shape);
        ck("sizing comes from the parent", teal.panel_height == base.panel_height);
        ck("the colour is the variant's own", teal.accent != base.accent);
    }
    {
        // A theme that inherits from itself must not hang the editor, and a
        // parent that is not there must not lose the child.
        write_file(work / "self/theme.ini",
                   "[theme]\ninherit = theme.ini\nname = Self\n[osd]\nscale = 1.75\n");
        OsdTheme self;
        ck("a self-inheriting theme still loads", self.load((work / "self/theme.ini").string()));
        ck("and keeps its own values", self.global_scale == 1.75f);

        write_file(work / "orphan/theme.ini",
                   "[theme]\ninherit = ../nowhere/theme.ini\nname = Orphan\n");
        OsdTheme orphan;
        ck("a missing parent is survivable", orphan.load((work / "orphan/theme.ini").string()));
        ck_eq("and the child is still applied", orphan.name, "Orphan");
    }
    {
        OsdTheme missing;
        ck("a theme that is not there fails", !missing.load((work / "nope.ini").string()));
    }

    // --- the folder ----------------------------------------------------------
    printf("theme folder\n");
    setenv("OSD_THEMES", themes.string().c_str(), 1);
    const fs::path active = work / "active/theme.ini";
    fs::create_directories(active.parent_path());
    {
        ck_eq("the folder is found", osd_themes_dir(active.string()), themes.string());
        const auto list = osd_list_themes(active.string());
        ck("every shipped theme is listed", list.size() >= 8);
        bool has_teal = false, named = true;
        for (const auto &entry : list) {
            if (entry.id == "minimal-teal") {
                has_teal = true;
                named = named && entry.name == "Minimal Teal";
            }
            // A folder with no readable name would show as a blank row.
            named = named && !entry.name.empty();
        }
        ck("by folder name", has_teal);
        ck("and shown by the name inside", named);
    }

    // --- applying ------------------------------------------------------------
    printf("applying\n");
    {
        fs::copy_file(themes / "tactical/theme.ini", active,
                      fs::copy_options::overwrite_existing);
        ck_eq("a copy is recognised by its name", osd_current_theme(active.string()), "tactical");

        const std::string before = read_file(active);
        ck("applying a theme succeeds",
           osd_apply_theme((themes / "minimal-teal/theme.ini").string(), active.string()));
        ck_eq("the active theme is now that one", osd_current_theme(active.string()), "minimal-teal");
        ck("the previous file is kept", fs::exists(active.string() + ".bak"));
        ck_eq("and kept whole", read_file(active.string() + ".bak"), before);

        const std::string copied = read_file(active);
        ck("comments survive the copy", copied.find("; Only the chrome is themed") != std::string::npos);
        // The trap this test exists for: `../minimal-orchid/theme.ini` resolved
        // against the copy points at a sibling of the active theme, which is not
        // where the themes are.
        const size_t at = copied.find("inherit");
        const std::string inherit_value =
            at == std::string::npos ? "" : copied.substr(copied.find('=', at) + 1, 2);
        ck("the theme still names a parent", at != std::string::npos);
        // Absolute, and checked as such: a path merely joined onto a relative
        // themes folder reads fine here and resolves against the copy's own
        // folder at load time, which is a different folder entirely.
        ck("the inherit path was made absolute", inherit_value.find('/') == 1);
        ck("and it points at the family base",
           copied.find(fs::weakly_canonical(themes / "minimal-orchid/theme.ini").string()) !=
               std::string::npos);

        OsdTheme applied, direct;
        applied.load(active.string());
        direct.load((themes / "minimal-teal/theme.ini").string());
        ck("the copy reads the same as the original", applied.panel_shape == direct.panel_shape &&
                                                          applied.accent == direct.accent &&
                                                          applied.panel_height == direct.panel_height);

        // A theme of the pilot's own is not one of the folders, and saying so is
        // the point - the picker shows "Custom" rather than the nearest name.
        write_file(active, "[theme]\nname = Mine\n[osd]\nscale = 2\n");
        ck_eq("an unknown theme is not claimed", osd_current_theme(active.string()), "");

        ck("applying a theme onto itself is a no-op",
           osd_apply_theme((themes / "minimal-red/theme.ini").string(),
                           (themes / "minimal-red/theme.ini").string()));
        ck("and did not touch the folder",
           !fs::exists((themes / "minimal-red/theme.ini").string() + ".bak"));
    }

    fs::remove_all(work);
    printf("%s\n", fails ? "FAILED" : "all good");
    return fails ? 1 : 0;
}
