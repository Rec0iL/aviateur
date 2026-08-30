#include "osd_tab.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>

namespace {

/// How long the controls have to be still before the file is written. Long
/// enough that a slider drag is one save, short enough to feel live.
constexpr double kSaveDelaySeconds = 0.35;

/// Trailing zeros are noise on a readout that sits beside a slider.
std::string short_num(float v) {
    std::ostringstream os;
    os << std::setprecision(4) << std::noshowpoint << v;
    return os.str();
}

std::shared_ptr<vecgui::HBoxContainer> make_row(const std::shared_ptr<vecgui::Node>& parent,
                                                const std::string& label,
                                                float label_width) {
    auto row = std::make_shared<vecgui::HBoxContainer>();
    row->set_separation(6);
    row->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
    parent->add_child(row);

    auto name = std::make_shared<vecgui::Label>();
    name->set_text(label);
    name->set_font_size(14);
    name->set_horizontal_alignment(vecgui::Alignment::Begin);
    // Wrapped, so a long field name costs a second line rather than widening the
    // whole panel - the control panel shares the window with the video.
    name->set_word_wrap(true);
    name->set_custom_minimum_size({label_width, 0});
    name->container_sizing.flag_v = vecgui::ContainerSizingFlag::ShrinkCenter;
    row->add_child(name);

    return row;
}

/// Explanatory text under a group of controls. Wrapped and small: an unwrapped
/// label sets the container's minimum width, which would push the video aside.
void add_note(const std::shared_ptr<vecgui::Node>& parent, const std::string& text) {
    auto note = std::make_shared<vecgui::Label>();
    note->set_text(text);
    note->set_font_size(12);
    note->set_word_wrap(true);
    note->set_horizontal_alignment(vecgui::Alignment::Begin);
    note->set_custom_minimum_size({120, 0});
    note->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
    parent->add_child(note);
}

} // namespace

void OsdContainer::on_ready() {
    set_margin_all(8);

    theme_path_ = GuiInterface::GetOsdThemePath();

    // A theme file that does not exist yet is written from the defaults, so the
    // tab always has something real to edit and msposd always has something to
    // read. An existing file wins - it may have been hand-tuned.
    if (!theme_.load(theme_path_)) {
        theme_.write_seed(theme_path_);
        GuiInterface::Instance().PutLog(LogLevel::Info, "Created OSD theme: {}", theme_path_);
    }

    auto scroll = std::make_shared<vecgui::ScrollContainer>();
    scroll->enable_hscroll(false);
    add_child(scroll);

    auto root = std::make_shared<vecgui::VBoxContainer>();
    root->set_separation(8);
    root->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
    scroll->add_child(root);

    // --- what is drawn at all -----------------------------------------------
    {
        auto box = add_section(root, "OSD", false);

        add_check(
            box,
            "Graphical widgets",
            [this] { return theme_.fancy; },
            [this](bool v) { theme_.fancy = v; });

        add_slider(
            box,
            "Opacity",
            0.0f,
            1.0f,
            false,
            [this] { return theme_.global_opacity; },
            [this](float v) { theme_.global_opacity = v; });

        add_slider(
            box,
            "Scale",
            0.3f,
            4.0f,
            false,
            [this] { return theme_.global_scale; },
            [this](float v) { theme_.global_scale = v; });

        add_check(
            box,
            "Show raw FC text underneath",
            [this] { return theme_.glyphs; },
            [this](bool v) { theme_.glyphs = v; });

        add_note(box,
                 "The widgets replace the flight controller's own text, so its glyph layer "
                 "is normally hidden. Turn it back on to see OSD content the recogniser does "
                 "not know about - a tuning page, say - at the cost of the recognised fields "
                 "flickering through underneath.");

        add_check(
            box,
            "Outline text",
            [this] { return theme_.text_outline; },
            [this](bool v) { theme_.text_outline = v; });

        add_color(box, OsdColorField{"text_outline_color", "Outline colour",
                                     &OsdTheme::text_outline_color});

        add_slider(
            box,
            "Outline width",
            1.0f,
            3.0f,
            true,
            [this] { return static_cast<float>(theme_.text_outline_width); },
            [this](float v) { theme_.text_outline_width = static_cast<int>(std::lround(v)); });

        add_note(box,
                 "Every outline on screen uses this colour, the compass included. Light text "
                 "over bright ground is unreadable without it.");
    }

    // --- per-element on/off, the thing you actually retune between flights ---
    {
        auto box = add_section(root, "Elements", false);

        // Hand-built rows of two rather than a GridContainer: the grid reports a
        // minimum height one row short, so whatever follows it is drawn over its
        // last row.
        std::shared_ptr<vecgui::HBoxContainer> pair;
        for (int i = 0; i < OSD_EL_COUNT; i++) {
            if (i % 2 == 0) {
                pair = std::make_shared<vecgui::HBoxContainer>();
                pair->set_separation(4);
                pair->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
                box->add_child(pair);
            }

            auto check = std::make_shared<vecgui::CheckButton>();
            check->set_text(osd_element_label(i));
            check->set_toggled_no_signal(theme_.elem_enabled[i]);
            check->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
            pair->add_child(check);

            check->connect_signal("toggled", [this, i](bool on) {
                theme_.elem_enabled[i] = on;
                mark_dirty();
            });
            refreshers_.emplace_back([this, check, i] { check->set_toggled_no_signal(theme_.elem_enabled[i]); });
        }

        add_note(box,
                 "Latitude and longitude place the map rather than drawing panels of their "
                 "own: latitude marks its top-left corner, longitude its bottom-right.");

        auto sizing = add_section(box, "Per-element opacity and scale", true);
        for (int i = 0; i < OSD_EL_COUNT; i++) {
            add_slider(
                sizing,
                std::string(osd_element_label(i)) + " opacity",
                0.0f,
                1.0f,
                false,
                [this, i] { return theme_.elem_opacity[i]; },
                [this, i](float v) { theme_.elem_opacity[i] = v; });
            add_slider(
                sizing,
                std::string(osd_element_label(i)) + " scale",
                0.3f,
                4.0f,
                false,
                [this, i] { return theme_.elem_scale[i]; },
                [this, i](float v) { theme_.elem_scale[i] = v; });
        }
    }

    // --- map ----------------------------------------------------------------
    {
        auto box = add_section(root, "Map", false);

        add_check(
            box,
            "Enabled",
            [this] { return theme_.map_enabled; },
            [this](bool v) { theme_.map_enabled = v; });

        static const std::vector<std::string> kStyles = {"roads", "satellite", "hybrid"};
        add_choice(
            box,
            "Style",
            kStyles,
            [this] {
                for (size_t i = 0; i < kStyles.size(); i++) {
                    if (kStyles[i] == theme_.map_style) {
                        return static_cast<int>(i);
                    }
                }
                return 2;
            },
            [this](int i) { theme_.map_style = kStyles[i]; });

        static const std::vector<std::string> kOrientations = {"north", "track", "heading"};
        add_choice(
            box,
            "Up is",
            kOrientations,
            [this] {
                for (size_t i = 0; i < kOrientations.size(); i++) {
                    if (kOrientations[i] == theme_.map_orientation) {
                        return static_cast<int>(i);
                    }
                }
                return 0;
            },
            [this](int i) { theme_.map_orientation = kOrientations[i]; });

        add_note(box,
                 "Either turning mode puts what is ahead at the top, and a compass needle is "
                 "drawn because north no longer is. Track-up follows where the aircraft is "
                 "going; heading-up follows where the nose points, which is what agrees with "
                 "a nose-mounted camera. In wind the two differ.");

        add_slider(
            box,
            "Opacity",
            0.0f,
            1.0f,
            false,
            [this] { return theme_.map_opacity; },
            [this](float v) { theme_.map_opacity = v; });

        add_check(
            box,
            "Zoom follows speed",
            [this] { return theme_.map_auto_zoom; },
            [this](bool v) { theme_.map_auto_zoom = v; });

        add_slider(
            box,
            "Fixed zoom",
            1.0f,
            19.0f,
            true,
            [this] { return static_cast<float>(theme_.map_zoom); },
            [this](float v) { theme_.map_zoom = static_cast<int>(std::lround(v)); });

        add_slider(
            box,
            "Zoom min",
            1.0f,
            19.0f,
            true,
            [this] { return static_cast<float>(theme_.map_zoom_min); },
            [this](float v) { theme_.map_zoom_min = static_cast<int>(std::lround(v)); });

        add_slider(
            box,
            "Zoom max",
            1.0f,
            19.0f,
            true,
            [this] { return static_cast<float>(theme_.map_zoom_max); },
            [this](float v) { theme_.map_zoom_max = static_cast<int>(std::lround(v)); });

        add_slider(
            box,
            "Look-ahead (s)",
            1.0f,
            60.0f,
            false,
            [this] { return theme_.map_lookahead; },
            [this](float v) { theme_.map_lookahead = v; });

        add_slider(
            box,
            "Lead (s)",
            0.0f,
            30.0f,
            false,
            [this] { return theme_.map_lead; },
            [this](float v) { theme_.map_lead = v; });

        add_slider(
            box,
            "Lead cap",
            0.0f,
            0.9f,
            false,
            [this] { return theme_.map_lead_max; },
            [this](float v) { theme_.map_lead_max = v; });

        add_number(
            box,
            "Zoom settle (ms)",
            [this] { return theme_.map_zoom_settle_ms; },
            [this](float v) { theme_.map_zoom_settle_ms = v; });

        add_number(
            box,
            "Smoothing (ms)",
            [this] { return theme_.map_smooth_ms; },
            [this](float v) { theme_.map_smooth_ms = v; });

        add_number(
            box,
            "Max width",
            [this] { return static_cast<float>(theme_.map_max_width); },
            [this](float v) { theme_.map_max_width = static_cast<int>(std::lround(v)); });

        add_number(
            box,
            "Max height",
            [this] { return static_cast<float>(theme_.map_max_height); },
            [this](float v) { theme_.map_max_height = static_cast<int>(std::lround(v)); });

        add_string(
            box,
            "Tile cache",
            [this] { return theme_.map_cache_dir; },
            [this](const std::string& v) { theme_.map_cache_dir = v; });
    }

    // --- colours ------------------------------------------------------------
    {
        auto box = add_section(root, "Colours", false);

        add_note(box, "RRGGBB, or AARRGGBB to set transparency.");

        for (const auto& field : OsdTheme::color_fields()) {
            add_color(box, field);
        }
    }

    // --- panel shape --------------------------------------------------------
    {
        auto box = add_section(root, "Panels", true);

        struct Geometry {
            const char* label;
            float OsdTheme::*member;
        };
        static const Geometry kGeometry[] = {
            {"Min width", &OsdTheme::panel_min_width},
            {"Height", &OsdTheme::panel_height},
            {"Tab height", &OsdTheme::tab_height},
            {"Chamfer", &OsdTheme::chamfer},
            {"Pad X", &OsdTheme::pad_x},
            {"Pad Y", &OsdTheme::pad_y},
            {"Bar height", &OsdTheme::bar_height},
            {"Value size", &OsdTheme::value_size},
            {"Label size", &OsdTheme::label_size},
            {"Label tracking", &OsdTheme::label_tracking},
            {"Hatch period", &OsdTheme::hatch_period},
            {"Hatch duty", &OsdTheme::hatch_duty},
            {"Hatch slant", &OsdTheme::hatch_slant},
        };
        for (const auto& [label, member] : kGeometry) {
            add_number(
                box,
                label,
                [this, member] { return theme_.*member; },
                [this, member](float v) { theme_.*member = v; });
        }

        add_number(
            box,
            "Element hold (ms)",
            [this] { return theme_.element_hold_ms; },
            [this](float v) { theme_.element_hold_ms = v; });

        add_note(box,
                 "Element hold must exceed the flight controller's blink off-period, or a "
                 "critical value vanishes exactly when it matters.");

        add_string(
            box,
            "Theme name",
            [this] { return theme_.name; },
            [this](const std::string& v) { theme_.name = v; });

        add_string(
            box,
            "Font",
            [this] { return theme_.font_path; },
            [this](const std::string& v) { theme_.font_path = v; });
    }

    // --- link statistics -----------------------------------------------------
    {
        auto box = add_section(root, "Link stats", true);

        add_note(box,
                 "wfb-ng and APFPV live on this side of the link - the flight controller has "
                 "never heard of them - so unlike every other widget there is no position on "
                 "the OSD grid to inherit. Place it here instead.");

        add_check(
            box,
            "Enabled",
            [this] { return theme_.link_enabled; },
            [this](bool v) { theme_.link_enabled = v; });

        static const std::vector<std::string> kLinkStyles = {"vertical", "horizontal"};
        add_choice(
            box,
            "Layout",
            kLinkStyles,
            [this] { return theme_.link_style == "horizontal" ? 1 : 0; },
            [this](int i) { theme_.link_style = kLinkStyles[i]; });

        add_note(box,
                 "Vertical stacks the antennas, for a left or right edge. Horizontal puts "
                 "them side by side, for the top or bottom.");

        add_slider(
            box,
            "Position X",
            0.0f,
            100.0f,
            false,
            [this] { return theme_.link_x; },
            [this](float v) { theme_.link_x = v; });

        add_slider(
            box,
            "Position Y",
            0.0f,
            100.0f,
            false,
            [this] { return theme_.link_y; },
            [this](float v) { theme_.link_y = v; });

        add_note(box,
                 "The top-left corner, as a percentage of the screen - so a layout carries "
                 "over between a 720p and a 1080p ground station. A value near 100 pulls the "
                 "widget back on screen rather than off the edge.");

        add_slider(
            box,
            "Size",
            0.3f,
            4.0f,
            false,
            [this] { return theme_.link_scale; },
            [this](float v) { theme_.link_scale = v; });

        add_slider(
            box,
            "Opacity",
            0.0f,
            1.0f,
            false,
            [this] { return theme_.link_opacity; },
            [this](float v) { theme_.link_opacity = v; });

        add_string(
            box,
            "Stats file",
            [this] { return theme_.link_source; },
            [this](const std::string& v) { theme_.link_source = v; });

        add_note(box,
                 "Aviateur writes this file about once a second and msposd polls it. It is "
                 "filled in for you; clearing it turns the widget off as surely as the "
                 "switch above.");
    }

    // --- compass ------------------------------------------------------------
    {
        auto box = add_section(root, "Compass", true);

        add_note(box,
                 "Where the compass goes is set on the flight controller - it lands wherever "
                 "you placed the compass bar in the OSD tab. Only the look is set here. The "
                 "heading itself comes from the attitude stream, not from the bar's glyphs, "
                 "so it stays smooth however coarse the bar is.");

        static const std::vector<std::string> kHeadingStyles = {
            "band", "rose", "ring", "navball", "numeric"};
        add_choice(
            box,
            "Style",
            kHeadingStyles,
            [this] {
                for (size_t i = 0; i < kHeadingStyles.size(); i++) {
                    if (kHeadingStyles[i] == theme_.heading_style) {
                        return static_cast<int>(i);
                    }
                }
                return 0;
            },
            [this](int i) { theme_.heading_style = kHeadingStyles[i]; });

        add_note(box,
                 "band - a scrolling tape, the way a jet HUD does it. rose - a round dial, "
                 "nose up. ring - the tape in perspective with the centre magnified. navball "
                 "- a sphere carrying pitch and bank as well. numeric - just the number.");

        add_slider(
            box,
            "Size",
            60.0f,
            1200.0f,
            true,
            [this] { return theme_.heading_size; },
            [this](float v) { theme_.heading_size = v; });

        add_note(box,
                 "Band and ring width, or rose and navball diameter. Neither firmware lets "
                 "you widen the bar itself, so this is where the size comes from.");

        add_slider(
            box,
            "Band span",
            45.0f,
            120.0f,
            true,
            [this] { return theme_.heading_span; },
            [this](float v) { theme_.heading_span = v; });

        add_note(box,
                 "Degrees visible end to end on the band. Narrow reads fast in a turn, wide "
                 "gives more context.");

        add_check(
            box,
            "Mark the ground track",
            [this] { return theme_.heading_show_track; },
            [this](bool v) { theme_.heading_show_track = v; });

        add_note(box,
                 "A second marker at the course over ground. On a wing in wind that is where "
                 "you are actually going, which is not where the nose points - and the gap "
                 "between the two is invisible in the video feed.");

        add_check(
            box,
            "Flip the ring",
            [this] { return theme_.heading_flip; },
            [this](bool v) { theme_.heading_flip = v; });

        add_slider(
            box,
            "Ring lens",
            0.3f,
            1.0f,
            false,
            [this] { return theme_.heading_lens; },
            [this](float v) { theme_.heading_lens = v; });

        add_note(box, "Below 1 the centre of the ring is magnified. 1 is a flat tape.");

        add_check(
            box,
            "Outline the compass",
            [this] { return theme_.heading_outline; },
            [this](bool v) { theme_.heading_outline = v; });

        add_slider(
            box,
            "Compass outline width",
            1.0f,
            5.0f,
            false,
            [this] { return theme_.heading_outline_width; },
            [this](float v) { theme_.heading_outline_width = v; });

        add_note(box,
                 "Outlines the ticks and markers, not just the numbers - a compass is thin "
                 "lines over live video, and thin lines over snow or sky disappear. Uses the "
                 "outline colour from the OSD section.");
    }

    // --- battery ------------------------------------------------------------
    {
        auto box = add_section(root, "Battery", true);

        add_number(
            box,
            "Cell min (V)",
            [this] { return theme_.cell_min; },
            [this](float v) { theme_.cell_min = v; });
        add_number(
            box,
            "Cell max (V)",
            [this] { return theme_.cell_max; },
            [this](float v) { theme_.cell_max = v; });
        add_number(
            box,
            "Amber below (V)",
            [this] { return theme_.cell_warn; },
            [this](float v) { theme_.cell_warn = v; });
        add_number(
            box,
            "Red below (V)",
            [this] { return theme_.cell_crit; },
            [this](float v) { theme_.cell_crit = v; });
    }

    // --- artificial horizon -------------------------------------------------
    {
        auto box = add_section(root, "Horizon", true);

        static const std::vector<std::string> kSchemes = {"classic", "tactical", "mono", "heat"};
        add_choice(
            box,
            "Scheme",
            kSchemes,
            [this] {
                for (size_t i = 0; i < kSchemes.size(); i++) {
                    if (kSchemes[i] == theme_.ahi_scheme) {
                        return static_cast<int>(i);
                    }
                }
                return 1;
            },
            [this](int i) { theme_.ahi_scheme = kSchemes[i]; });

        // "(scheme)" is the empty palette name, which removes the key entirely -
        // an explicit colour would otherwise override the preset forever.
        std::vector<std::string> palette = osd_palette_names();
        palette[0] = "(scheme)";

        struct PaletteField {
            const char* label;
            std::string OsdTheme::*member;
        };
        static const PaletteField kPalette[] = {
            {"Level", &OsdTheme::ahi_level_color},
            {"Moderate", &OsdTheme::ahi_moderate_color},
            {"Steep", &OsdTheme::ahi_steep_color},
            {"Ladder", &OsdTheme::ahi_line_color},
        };
        for (const auto& [label, member] : kPalette) {
            add_choice(
                box,
                label,
                palette,
                [this, member] {
                    const auto& names = osd_palette_names();
                    for (size_t i = 0; i < names.size(); i++) {
                        if (names[i] == theme_.*member) {
                            return static_cast<int>(i);
                        }
                    }
                    return 0;
                },
                [this, member](int i) { theme_.*member = osd_palette_names()[i]; });
        }

        add_number(
            box,
            "Level below (deg)",
            [this] { return theme_.ahi_level_max; },
            [this](float v) { theme_.ahi_level_max = v; });
        add_number(
            box,
            "Steep beyond (deg)",
            [this] { return theme_.ahi_moderate_max; },
            [this](float v) { theme_.ahi_moderate_max = v; });
        add_slider(
            box,
            "Steep thickness",
            1.0f,
            12.0f,
            true,
            [this] { return static_cast<float>(theme_.ahi_steep_thickness); },
            [this](float v) { theme_.ahi_steep_thickness = static_cast<int>(std::lround(v)); });
    }

    // --- where the file is, and how to get back to a known state ------------
    {
        auto box = add_section(root, "Theme file", true);

        add_note(box, theme_path_);

        auto reload = std::make_shared<vecgui::Button>();
        reload->set_text("Reload from file");
        reload->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
        box->add_child(reload);
        reload->connect_signal("triggered", [this] {
            if (theme_.load(theme_path_)) {
                for (const auto& refresh : refreshers_) {
                    refresh();
                }
                set_status("Reloaded");
            } else {
                set_status("Could not read the file");
            }
        });

        auto reset = std::make_shared<vecgui::Button>();
        reset->set_text("Reset to defaults");
        reset->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
        box->add_child(reset);
        reset->connect_signal("triggered", [this] {
            theme_ = OsdTheme();
            for (const auto& refresh : refreshers_) {
                refresh();
            }
            mark_dirty();
            set_status("Reset to defaults");
        });
    }

    status_label_ = std::make_shared<vecgui::Label>();
    status_label_->set_font_size(12);
    status_label_->set_word_wrap(true);
    status_label_->set_horizontal_alignment(vecgui::Alignment::Begin);
    status_label_->set_custom_minimum_size({120, 0});
    status_label_->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
    root->add_child(status_label_);
    set_status("Edits apply live");
}

void OsdContainer::update(const double dt) {
    MarginContainer::update(dt);

    // TextEdit has no change signal, so commit whatever the fields hold once
    // their text stops moving.
    for (auto& binding : text_bindings_) {
        std::string current = binding.edit->get_text();
        if (current != binding.last) {
            binding.last = current;
            binding.apply(current);
            mark_dirty();
        }
    }

    if (!dirty_) {
        return;
    }
    idle_since_change_ += dt;
    if (idle_since_change_ >= kSaveDelaySeconds) {
        save_now();
    }
}

void OsdContainer::mark_dirty() {
    dirty_ = true;
    idle_since_change_ = 0.0;
}

void OsdContainer::save_now() {
    dirty_ = false;
    idle_since_change_ = 0.0;

    if (theme_.save(theme_path_)) {
        set_status("Saved - msposd picks this up on its next frame");
    } else {
        set_status("Could not write " + theme_path_);
        GuiInterface::Instance().PutLog(LogLevel::Error, "Failed to write OSD theme: {}", theme_path_);
    }
}

void OsdContainer::set_status(const std::string& text) {
    if (status_label_) {
        status_label_->set_text(text);
    }
}

std::shared_ptr<vecgui::VBoxContainer> OsdContainer::add_section(const std::shared_ptr<vecgui::Node>& parent,
                                                                 const std::string& title,
                                                                 const bool collapsed) {
    auto section = std::make_shared<vecgui::CollapseContainer>(vecgui::CollapseButtonType::Default);
    section->set_title(title);
    section->set_collapse(collapsed);
    section->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
    parent->add_child(section);

    auto box = std::make_shared<vecgui::VBoxContainer>();
    box->set_separation(6);
    box->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
    section->add_child(box);

    return box;
}

void OsdContainer::add_check(const std::shared_ptr<vecgui::Node>& parent,
                             const std::string& label,
                             const std::function<bool()>& get,
                             const std::function<void(bool)>& set) {
    auto check = std::make_shared<vecgui::CheckButton>();
    check->set_text(label);
    check->set_toggled_no_signal(get());
    check->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
    parent->add_child(check);

    check->connect_signal("toggled", [this, set](bool on) {
        set(on);
        mark_dirty();
    });
    refreshers_.emplace_back([check, get] { check->set_toggled_no_signal(get()); });
}

void OsdContainer::add_slider(const std::shared_ptr<vecgui::Node>& parent,
                              const std::string& label,
                              const float min_value,
                              const float max_value,
                              const bool integer,
                              const std::function<float()>& get,
                              const std::function<void(float)>& set) {
    // Deliberately not a vecgui::Slider. Slider draws itself with raw calls at
    // its global position, which a ScrollContainer's render target offsets out
    // of view - inside this tab it is simply invisible. Verified by putting a
    // bare one here next to a working TextEdit. A clamped numeric field with the
    // range in its name is the honest alternative that does render.
    auto row = make_row(parent,
                        label + " (" + short_num(min_value) + "-" + short_num(max_value) + ")",
                        150);

    auto edit = std::make_shared<vecgui::TextEdit>();
    edit->set_text(short_num(get()));
    edit->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
    row->add_child(edit);

    text_bindings_.push_back({edit, edit->get_text(), [set, min_value, max_value, integer](const std::string& text) {
                                  try {
                                      size_t used = 0;
                                      float parsed = std::stof(text, &used);
                                      if (used == 0) {
                                          return;
                                      }
                                      parsed = std::clamp(parsed, min_value, max_value);
                                      if (integer) {
                                          parsed = std::round(parsed);
                                      }
                                      set(parsed);
                                  } catch (...) {
                                      // Half-typed input keeps the last good value.
                                  }
                              }});
    const size_t index = text_bindings_.size() - 1;
    refreshers_.emplace_back([this, index, get] {
        auto& binding = text_bindings_[index];
        binding.last = short_num(get());
        binding.edit->set_text(binding.last);
    });
}

void OsdContainer::add_number(const std::shared_ptr<vecgui::Node>& parent,
                              const std::string& label,
                              const std::function<float()>& get,
                              const std::function<void(float)>& set) {
    auto row = make_row(parent, label, 112);

    auto edit = std::make_shared<vecgui::TextEdit>();
    edit->set_text(short_num(get()));
    edit->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
    row->add_child(edit);

    // Not set_numbers_only: it rejects '-' and '.', which hatch_slant and every
    // fractional field need.
    text_bindings_.push_back({edit, edit->get_text(), [set](const std::string& text) {
                                  try {
                                      size_t used = 0;
                                      const float parsed = std::stof(text, &used);
                                      if (used > 0) {
                                          set(parsed);
                                      }
                                  } catch (...) {
                                      // Half-typed input keeps the last good value.
                                  }
                              }});
    const size_t index = text_bindings_.size() - 1;
    refreshers_.emplace_back([this, index, get] {
        auto& binding = text_bindings_[index];
        binding.last = short_num(get());
        binding.edit->set_text(binding.last);
    });
}

void OsdContainer::add_string(const std::shared_ptr<vecgui::Node>& parent,
                              const std::string& label,
                              const std::function<std::string()>& get,
                              const std::function<void(const std::string&)>& set) {
    auto row = make_row(parent, label, 88);

    auto edit = std::make_shared<vecgui::TextEdit>();
    edit->set_text(get());
    edit->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
    row->add_child(edit);

    text_bindings_.push_back({edit, edit->get_text(), [set](const std::string& text) { set(text); }});
    const size_t index = text_bindings_.size() - 1;
    refreshers_.emplace_back([this, index, get] {
        auto& binding = text_bindings_[index];
        binding.last = get();
        binding.edit->set_text(binding.last);
    });
}

void OsdContainer::add_choice(const std::shared_ptr<vecgui::Node>& parent,
                              const std::string& label,
                              const std::vector<std::string>& items,
                              const std::function<int()>& get,
                              const std::function<void(int)>& set) {
    auto row = make_row(parent, label, 96);

    auto button = std::make_shared<vecgui::MenuButton>();
    button->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
    row->add_child(button);

    auto menu = button->get_popup_menu().lock();
    for (const auto& item : items) {
        menu->create_item(item);
    }
    button->select_item(get());

    button->connect_signal("item_selected", [this, set](const uint32_t index) {
        set(static_cast<int>(index));
        mark_dirty();
    });
    refreshers_.emplace_back([button, get] { button->select_item(get()); });
}

void OsdContainer::add_color(const std::shared_ptr<vecgui::Node>& parent, const OsdColorField& field) {
    auto row = make_row(parent, field.label, 76);

    auto swatch = std::make_shared<vecgui::Panel>();
    swatch->set_custom_minimum_size({26, 0});
    swatch->container_sizing.flag_v = vecgui::ContainerSizingFlag::Fill;
    row->add_child(swatch);

    auto member = field.member;
    auto paint_swatch = [swatch, this, member] {
        const uint32_t argb = theme_.*member;
        vecgui::StyleBox box;
        box.bg_color = vecgui::ColorU((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, (argb >> 24) & 0xFF);
        box.border_width = 0;
        swatch->theme_override_bg_ = box;
    };
    paint_swatch();

    auto edit = std::make_shared<vecgui::TextEdit>();
    edit->set_text(osd_format_color(theme_.*member));
    edit->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
    row->add_child(edit);

    text_bindings_.push_back({edit, edit->get_text(), [this, member, paint_swatch](const std::string& text) {
                                  uint32_t parsed = 0;
                                  if (osd_parse_color(text, &parsed)) {
                                      theme_.*member = parsed;
                                      paint_swatch();
                                  }
                                  // A half-typed hex string leaves the colour alone.
                              }});
    const size_t index = text_bindings_.size() - 1;
    refreshers_.emplace_back([this, index, member, paint_swatch] {
        auto& binding = text_bindings_[index];
        binding.last = osd_format_color(theme_.*member);
        binding.edit->set_text(binding.last);
        paint_swatch();
    });
}
