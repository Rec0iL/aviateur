// Aviateur's link-stats writer against msposd's parser.
//
// The two live in different repositories and talk to each other through nothing
// but the key names in a small ini. Nothing at build time connects them, so a
// key renamed on one side would silently stop the widget on the other - the OSD
// would simply show no link panel, with no error anywhere. This is the check
// that catches that.
//
// Standalone, because Aviateur has no test framework and inventing one for a
// single file would be worse than a documented command:
//
//   MSPOSD=../msposd            # wherever the msposd checkout is
//   PIXELPILOT=../pixelpilot    # and the PixelPilot one
//   gcc -c -w -I $MSPOSD -D_GNU_SOURCE -o /tmp/link.o \
//       $MSPOSD/osd/widgets/osd_link_stats.c
//   gcc -c -w -I $PIXELPILOT/src -o /tmp/ppwriter.o \
//       $PIXELPILOT/src/osd_link_writer.c
//   g++ -Wall -I $MSPOSD -I src/gui -I $PIXELPILOT/src -D_GNU_SOURCE \
//       -o /tmp/test_link src/gui/test_osd_link_writer.cpp \
//       src/gui/osd_link_writer.cpp /tmp/link.o /tmp/ppwriter.o -lm
//   /tmp/test_link
//
// PixelPilot's writer is in there because the default path is now resolved
// independently in three places, and three copies of one rule is exactly the
// kind of thing that drifts. If they ever disagree the widget goes blank with
// no error anywhere, so the agreement is asserted rather than assumed.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
extern "C" {
#include "osd/widgets/osd_link.h"
}
struct OsdLinkStats {
    int rssi_dbm[2] = {0, 0};
    int snr_db[2] = {0, 0};
    bool antenna_valid[2] = {false, false};
    float quality_pct = -1.0f, loss_pct = -1.0f, bitrate_mbps = -1.0f;
};
bool osd_write_link_stats(const std::string &path, const OsdLinkStats &stats);
std::string osd_link_stats_path();

int fails = 0;
static void ck(const char *n, bool ok) { printf("  %-46s %s\n", n, ok ? "PASS" : "FAIL"); if (!ok) fails++; }

// PixelPilot's writer, which resolves the same default path.
extern "C" {
const char *pp_osd_link_stats_path(void);
}

int main() {
    // All three ends have to agree about where the file lives, with and without
    // the environment override. Nothing at build time connects them.
    unsetenv("MSPOSD_LINK_STATS");
    ck("default path agrees with msposd", osd_link_stats_path() == osd_link_default_path());
    ck("default path agrees with PixelPilot",
       osd_link_stats_path() == std::string(pp_osd_link_stats_path()));
    setenv("MSPOSD_LINK_STATS", "/tmp/_agreed_link.ini", 1);
    ck("override agrees with msposd", osd_link_stats_path() == osd_link_default_path());
    ck("override agrees with PixelPilot",
       osd_link_stats_path() == std::string(pp_osd_link_stats_path()));
    unsetenv("MSPOSD_LINK_STATS");

    OsdLinkStats w;
    w.rssi_dbm[0] = -58; w.snr_db[0] = 18; w.antenna_valid[0] = true;
    w.rssi_dbm[1] = -71; w.snr_db[1] = 11; w.antenna_valid[1] = true;
    w.quality_pct = 97.0f; w.loss_pct = 0.42f;
    const std::string p = "/tmp/_rt_link.ini";
    ck("writer reports success", osd_write_link_stats(p, w));

    osd_link_stats_t r; memset(&r, 0, sizeof(r));
    ck("parser reads the written file", osd_link_stats_load(p.c_str(), &r, 1000));
    ck("source survives", strcmp(r.source, "WFB-NG") == 0);
    ck("two antennas", r.antennas == 2);
    ck("ant0 rssi", r.rssi_valid[0] && r.rssi_dbm[0] == -58);
    ck("ant0 snr",  r.snr_valid[0]  && r.snr_db[0]   == 18);
    ck("ant1 rssi", r.rssi_valid[1] && r.rssi_dbm[1] == -71);
    ck("quality",   r.quality_pct > 96.9f && r.quality_pct < 97.1f);
    ck("loss",      r.loss_pct > 0.41f && r.loss_pct < 0.43f);
    ck("bitrate stays absent", r.bitrate_mbps < 0.0f);

    // A silent antenna must not be published at all.
    OsdLinkStats w2;
    w2.rssi_dbm[0] = -60; w2.snr_db[0] = 12; w2.antenna_valid[0] = true;
    osd_write_link_stats(p, w2);
    memset(&r, 0, sizeof(r));
    osd_link_stats_load(p.c_str(), &r, 1000);
    ck("a silent antenna is omitted", r.antennas == 1 && !r.rssi_valid[1]);
    remove(p.c_str());
    printf("\n%s (%d failures)\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails != 0;
}
