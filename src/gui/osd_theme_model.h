#pragma once

// The msposd widget theme, as data.
//
// msposd re-reads its theme file whenever the mtime changes (osd.c
// widgets_sync_config), which is the whole integration: we write the file, it
// picks the change up on the next frame. No IPC, and nothing breaks if msposd
// is not running.
//
// The keys, the clamped ranges and the defaults below mirror
// msposd/osd/widgets/osd_theme.c and themes/tactical/theme.ini. Those are the
// source of truth - msposd clamps what it reads, so a value out of range here
// degrades the look rather than breaking anything, but they should be kept in
// step. Anything msposd does not recognise is ignored on its side.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

enum OsdElement {
    OSD_EL_VOLTAGE = 0,
    OSD_EL_CURRENT,
    OSD_EL_MAH,
    OSD_EL_ALTITUDE,
    OSD_EL_LATITUDE,
    OSD_EL_LONGITUDE,
    OSD_EL_RSSI,
    OSD_EL_SATS,
    OSD_EL_THROTTLE,
    OSD_EL_FLIGHT_TIME,
    OSD_EL_FLIGHT_MODE,
    OSD_EL_WARNING,
    // The compass bar. It has no reading of its own - the run of arrow glyphs
    // only says where the display goes, and the heading comes from
    // MSP_ATTITUDE - so the opacity and scale here move the compass, and
    // turning it off leaves the flight controller's own bar showing.
    OSD_EL_HEADING_BAR,
    OSD_EL_COUNT,
};

/// The ini key, matching msposd's osd_element_type_name().
const char* osd_element_key(int element);

/// What to call it on screen.
const char* osd_element_label(int element);

struct OsdTheme;

/// One editable colour: the ini key, the label, and where it lives in OsdTheme.
struct OsdColorField {
    const char* key;
    const char* label;
    uint32_t OsdTheme::*member;
};

struct OsdTheme {
    // [osd]
    bool fancy = true; // false = the flight controller's own glyph text
    float global_opacity = 1.0f;
    float global_scale = 1.0f;
    // Widgets replace the flight controller's text, so its glyph layer is
    // normally not drawn. Turning it back on shows OSD content the recogniser
    // does not know about - a tuning page, say - at the cost of the recognised
    // fields flickering through underneath.
    bool glyphs = false;

    // [theme]
    std::string name = "Tactical";
    std::string font_path = "fonts/UbuntuMono-Regular.ttf";
    float panel_min_width = 250.0f;
    float panel_height = 104.0f;
    float tab_height = 36.0f;
    float chamfer = 14.0f;
    float pad_x = 13.0f;
    float pad_y = 11.0f;
    float bar_height = 16.0f;
    float value_size = 25.0f;
    float label_size = 11.0f;
    float label_tracking = 2.5f;
    float hatch_period = 7.0f;
    float hatch_duty = 0.64f;
    float hatch_slant = -0.45f;
    float element_hold_ms = 2000.0f;
    // Outline behind every string, and the colour every outline uses - the
    // compass picks this colour up for its ticks and markers too.
    bool text_outline = true;
    uint32_t text_outline_color = 0xC0000000;
    int text_outline_width = 1;

    // [colors], 0xAARRGGBB
    uint32_t accent = 0xFF00E5FF;
    uint32_t warn = 0xFFFFB300;
    uint32_t crit = 0xFFFF3B30;
    uint32_t good = 0xFF00FF9C;
    uint32_t threat = 0xFFFF6A1F;
    uint32_t panel_fill = 0xD60A1A26;
    uint32_t panel_edge = 0xFF0E3D52;
    uint32_t track = 0xCC06222E;
    uint32_t label = 0xFF4FA8C4;
    uint32_t peak = 0xFF8FD8EA;

    // [voltage] - per-cell bar scale and thresholds
    float cell_min = 3.00f;
    float cell_max = 4.35f;
    float cell_warn = 3.60f;
    float cell_crit = 3.40f;

    // [elements]
    std::array<bool, OSD_EL_COUNT> elem_enabled{};
    std::array<float, OSD_EL_COUNT> elem_opacity{};
    std::array<float, OSD_EL_COUNT> elem_scale{};

    // [ahi]. The four colours are palette names, not RGBA - the horizon is drawn
    // through msposd's fixed colour table. Empty means "leave it to the scheme",
    // and the key is then not written at all.
    std::string ahi_scheme = "tactical";
    std::string ahi_level_color;
    std::string ahi_moderate_color;
    std::string ahi_steep_color;
    std::string ahi_line_color;
    float ahi_level_max = 2.0f;
    float ahi_moderate_max = 10.0f;
    int ahi_steep_thickness = 5;

    // [map]
    bool map_enabled = true;
    std::string map_style = "hybrid"; // roads | satellite | hybrid
    // north = north stays up; track = the map turns so the ground track is up;
    // heading = it turns with the nose, which is what agrees with a
    // nose-mounted camera - in wind the two differ. Either turning mode costs a
    // fixed frame of reference, so msposd draws a compass needle.
    std::string map_orientation = "north";
    int map_zoom = 16;                // used when auto_zoom is off
    float map_opacity = 1.0f;
    bool map_auto_zoom = true;
    int map_zoom_min = 14;
    int map_zoom_max = 17;
    float map_lookahead = 20.0f;
    float map_lead = 6.0f;
    float map_lead_max = 0.35f;
    float map_zoom_settle_ms = 3000.0f;
    float map_smooth_ms = 1500.0f;
    int map_max_width = 420;
    int map_max_height = 300;
    std::string map_cache_dir = "/tmp/msposd-tiles";

    // [heading]. Where the display goes is the flight controller's business -
    // wherever the compass bar is placed - so only the look is set here.
    std::string heading_style = "band"; // band | rose | ring | navball | numeric
    float heading_size = 480.0f;        // band/ring width, or rose/navball diameter
    float heading_span = 90.0f;         // band only: degrees visible end to end
    bool heading_show_track = true;     // a second marker at the ground course
    bool heading_flip = false;          // ring only: curve the other way
    float heading_lens = 0.62f;         // ring only: <1 magnifies the centre
    bool heading_outline = true;        // outline the ticks and markers, not just the text
    float heading_outline_width = 2.0f;

    OsdTheme();

    /// Merges `path` over whatever is already here, the way msposd does: a key
    /// that is absent or malformed keeps its current value, so a half-written
    /// file cannot blank the theme.
    bool load(const std::string& path);

    /// Writes the values back. An existing file is updated in place, which keeps
    /// its comments and ordering - that matters, because the shipped theme
    /// documents what every knob does and a GUI should not eat that.
    bool save(const std::string& path) const;

    /// Writes a fully commented file from these values. Used once, when the
    /// theme file does not exist yet.
    bool write_seed(const std::string& path) const;

    /// The editable colours, in the order they should be shown.
    static const std::array<OsdColorField, 10>& color_fields();
};

/// "RRGGBB" / "AARRGGBB", with or without a leading '#'. False on anything else,
/// so a half-typed value leaves the colour alone instead of blanking it.
bool osd_parse_color(const std::string& text, uint32_t* out);

/// Back to text - 6 digits when fully opaque, 8 when not.
std::string osd_format_color(uint32_t argb);

/// Palette names msposd accepts for the AHI, "" first meaning "use the scheme".
const std::vector<std::string>& osd_palette_names();
