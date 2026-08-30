#include "osd_link_writer.h"

#include "../gui_interface.h"

#include <cstdio>
#include <cstdlib>

std::string osd_link_stats_path() {
    return GuiInterface::GetAppDataDir() + "osd-link.ini";
}

bool osd_write_link_stats(const std::string &path, const OsdLinkStats &stats) {
    if (path.empty()) {
        return false;
    }

    // Written to a temporary and renamed into place. msposd polls this file on
    // its own clock, several times a second, so it will land mid-write sooner or
    // later; a rename within one filesystem is atomic, so it sees either the old
    // file or the new one. Its parser refuses a half-written file too, but that
    // is the belt to this braces.
    const std::string tmp = path + ".tmp";
    FILE *f = fopen(tmp.c_str(), "w");
    if (!f) {
        return false;
    }

    fprintf(f,
            "; Link statistics for msposd's link widget. Written by Aviateur,\n"
            "; rewritten about once a second. Point the theme's [link] source at\n"
            "; this file to have the widget appear.\n"
            "source = WFB-NG\n");

    for (int i = 0; i < 2; i++) {
        if (!stats.antenna_valid[i]) {
            continue;
        }
        fprintf(f, "ant%d_rssi = %d\n", i, stats.rssi_dbm[i]);
        fprintf(f, "ant%d_snr = %d\n", i, stats.snr_db[i]);
    }

    // Only what was actually measured. Writing a zero for something we do not
    // know would show up on the OSD as a dead link rather than as a blank.
    if (stats.quality_pct >= 0.0f) {
        fprintf(f, "quality = %.0f\n", stats.quality_pct);
    }
    if (stats.loss_pct >= 0.0f) {
        fprintf(f, "loss = %.2f\n", stats.loss_pct);
    }
    if (stats.bitrate_mbps >= 0.0f) {
        fprintf(f, "bitrate_mbps = %.1f\n", stats.bitrate_mbps);
    }

    fclose(f);

    if (rename(tmp.c_str(), path.c_str()) != 0) {
        remove(tmp.c_str());
        return false;
    }
    return true;
}
