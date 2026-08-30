#include "osd_theme_model.h"

#include <mini/ini.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {

const char* const kElementKeys[OSD_EL_COUNT] = {
    "voltage",
    "current",
    "mah",
    "altitude",
    "latitude",
    "longitude",
    "rssi",
    "sats",
    "throttle",
    "flight_time",
    "flight_mode",
    "warning",
    "heading_bar",
};

const char* const kElementLabels[OSD_EL_COUNT] = {
    "Battery voltage",
    "Current draw",
    "Capacity used",
    "Altitude",
    "Latitude",
    "Longitude",
    "Signal",
    "Satellites",
    "Throttle",
    "Flight time",
    "Flight mode",
    "Messages",
    "Compass",
};

/// msposd strips a trailing `; comment` from every value; mINI does not. Reading
/// a hand-commented theme without this turns "16  ; 1..19" into a parse failure.
std::string strip_comment(std::string v) {
    const auto cut = v.find_first_of(";#");
    if (cut != std::string::npos) {
        v.erase(cut);
    }
    const auto first = v.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = v.find_last_not_of(" \t\r\n");
    return v.substr(first, last - first + 1);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

/// Trailing zeros make a hand-edited file unreadable: 2000.000000 for a value
/// the shipped theme writes as 2000.
std::string fnum(float v) {
    std::ostringstream os;
    os << std::setprecision(6) << std::noshowpoint << v;
    return os.str();
}

bool get_raw(const mINI::INIStructure& ini, const char* section, const char* key, std::string* out) {
    if (!ini.has(section) || !ini.get(section).has(key)) {
        return false;
    }
    const std::string v = strip_comment(ini.get(section).get(key));
    if (v.empty()) {
        return false;
    }
    *out = v;
    return true;
}

void get_float(const mINI::INIStructure& ini, const char* section, const char* key, float* out) {
    std::string raw;
    if (!get_raw(ini, section, key, &raw)) {
        return;
    }
    try {
        size_t used = 0;
        const float parsed = std::stof(raw, &used);
        if (used > 0) {
            *out = parsed;
        }
    } catch (...) {
        // A malformed value keeps the current one, exactly as msposd does.
    }
}

void get_int(const mINI::INIStructure& ini, const char* section, const char* key, int* out) {
    float f = static_cast<float>(*out);
    get_float(ini, section, key, &f);
    *out = static_cast<int>(f);
}

void get_bool(const mINI::INIStructure& ini, const char* section, const char* key, bool* out) {
    std::string raw;
    if (!get_raw(ini, section, key, &raw)) {
        return;
    }
    raw = lower(raw);
    if (raw == "on" || raw == "true" || raw == "1" || raw == "yes") {
        *out = true;
    } else if (raw == "off" || raw == "false" || raw == "0" || raw == "no") {
        *out = false;
    }
}

void get_string(const mINI::INIStructure& ini, const char* section, const char* key, std::string* out) {
    std::string raw;
    if (get_raw(ini, section, key, &raw)) {
        *out = raw;
    }
}

void get_color(const mINI::INIStructure& ini, const char* section, const char* key, uint32_t* out) {
    std::string raw;
    if (!get_raw(ini, section, key, &raw)) {
        return;
    }
    uint32_t parsed = 0;
    if (osd_parse_color(raw, &parsed)) {
        *out = parsed;
    }
}

/// One palette-name field, or "" when the key is absent and the scheme decides.
void get_palette(const mINI::INIStructure& ini, const char* key, std::string* out) {
    std::string raw;
    if (get_raw(ini, "ahi", key, &raw)) {
        *out = lower(raw);
    }
}

const char* on_off(bool v) {
    return v ? "on" : "off";
}

} // namespace

const char* osd_element_key(int element) {
    if (element < 0 || element >= OSD_EL_COUNT) {
        return "none";
    }
    return kElementKeys[element];
}

const char* osd_element_label(int element) {
    if (element < 0 || element >= OSD_EL_COUNT) {
        return "";
    }
    return kElementLabels[element];
}

const std::vector<std::string>& osd_palette_names() {
    // Empty first: "leave it to the scheme", which writes no key at all.
    static const std::vector<std::string> names = {
        "", "red", "green", "blue", "yellow", "magenta", "cyan", "white", "black", "gray"};
    return names;
}

const std::array<OsdColorField, 10>& OsdTheme::color_fields() {
    static const std::array<OsdColorField, 10> fields = {{
        {"accent", "Accent", &OsdTheme::accent},
        {"warn", "Warning", &OsdTheme::warn},
        {"crit", "Critical", &OsdTheme::crit},
        {"good", "Good", &OsdTheme::good},
        {"threat", "Threat", &OsdTheme::threat},
        {"panel_fill", "Panel fill", &OsdTheme::panel_fill},
        {"panel_edge", "Panel edge", &OsdTheme::panel_edge},
        {"track", "Bar track", &OsdTheme::track},
        {"label", "Label", &OsdTheme::label},
        {"peak", "Peak marker", &OsdTheme::peak},
    }};
    return fields;
}

bool osd_parse_color(const std::string& text, uint32_t* out) {
    if (!out) {
        return false;
    }
    std::string v = strip_comment(text);
    if (!v.empty() && v[0] == '#') {
        v.erase(0, 1);
    }
    if (v.size() != 6 && v.size() != 8) {
        return false;
    }
    for (const char c : v) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    const uint32_t raw = static_cast<uint32_t>(std::stoul(v, nullptr, 16));
    *out = (v.size() == 6) ? (0xFF000000u | raw) : raw;
    return true;
}

std::string osd_format_color(const uint32_t argb) {
    std::ostringstream os;
    os << std::uppercase << std::hex << std::setfill('0');
    if ((argb >> 24) == 0xFFu) {
        os << std::setw(6) << (argb & 0x00FFFFFFu);
    } else {
        os << std::setw(8) << argb;
    }
    return os.str();
}

OsdTheme::OsdTheme() {
    elem_enabled.fill(true);
    elem_opacity.fill(1.0f);
    elem_scale.fill(1.0f);
    // Icon-only, so it needs less room than a labelled panel.
    elem_scale[OSD_EL_SATS] = 0.7f;
}

bool OsdTheme::load(const std::string& path) {
    mINI::INIFile file(path);
    mINI::INIStructure ini;
    if (!file.read(ini)) {
        return false;
    }

    std::string mode = fancy ? "fancy" : "classic";
    get_string(ini, "osd", "mode", &mode);
    fancy = lower(mode) != "classic";
    get_float(ini, "osd", "opacity", &global_opacity);
    get_float(ini, "osd", "scale", &global_scale);
    get_bool(ini, "osd", "glyphs", &glyphs);

    get_string(ini, "theme", "name", &name);
    get_string(ini, "theme", "font", &font_path);
    get_float(ini, "theme", "panel_min_width", &panel_min_width);
    get_float(ini, "theme", "panel_height", &panel_height);
    get_float(ini, "theme", "tab_height", &tab_height);
    get_float(ini, "theme", "chamfer", &chamfer);
    get_float(ini, "theme", "pad_x", &pad_x);
    get_float(ini, "theme", "pad_y", &pad_y);
    get_float(ini, "theme", "bar_height", &bar_height);
    get_float(ini, "theme", "value_size", &value_size);
    get_float(ini, "theme", "label_size", &label_size);
    get_float(ini, "theme", "label_tracking", &label_tracking);
    get_float(ini, "theme", "hatch_period", &hatch_period);
    get_float(ini, "theme", "hatch_duty", &hatch_duty);
    get_float(ini, "theme", "hatch_slant", &hatch_slant);
    get_float(ini, "theme", "element_hold_ms", &element_hold_ms);
    get_bool(ini, "theme", "text_outline", &text_outline);
    get_color(ini, "theme", "text_outline_color", &text_outline_color);
    get_int(ini, "theme", "text_outline_width", &text_outline_width);

    for (const auto& f : color_fields()) {
        get_color(ini, "colors", f.key, &(this->*f.member));
    }

    get_float(ini, "voltage", "cell_min", &cell_min);
    get_float(ini, "voltage", "cell_max", &cell_max);
    get_float(ini, "voltage", "cell_warn", &cell_warn);
    get_float(ini, "voltage", "cell_crit", &cell_crit);

    for (int i = 0; i < OSD_EL_COUNT; i++) {
        const std::string key = osd_element_key(i);
        bool on = elem_enabled[i];
        get_bool(ini, "elements", key.c_str(), &on);
        elem_enabled[i] = on;
        get_float(ini, "elements", (key + "_opacity").c_str(), &elem_opacity[i]);
        get_float(ini, "elements", (key + "_scale").c_str(), &elem_scale[i]);
    }

    get_string(ini, "ahi", "scheme", &ahi_scheme);
    ahi_scheme = lower(ahi_scheme);
    get_palette(ini, "level_color", &ahi_level_color);
    get_palette(ini, "moderate_color", &ahi_moderate_color);
    get_palette(ini, "steep_color", &ahi_steep_color);
    get_palette(ini, "line_color", &ahi_line_color);
    get_float(ini, "ahi", "level_max", &ahi_level_max);
    get_float(ini, "ahi", "moderate_max", &ahi_moderate_max);
    get_int(ini, "ahi", "steep_thickness", &ahi_steep_thickness);

    get_string(ini, "heading", "style", &heading_style);
    heading_style = lower(heading_style);
    get_float(ini, "heading", "size", &heading_size);
    get_float(ini, "heading", "span", &heading_span);
    get_bool(ini, "heading", "show_track", &heading_show_track);
    get_bool(ini, "heading", "flip", &heading_flip);
    get_float(ini, "heading", "lens", &heading_lens);
    get_bool(ini, "heading", "outline", &heading_outline);
    get_float(ini, "heading", "outline_width", &heading_outline_width);

    get_bool(ini, "map", "enabled", &map_enabled);
    get_string(ini, "map", "style", &map_style);
    map_style = lower(map_style);
    get_string(ini, "map", "orientation", &map_orientation);
    map_orientation = lower(map_orientation);
    get_int(ini, "map", "zoom", &map_zoom);
    get_float(ini, "map", "opacity", &map_opacity);
    get_bool(ini, "map", "auto_zoom", &map_auto_zoom);
    get_int(ini, "map", "zoom_min", &map_zoom_min);
    get_int(ini, "map", "zoom_max", &map_zoom_max);
    get_float(ini, "map", "lookahead", &map_lookahead);
    get_float(ini, "map", "lead", &map_lead);
    get_float(ini, "map", "lead_max", &map_lead_max);
    get_float(ini, "map", "zoom_settle_ms", &map_zoom_settle_ms);
    get_float(ini, "map", "smooth_ms", &map_smooth_ms);
    get_int(ini, "map", "max_width", &map_max_width);
    get_int(ini, "map", "max_height", &map_max_height);
    get_string(ini, "map", "cache_dir", &map_cache_dir);

    return true;
}

bool OsdTheme::save(const std::string& path) const {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return write_seed(path);
    }

    mINI::INIFile file(path);
    mINI::INIStructure ini;
    file.read(ini);

    ini["osd"]["mode"] = fancy ? "fancy" : "classic";
    ini["osd"]["opacity"] = fnum(global_opacity);
    ini["osd"]["scale"] = fnum(global_scale);
    ini["osd"]["glyphs"] = on_off(glyphs);

    ini["theme"]["name"] = name;
    ini["theme"]["font"] = font_path;
    ini["theme"]["panel_min_width"] = fnum(panel_min_width);
    ini["theme"]["panel_height"] = fnum(panel_height);
    ini["theme"]["tab_height"] = fnum(tab_height);
    ini["theme"]["chamfer"] = fnum(chamfer);
    ini["theme"]["pad_x"] = fnum(pad_x);
    ini["theme"]["pad_y"] = fnum(pad_y);
    ini["theme"]["bar_height"] = fnum(bar_height);
    ini["theme"]["value_size"] = fnum(value_size);
    ini["theme"]["label_size"] = fnum(label_size);
    ini["theme"]["label_tracking"] = fnum(label_tracking);
    ini["theme"]["hatch_period"] = fnum(hatch_period);
    ini["theme"]["hatch_duty"] = fnum(hatch_duty);
    ini["theme"]["hatch_slant"] = fnum(hatch_slant);
    ini["theme"]["element_hold_ms"] = fnum(element_hold_ms);
    ini["theme"]["text_outline"] = on_off(text_outline);
    ini["theme"]["text_outline_color"] = osd_format_color(text_outline_color);
    ini["theme"]["text_outline_width"] = std::to_string(text_outline_width);

    for (const auto& f : color_fields()) {
        ini["colors"][f.key] = osd_format_color(this->*f.member);
    }

    ini["voltage"]["cell_min"] = fnum(cell_min);
    ini["voltage"]["cell_max"] = fnum(cell_max);
    ini["voltage"]["cell_warn"] = fnum(cell_warn);
    ini["voltage"]["cell_crit"] = fnum(cell_crit);

    for (int i = 0; i < OSD_EL_COUNT; i++) {
        const std::string key = osd_element_key(i);
        ini["elements"][key] = on_off(elem_enabled[i]);
        ini["elements"][key + "_opacity"] = fnum(elem_opacity[i]);
        ini["elements"][key + "_scale"] = fnum(elem_scale[i]);
    }

    ini["ahi"]["scheme"] = ahi_scheme;
    // An individual colour overrides the scheme in msposd, so "inherit" has to
    // mean the key is gone, not the key set to something.
    const std::pair<const char*, const std::string*> palette[] = {
        {"level_color", &ahi_level_color},
        {"moderate_color", &ahi_moderate_color},
        {"steep_color", &ahi_steep_color},
        {"line_color", &ahi_line_color},
    };
    for (const auto& [key, value] : palette) {
        if (value->empty()) {
            ini["ahi"].remove(key);
        } else {
            ini["ahi"][key] = *value;
        }
    }
    ini["ahi"]["level_max"] = fnum(ahi_level_max);
    ini["ahi"]["moderate_max"] = fnum(ahi_moderate_max);
    ini["ahi"]["steep_thickness"] = std::to_string(ahi_steep_thickness);

    ini["heading"]["style"] = heading_style;
    ini["heading"]["size"] = fnum(heading_size);
    ini["heading"]["span"] = fnum(heading_span);
    ini["heading"]["show_track"] = on_off(heading_show_track);
    ini["heading"]["flip"] = on_off(heading_flip);
    ini["heading"]["lens"] = fnum(heading_lens);
    ini["heading"]["outline"] = on_off(heading_outline);
    ini["heading"]["outline_width"] = fnum(heading_outline_width);

    ini["map"]["enabled"] = on_off(map_enabled);
    ini["map"]["style"] = map_style;
    ini["map"]["orientation"] = map_orientation;
    ini["map"]["zoom"] = std::to_string(map_zoom);
    ini["map"]["opacity"] = fnum(map_opacity);
    ini["map"]["auto_zoom"] = on_off(map_auto_zoom);
    ini["map"]["zoom_min"] = std::to_string(map_zoom_min);
    ini["map"]["zoom_max"] = std::to_string(map_zoom_max);
    ini["map"]["lookahead"] = fnum(map_lookahead);
    ini["map"]["lead"] = fnum(map_lead);
    ini["map"]["lead_max"] = fnum(map_lead_max);
    ini["map"]["zoom_settle_ms"] = fnum(map_zoom_settle_ms);
    ini["map"]["smooth_ms"] = fnum(map_smooth_ms);
    ini["map"]["max_width"] = std::to_string(map_max_width);
    ini["map"]["max_height"] = std::to_string(map_max_height);
    ini["map"]["cache_dir"] = map_cache_dir;

    // write() updates the file in place, so the comments below survive being
    // edited from the GUI. It only rewrites the value part of a changed line -
    // which is why every comment in write_seed() is on its own line. An inline
    // comment would be eaten the first time you touched that knob.
    return file.write(ini, true);
}

bool OsdTheme::write_seed(const std::string& path) const {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

    std::ofstream f(path, std::ios::trunc);
    if (!f) {
        return false;
    }

    // Every comment is a whole line. mINI preserves those when it rewrites a
    // value; an inline comment on a key line would be destroyed the first time
    // that key changed.
    f << "; msposd widget theme, written by Aviateur's OSD tab.\n"
         "; msposd re-reads this file whenever its mtime changes, so edits apply live.\n"
         "; Safe to hand-edit - keep comments on their own line and they will survive.\n"
         "; Unknown keys are ignored and bad values keep msposd's default, so a mistake\n"
         "; in here degrades the look but never takes the OSD down.\n\n";

    f << "[osd]\n"
         "; classic = the flight controller's own glyph text, untouched\n"
         "; fancy   = graphical widgets\n"
      << "mode = " << (fancy ? "fancy" : "classic") << "\n"
      << "opacity = " << fnum(global_opacity) << "\n"
      << "scale = " << fnum(global_scale) << "\n"
         "; The widgets replace the flight controller's own text, so its glyph layer is\n"
         "; normally not drawn at all. Turning it back on shows OSD content the recogniser\n"
         "; does not know about - a tuning page, say - at the cost of the recognised\n"
         "; fields flickering through underneath.\n"
      << "glyphs = " << on_off(glyphs) << "\n\n";

    f << "[theme]\n"
      << "name = " << name << "\n"
      << "font = " << font_path << "\n"
      << "panel_min_width = " << fnum(panel_min_width) << "\n"
      << "panel_height = " << fnum(panel_height) << "\n"
         "; raised tab holding the value\n"
      << "tab_height = " << fnum(tab_height) << "\n"
         "; bottom-right corner cut\n"
      << "chamfer = " << fnum(chamfer) << "\n"
      << "pad_x = " << fnum(pad_x) << "\n"
      << "pad_y = " << fnum(pad_y) << "\n"
      << "bar_height = " << fnum(bar_height) << "\n"
      << "value_size = " << fnum(value_size) << "\n"
      << "label_size = " << fnum(label_size) << "\n"
      << "label_tracking = " << fnum(label_tracking) << "\n"
         "; stripe pitch in px, the fraction of it that is stripe, and the lean\n"
      << "hatch_period = " << fnum(hatch_period) << "\n"
      << "hatch_duty = " << fnum(hatch_duty) << "\n"
      << "hatch_slant = " << fnum(hatch_slant) << "\n"
         "; How long a vanished element keeps being drawn, ms. Flight controllers blink\n"
         "; critical values by alternating them with blank, so this must exceed the blink\n"
         "; off-period or the widget disappears exactly when the reading matters most.\n"
      << "element_hold_ms = " << fnum(element_hold_ms) << "\n"
         "; Outline behind every string, and the colour every outline on screen uses -\n"
         "; the compass picks this colour up for its ticks and markers too. Small light\n"
         "; text over a bright frame is unreadable without it.\n"
      << "text_outline = " << on_off(text_outline) << "\n"
      << "text_outline_color = " << osd_format_color(text_outline_color) << "\n"
      << "text_outline_width = " << text_outline_width << "\n\n";

    f << "[colors]\n"
         "; RRGGBB or AARRGGBB\n";
    for (const auto& fld : color_fields()) {
        f << fld.key << " = " << osd_format_color(this->*fld.member) << "\n";
    }
    f << "\n";

    f << "[voltage]\n"
         "; per-cell bar scale, then the amber and red thresholds\n"
      << "cell_min = " << fnum(cell_min) << "\n"
      << "cell_max = " << fnum(cell_max) << "\n"
      << "cell_warn = " << fnum(cell_warn) << "\n"
      << "cell_crit = " << fnum(cell_crit) << "\n\n";

    f << "[elements]\n"
         "; Which recognised elements are drawn as widgets, plus per-element opacity\n"
         "; (0..1) and size scale (0.3..4.0).\n"
         "; lat/lon drive the map rather than drawing value panels of their own: where\n"
         "; you place them on the flight controller defines the map's rectangle -\n"
         "; latitude at the top-left corner, longitude at the bottom-right.\n";
    for (int i = 0; i < OSD_EL_COUNT; i++) {
        const std::string key = osd_element_key(i);
        f << key << " = " << on_off(elem_enabled[i]) << "\n"
          << key << "_opacity = " << fnum(elem_opacity[i]) << "\n"
          << key << "_scale = " << fnum(elem_scale[i]) << "\n";
    }
    f << "\n";

    f << "[ahi]\n"
         "; scheme picks a preset: classic | tactical | mono | heat\n"
      << "scheme = " << ahi_scheme << "\n"
         "; Individual palette colours override the preset. Names, not RGBA - the\n"
         "; horizon is drawn through msposd's fixed colour table.\n";
    if (!ahi_level_color.empty()) {
        f << "level_color = " << ahi_level_color << "\n";
    }
    if (!ahi_moderate_color.empty()) {
        f << "moderate_color = " << ahi_moderate_color << "\n";
    }
    if (!ahi_steep_color.empty()) {
        f << "steep_color = " << ahi_steep_color << "\n";
    }
    if (!ahi_line_color.empty()) {
        f << "line_color = " << ahi_line_color << "\n";
    }
    f << "; degrees; below level_max the aircraft counts as level, beyond moderate_max steep\n"
      << "level_max = " << fnum(ahi_level_max) << "\n"
      << "moderate_max = " << fnum(ahi_moderate_max) << "\n"
         "; the centre bar thickens when steep\n"
      << "steep_thickness = " << ahi_steep_thickness << "\n\n";

    f << "[heading]\n"
         "; Where the compass goes comes from the flight controller - wherever the\n"
         "; compass bar is placed - and the heading itself from MSP_ATTITUDE, so none of\n"
         "; this depends on reading the bar's glyphs. Both firmwares fix the bar's width,\n"
         "; which is why the size is set here instead.\n"
         "; band | rose | ring | navball | numeric\n"
      << "style = " << heading_style << "\n"
         "; Band/ring width, or rose/navball diameter, in pixels.\n"
      << "size = " << fnum(heading_size) << "\n"
         "; Band only: degrees visible end to end, 45..120. Narrow reads fast in a turn,\n"
         "; wide gives more context.\n"
      << "span = " << fnum(heading_span) << "\n"
         "; A second marker at the ground course. On a wing in wind this is where you are\n"
         "; actually going, which is not where the nose points.\n"
      << "show_track = " << on_off(heading_show_track) << "\n"
         "; Ring only: which way the arc curves, and how hard the centre is magnified.\n"
      << "flip = " << on_off(heading_flip) << "\n"
      << "lens = " << fnum(heading_lens) << "\n"
         "; Outline behind the ticks and markers as well as the numbers. A compass is\n"
         "; thin lines over live video, and thin lines over snow or sky disappear.\n"
      << "outline = " << on_off(heading_outline) << "\n"
      << "outline_width = " << fnum(heading_outline_width) << "\n\n";

    f << "[map]\n"
      << "enabled = " << on_off(map_enabled) << "\n"
         "; roads | satellite | hybrid\n"
      << "style = " << map_style << "\n"
         "; north = north stays up; track = the map turns so your ground track is up;\n"
         "; heading = it turns with the nose, which is what agrees with a nose-mounted\n"
         "; camera - in wind the two differ. Either turning mode is easier to fly to and\n"
         "; harder to orient by, so a compass needle is drawn whenever one is on.\n"
      << "orientation = " << map_orientation << "\n"
         "; 1..19, used when auto_zoom is off\n"
      << "zoom = " << map_zoom << "\n"
      << "opacity = " << fnum(map_opacity) << "\n"
         "; The view follows the ground track rather than sitting still on the aircraft.\n"
         "; A fixed zoom is wrong at both ends of the speed range: hovering it shows a\n"
         "; patch you could walk across, and at 30 m/s it shows where you have been.\n"
      << "auto_zoom = " << on_off(map_auto_zoom) << "\n"
         "; furthest out the map will go, and closest in - also where it sits stationary\n"
      << "zoom_min = " << map_zoom_min << "\n"
      << "zoom_max = " << map_zoom_max << "\n"
         "; seconds of travel that must fit across the map\n"
      << "lookahead = " << fnum(map_lookahead) << "\n"
         "; How far ahead of the aircraft the map centres, in seconds of travel, and how\n"
         "; far back that is allowed to push the marker. `lead` sets how quickly the\n"
         "; marker slides back as you speed up; `lead_max` is where it stops - a fraction\n"
         "; of the half-viewport, so the marker can never leave its own map.\n"
      << "lead = " << fnum(map_lead) << "\n"
      << "lead_max = " << fnum(map_lead_max) << "\n"
         "; A zoom change throws away every tile on screen and fetches a new set, so a new\n"
         "; zoom has to be what the speed has been asking for this long before it is taken.\n"
      << "zoom_settle_ms = " << fnum(map_zoom_settle_ms) << "\n"
         "; easing time constant for the speed and the lead offset\n"
      << "smooth_ms = " << fnum(map_smooth_ms) << "\n"
         "; Upper bound on the rectangle the two elements span. Without a cap, placing\n"
         "; latitude and longitude far apart covers most of the video, which is unflyable.\n"
      << "max_width = " << map_max_width << "\n"
      << "max_height = " << map_max_height << "\n"
      << "cache_dir = " << map_cache_dir << "\n";

    return f.good();
}
