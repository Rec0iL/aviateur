#pragma once

// Publishes wfb-ng link statistics for msposd's link widget.
//
// msposd draws the fancy OSD but has no idea wfb-ng exists: it only ever sees
// MSP coming *down* from the air unit, while the radio link lives entirely on
// this side. So the numbers are handed over the same way the widget theme is -
// we write a small ini, msposd polls it. No IPC, no shared library, and nothing
// breaks if msposd is not running.
//
// The file is rewritten about once a second. msposd is reading it on its own
// clock, so the write goes to a temporary and is renamed into place: a rename is
// atomic within a filesystem, which means a reader sees either the old file or
// the new one and never a half-written one.

#include <string>

struct OsdLinkStats {
    // Per antenna. Aviateur's receiver reports two.
    int rssi_dbm[2] = {0, 0};
    int snr_db[2] = {0, 0};
    bool antenna_valid[2] = {false, false};

    // What the receiver is tuned to. Zero means unknown, which the widget shows
    // as nothing rather than as channel zero.
    int channel = 0;
    int freq_mhz = 0;
    int bandwidth_mhz = 0;

    // Negative means "not measured", which the widget shows differently from a
    // measured zero - a link quality of 0 is a dead link, an absent one is a
    // ground station that does not count packets.
    float quality_pct = -1.0f;
    float loss_pct = -1.0f;
    float bitrate_mbps = -1.0f;
};

/// Where the stats file lives.
///
/// The same rule msposd applies when its theme does not name a path:
/// `$MSPOSD_LINK_STATS` if set, otherwise /tmp/msposd-link.ini. Both ends
/// resolving it identically is the point - it means the widget works with
/// nothing configured at either end.
///
/// Not the app data directory, where the theme lives: this is a runtime file
/// two processes share, not configuration, and msposd has no business knowing
/// where Aviateur keeps its settings.
std::string osd_link_stats_path();

/// The centre frequency of an 802.11 channel, or 0 for one we do not know.
/// Aviateur is told a channel number; the widget shows megahertz beside it
/// because pilots read both.
int osd_link_mhz_from_channel(int channel);

/// Notes the decoder's current bitrate, in bits per second, for the next write.
///
/// The decoder reports this already and the GUI shows it; the widget needs the
/// same number and this is the cheapest way to reach it from the receiver
/// thread that does the writing.
void osd_link_note_bitrate(unsigned long long bits_per_second);

/// The last noted bitrate as megabits per second, or -1 before there is one.
///
/// Divided by 1e6, not by 1024x1024. Aviateur's own readout uses the latter and
/// calls the result "Mbps", which is off by five percent - matching that here
/// would be copying a mislabel into a second place.
float osd_link_bitrate_mbps();

/// Writes `stats` to `path`. False if the file could not be written, which the
/// caller is free to ignore - a missing stats file just means no widget.
bool osd_write_link_stats(const std::string &path, const OsdLinkStats &stats);
