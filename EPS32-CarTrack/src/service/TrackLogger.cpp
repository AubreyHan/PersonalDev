#include "service/TrackLogger.h"
#include <SPI.h>
#include <math.h>

static constexpr double GCJ_A = 6378245.0;
static constexpr double GCJ_EE = 0.00669342162296594323;
static constexpr double GCJ_PI = 3.1415926535897932384626;

static bool outOfChina(double lat, double lon) {
    if (lon < 72.004 || lon > 137.8347) return true;
    if (lat < 0.8293 || lat > 55.8271) return true;
    return false;
}

static double transformLat(double x, double y) {
    double ret = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y + 0.2 * sqrt(fabs(x));
    ret += (20.0 * sin(6.0 * x * GCJ_PI) + 20.0 * sin(2.0 * x * GCJ_PI)) * 2.0 / 3.0;
    ret += (20.0 * sin(y * GCJ_PI) + 40.0 * sin(y / 3.0 * GCJ_PI)) * 2.0 / 3.0;
    ret += (160.0 * sin(y / 12.0 * GCJ_PI) + 320.0 * sin(y * GCJ_PI / 30.0)) * 2.0 / 3.0;
    return ret;
}

static double transformLon(double x, double y) {
    double ret = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y + 0.1 * sqrt(fabs(x));
    ret += (20.0 * sin(6.0 * x * GCJ_PI) + 20.0 * sin(2.0 * x * GCJ_PI)) * 2.0 / 3.0;
    ret += (20.0 * sin(x * GCJ_PI) + 40.0 * sin(x / 3.0 * GCJ_PI)) * 2.0 / 3.0;
    ret += (150.0 * sin(x / 12.0 * GCJ_PI) + 300.0 * sin(x / 30.0 * GCJ_PI)) * 2.0 / 3.0;
    return ret;
}

LatLonGcj TrackLogger::wgs84ToGcj02(double wgsLat, double wgsLon) {
    if (outOfChina(wgsLat, wgsLon)) {
        return { wgsLat, wgsLon };
    }
    double dLat = transformLat(wgsLon - 105.0, wgsLat - 35.0);
    double dLon = transformLon(wgsLon - 105.0, wgsLat - 35.0);
    double radLat = wgsLat / 180.0 * GCJ_PI;
    double magic = sin(radLat);
    magic = 1.0 - GCJ_EE * magic * magic;
    double sqrtMagic = sqrt(magic);
    dLat = (dLat * 180.0) / ((GCJ_A * (1.0 - GCJ_EE)) / (magic * sqrtMagic) * GCJ_PI);
    dLon = (dLon * 180.0) / (GCJ_A / sqrtMagic * cos(radLat) * GCJ_PI);
    return { wgsLat + dLat, wgsLon + dLon };
}

bool TrackLogger::init() {
    SPI.begin(PinConfig::SD_SPI_SCK, PinConfig::SD_SPI_MISO, PinConfig::SD_SPI_MOSI, PinConfig::SD_SPI_CS);
    if (!_sd.begin(SdSpiConfig(PinConfig::SD_SPI_CS, DEDICATED_SPI, SD_SCK_MHZ(25), &SPI))) {
        Serial.println("[TrackLogger] SD Mount failed. Check card format.");
        _isMounted = false;
        SystemState::instance().lock();
        SystemState::instance().getContextUnsafe().isSdMounted = false;
        SystemState::instance().getContextUnsafe().sdFormatRequired = true;
        SystemState::instance().unlock();
        return false;
    }

    _isMounted = true;
    _sd.mkdir(StorageConfig::TRACKS_DIR);

    SystemState::instance().lock();
    SystemState::instance().getContextUnsafe().isSdMounted = true;
    SystemState::instance().getContextUnsafe().sdFormatRequired = false;
    SystemState::instance().unlock();
    Serial.println("[TrackLogger] SD Card mounted successfully (SdFat).");
    return true;
}

bool TrackLogger::formatSd() {
    if (!_sd.format(&Serial)) {
        return false;
    }
    return init();
}

void TrackLogger::writeGpxHeader(FsFile& f, const char* name, const char* desc, const char* isoTime) {
    f.println("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    f.println("<gpx version=\"1.1\" creator=\"M5Track-M9N\" ");
    f.println("     xmlns=\"http://www.topografix.com/GPX/1/1\" ");
    f.println("     xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" ");
    f.println("     xmlns:gpxtpx=\"http://www.garmin.com/xmlschemas/TrackPointExtension/v1\" ");
    f.println("     xmlns:m5=\"http://m5stack.com/xmlschemas/VehicleTrack/v1\" ");
    f.println("     xsi:schemaLocation=\"http://www.topografix.com/GPX/1/1 http://www.topografix.com/GPX/1/1/gpx.xsd\">");
    f.printf("  <metadata>\n    <name>%s</name>\n    <desc>%s</desc>\n    <time>%s</time>\n  </metadata>\n", name, desc, isoTime);
    f.println("  <trk>");
    f.println("    <name>M5Core2 Vehicle Drive</name>");
    f.println("    <trkseg>");
    f.print("    </trkseg>\n  </trk>\n</gpx>\n");
    f.sync();
}

bool TrackLogger::startNewTrackFiles() {
    if (!_isMounted) return false;

    SystemState& state = SystemState::instance();
    state.lock();
    SystemContext& ctx = state.getContextUnsafe();
    uint16_t y = ctx.gnss.year;
    uint8_t m = ctx.gnss.month;
    uint8_t d = ctx.gnss.day;
    uint8_t h = ctx.gnss.hour;
    uint8_t min = ctx.gnss.minute;
    uint8_t s = ctx.gnss.second;
    state.unlock();

    if (y < 2024) { y = 2026; m = 10; d = 28; h = 12; min = 0; s = 0; }

    snprintf(_currentWName, sizeof(_currentWName), "/tracks/W-%04d%02d%02d-%02d%02d%02d.gpx", y, m, d, h, min, s);
    snprintf(_currentCName, sizeof(_currentCName), "/tracks/C-%04d%02d%02d-%02d%02d%02d.gpx", y, m, d, h, min, s);

    char isoTime[32];
    snprintf(isoTime, sizeof(isoTime), "%04d-%02d-%02dT%02d:%02d:%02d.000Z", y, m, d, h, min, s);

    if (!_wFile.open(_currentWName, O_RDWR | O_CREAT | O_TRUNC)) return false;
    if (!_cFile.open(_currentCName, O_RDWR | O_CREAT | O_TRUNC)) {
        _wFile.close();
        return false;
    }

    writeGpxHeader(_wFile, "WGS-84 Track", "International WGS-84 Coordinate Track", isoTime);
    writeGpxHeader(_cFile, "GCJ-02 Track", "China GCJ-02 Coordinate Track", isoTime);

    _isLogging = true;
    _recordedPoints = 0;
    _closeTagsLen = strlen("    </trkseg>\n  </trk>\n</gpx>\n");

    state.lock();
    ctx.isLogging = true;
    ctx.currentGpxName = _currentWName;
    ctx.recordedPoints = 0;
    state.unlock();
    return true;
}

void TrackLogger::appendTrackPoint(const GnssData& gnss, float altM, float tempC, float pressHpa, const VehicleDynamics& dyn, uint8_t batPct) {
    if (!_isLogging) return;

    // Overwrite the closing tags at the end of file (Real-Time XML Pre-Closing)
    _wFile.seekEnd(-((int)_closeTagsLen));
    _cFile.seekEnd(-((int)_closeTagsLen));

    char timeStr[32];
    snprintf(timeStr, sizeof(timeStr), "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
             gnss.year, gnss.month, gnss.day, gnss.hour, gnss.minute, gnss.second);

    // 1. Write WGS-84 Point
    _wFile.printf("      <trkpt lat=\"%.7f\" lon=\"%.7f\">\n", gnss.latitude, gnss.longitude);
    _wFile.printf("        <ele>%.2f</ele>\n        <time>%s</time>\n", altM, timeStr);
    _wFile.printf("        <course>%.1f</course>\n        <speed>%.2f</speed>\n", gnss.headingDeg, gnss.speedKmh / 3.6f);
    _wFile.printf("        <fix>%s</fix>\n        <sat>%d</sat>\n", (gnss.fixType >= 3) ? "3d" : "none", gnss.numSats);
    _wFile.printf("        <hdop>%.2f</hdop>\n        <vdop>%.2f</vdop>\n        <pdop>%.2f</pdop>\n", gnss.hdop, gnss.vdop, gnss.pdop);
    _wFile.println("        <extensions>");
    _wFile.printf("          <gpxtpx:TrackPointExtension><gpxtpx:atemp>%.1f</gpxtpx:atemp></gpxtpx:TrackPointExtension>\n", tempC);
    _wFile.printf("          <m5:pressure_hpa>%.2f</m5:pressure_hpa>\n", pressHpa);
    _wFile.printf("          <m5:lateral_g>%.2f</m5:lateral_g>\n", dyn.lateralG);
    _wFile.printf("          <m5:long_g>%.2f</m5:long_g>\n", dyn.longG);
    _wFile.printf("          <m5:pitch_deg>%.1f</m5:pitch_deg>\n", dyn.pitchDeg);
    _wFile.printf("          <m5:roll_deg>%.1f</m5:roll_deg>\n", dyn.rollDeg);
    _wFile.printf("          <m5:road_slope_percent>%+.1f</m5:road_slope_percent>\n", dyn.roadSlopePercent);
    _wFile.printf("          <m5:battery_pct>%d</m5:battery_pct>\n", batPct);
    _wFile.println("        </extensions>");
    _wFile.println("      </trkpt>");
    _wFile.print("    </trkseg>\n  </trk>\n</gpx>\n");

    // 2. Write GCJ-02 Point
    LatLonGcj gcj = wgs84ToGcj02(gnss.latitude, gnss.longitude);
    _cFile.printf("      <trkpt lat=\"%.7f\" lon=\"%.7f\">\n", gcj.lat, gcj.lon);
    _cFile.printf("        <ele>%.2f</ele>\n        <time>%s</time>\n", altM, timeStr);
    _cFile.printf("        <course>%.1f</course>\n        <speed>%.2f</speed>\n", gnss.headingDeg, gnss.speedKmh / 3.6f);
    _cFile.printf("        <fix>%s</fix>\n        <sat>%d</sat>\n", (gnss.fixType >= 3) ? "3d" : "none", gnss.numSats);
    _cFile.printf("        <hdop>%.2f</hdop>\n        <vdop>%.2f</vdop>\n        <pdop>%.2f</pdop>\n", gnss.hdop, gnss.vdop, gnss.pdop);
    _cFile.println("        <extensions>");
    _cFile.printf("          <gpxtpx:TrackPointExtension><gpxtpx:atemp>%.1f</gpxtpx:atemp></gpxtpx:TrackPointExtension>\n", tempC);
    _cFile.printf("          <m5:pressure_hpa>%.2f</m5:pressure_hpa>\n", pressHpa);
    _cFile.printf("          <m5:lateral_g>%.2f</m5:lateral_g>\n", dyn.lateralG);
    _cFile.printf("          <m5:long_g>%.2f</m5:long_g>\n", dyn.longG);
    _cFile.printf("          <m5:pitch_deg>%.1f</m5:pitch_deg>\n", dyn.pitchDeg);
    _cFile.printf("          <m5:roll_deg>%.1f</m5:roll_deg>\n", dyn.rollDeg);
    _cFile.printf("          <m5:road_slope_percent>%+.1f</m5:road_slope_percent>\n", dyn.roadSlopePercent);
    _cFile.printf("          <m5:battery_pct>%d</m5:battery_pct>\n", batPct);
    _cFile.println("        </extensions>");
    _cFile.println("      </trkpt>");
    _cFile.print("    </trkseg>\n  </trk>\n</gpx>\n");

    _recordedPoints++;
    if (_recordedPoints % StorageConfig::SYNC_INTERVAL_POINTS == 0) {
        _wFile.sync();
        _cFile.sync();
    }

    SystemState& state = SystemState::instance();
    state.lock();
    state.getContextUnsafe().recordedPoints = _recordedPoints;
    state.unlock();
}

void TrackLogger::stopLogging() {
    if (!_isLogging) return;
    _wFile.sync();
    _wFile.close();
    _cFile.sync();
    _cFile.close();
    _isLogging = false;

    SystemState& state = SystemState::instance();
    state.lock();
    state.getContextUnsafe().isLogging = false;
    state.unlock();
}

void TrackLogger::update() {
    if (!_isMounted) return;

    unsigned long now = millis();
    if (now - _lastLogMs < 1000) return; // 1Hz logging
    _lastLogMs = now;

    SystemState& state = SystemState::instance();
    state.lock();
    SystemContext ctx = state.getContextUnsafe();
    state.unlock();

    bool canRecord = (ctx.gnss.isValid && ctx.gnss.fixType >= FIX_3D && ctx.gnss.hdop < GnssConfig::MAX_HDOP_FOR_RECORD);

    if (!_isLogging) {
        // Strict start condition: speed > 3.0 km/h AND 3D Fix (HDOP < 2.0)
        if (canRecord && ctx.gnss.speedKmh > GnssConfig::RECORD_TRIGGER_SPEED_KMH) {
            startNewTrackFiles();
        }
    } else {
        // Hourly slicing (Scheme B: 3600 points per hour)
        if (_recordedPoints >= StorageConfig::HOURLY_SPLIT_POINTS) {
            stopLogging();
            startNewTrackFiles();
        }

        // Standstill pause logic (0~15 mins pause, > 15 mins stop)
        if (ctx.gnss.speedKmh < 1.0f) {
            _standstillSeconds++;
            if (_standstillSeconds >= 900) { // 15 mins
                stopLogging();
                _standstillSeconds = 0;
                return;
            }
            // Temporarily pause adding points when stopped to avoid drift knot
            return;
        } else {
            _standstillSeconds = 0;
        }

        // Append point
        appendTrackPoint(ctx.gnss, ctx.gnss.altitudeMeters, ctx.weather.temperatureC,
                         1013.25f, ctx.dynamics, ctx.batteryPercent);
    }
}
