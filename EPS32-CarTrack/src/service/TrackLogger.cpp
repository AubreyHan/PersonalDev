#include "service/TrackLogger.h"
#include <SPI.h>
#include <math.h>

static constexpr double PI_VAL   = 3.14159265358979323846;
static constexpr double EE_VAL   = 0.00669342162296594323;
static constexpr double AXIS_VAL = 6378245.0;

static bool outOfChina(double lat, double lon) {
    if (lon < 72.004 || lon > 137.8347) return true;
    if (lat < 0.8293 || lat > 55.8271) return true;
    return false;
}

static double transformLat(double x, double y) {
    double ret = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y + 0.2 * sqrt(fabs(x));
    ret += (20.0 * sin(6.0 * x * PI_VAL) + 20.0 * sin(2.0 * x * PI_VAL)) * 2.0 / 3.0;
    ret += (20.0 * sin(y * PI_VAL) + 40.0 * sin(y / 3.0 * PI_VAL)) * 2.0 / 3.0;
    ret += (160.0 * sin(y / 12.0 * PI_VAL) + 320.0 * sin(y * PI_VAL / 30.0)) * 2.0 / 3.0;
    return ret;
}

static double transformLon(double x, double y) {
    double ret = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y + 0.1 * sqrt(fabs(x));
    ret += (20.0 * sin(6.0 * x * PI_VAL) + 20.0 * sin(2.0 * x * PI_VAL)) * 2.0 / 3.0;
    ret += (20.0 * sin(x * PI_VAL) + 40.0 * sin(x / 3.0 * PI_VAL)) * 2.0 / 3.0;
    ret += (150.0 * sin(x / 12.0 * PI_VAL) + 300.0 * sin(x / 30.0 * PI_VAL)) * 2.0 / 3.0;
    return ret;
}

void TrackLogger::wgs84ToGcj02(double wgLat, double wgLon, double& mgLat, double& mgLon) {
    if (outOfChina(wgLat, wgLon)) {
        mgLat = wgLat;
        mgLon = wgLon;
        return;
    }
    double dLat = transformLat(wgLon - 105.0, wgLat - 35.0);
    double dLon = transformLon(wgLon - 105.0, wgLat - 35.0);
    double radLat = wgLat / 180.0 * PI_VAL;
    double magic = sin(radLat);
    magic = 1.0 - EE_VAL * magic * magic;
    double sqrtMagic = sqrt(magic);
    dLat = (dLat * 180.0) / ((AXIS_VAL * (1.0 - EE_VAL)) / (magic * sqrtMagic) * PI_VAL);
    dLon = (dLon * 180.0) / (AXIS_VAL / sqrtMagic * cos(radLat) * PI_VAL);
    mgLat = wgLat + dLat;
    mgLon = wgLon + dLon;
}

bool TrackLogger::init() {
    SPI.begin(PinConfig::SD_SPI_SCK, PinConfig::SD_SPI_MISO, PinConfig::SD_SPI_MOSI, PinConfig::SD_SPI_CS);
    SdSpiConfig spiCfg(PinConfig::SD_SPI_CS, SHARED_SPI, SD_SCK_MHZ(20));

    _mounted = _sd.begin(spiCfg);
    if (_mounted) {
        if (!_sd.exists(StorageConfig::TRACKS_DIR)) {
            _sd.mkdir(StorageConfig::TRACKS_DIR);
        }
        healUnclosedGpxFiles();
        enforceStorageQuota50Percent();
        refreshStorageStats();
    } else {
        Serial.println("[SD] TF Card not mounted or absent (Graceful Standby).");
        SystemState& state = SystemState::instance();
        state.lock();
        state.getContextUnsafe().storage.isMounted   = false;
        state.getContextUnsafe().storage.needsFormat = false;
        state.unlock();
    }
    return _mounted;
}

bool TrackLogger::formatFat32() {
    closeActiveStreams();
    SdSpiConfig spiCfg(PinConfig::SD_SPI_CS, SHARED_SPI, SD_SCK_MHZ(20));
    if (!_sd.cardBegin(spiCfg)) {
        return false;
    }
    FatFormatter formatter;
    uint8_t sectorBuf[512];
    bool ok = formatter.format(_sd.card(), sectorBuf, &Serial);
    if (ok && _sd.begin(spiCfg)) {
        _mounted = true;
        _sd.mkdir(StorageConfig::TRACKS_DIR);
        refreshStorageStats();
        return true;
    }
    return false;
}

void TrackLogger::healUnclosedGpxFiles() {
    if (!_mounted) return;
    FsFile dir;
    if (!dir.open(StorageConfig::TRACKS_DIR)) return;

    FsFile entry;
    char name[48];
    const char* footer = "</trkseg>\n  </trk>\n</gpx>\n";

    while (entry.openNext(&dir, O_RDWR)) {
        if (!entry.isDir()) {
            entry.getName(name, sizeof(name));
            uint64_t sz = entry.fileSize();
            if (sz > 128 && strstr(name, ".gpx") != nullptr) {
                entry.seekSet(sz - 16);
                char tail[17] = {0};
                entry.read(tail, 16);
                if (strstr(tail, "</gpx>") == nullptr) {
                    entry.seekEnd();
                    entry.write(footer, strlen(footer));
                    entry.sync();
                    Serial.printf("[SD] Self-healed unclosed GPX file: %s\n", name);
                }
            }
        }
        entry.close();
    }
    dir.close();
}

void TrackLogger::enforceStorageQuota50Percent() {
    if (!_mounted) return;
    uint64_t totalBytes = (uint64_t)_sd.clusterCount() * _sd.bytesPerCluster();
    if (totalBytes == 0) return;

    uint64_t maxTracksBytes = (uint64_t)(totalBytes * MotionPowerConfig::TRACK_STORAGE_QUOTA_RATIO);
    uint64_t targetBytes    = (uint64_t)(totalBytes * MotionPowerConfig::TRACK_RECOVERY_QUOTA_RATIO);

    FsFile dir;
    if (!dir.open(StorageConfig::TRACKS_DIR)) return;

    uint64_t tracksUsed = 0;
    FsFile entry;
    while (entry.openNext(&dir, O_RDONLY)) {
        if (!entry.isDir()) {
            tracksUsed += entry.fileSize();
        }
        entry.close();
    }
    dir.close();

    // If /tracks/ exceeds 50% of total card capacity, delete oldest W/C pairs until < 40%
    int guard = 0;
    while (tracksUsed > maxTracksBytes && guard++ < 64) {
        if (!dir.open(StorageConfig::TRACKS_DIR)) break;
        char oldestWgs[48] = {0};
        while (entry.openNext(&dir, O_RDONLY)) {
            char fname[48] = {0};
            entry.getName(fname, sizeof(fname));
            if (fname[0] == 'W' && strstr(fname, ".gpx")) {
                if (oldestWgs[0] == '\0' || strcmp(fname, oldestWgs) < 0) {
                    strncpy(oldestWgs, fname, sizeof(oldestWgs) - 1);
                }
            }
            entry.close();
        }
        dir.close();

        if (oldestWgs[0] == '\0') break;

        char wPath[64], cPath[64];
        snprintf(wPath, sizeof(wPath), "%s/%s", StorageConfig::TRACKS_DIR, oldestWgs);
        oldestWgs[0] = 'C';
        snprintf(cPath, sizeof(cPath), "%s/%s", StorageConfig::TRACKS_DIR, oldestWgs);

        FsFile f;
        if (f.open(wPath, O_RDONLY)) { tracksUsed -= f.fileSize(); f.close(); _sd.remove(wPath); }
        if (f.open(cPath, O_RDONLY)) { tracksUsed -= f.fileSize(); f.close(); _sd.remove(cPath); }

        if (tracksUsed <= targetBytes) break;
    }
}

void TrackLogger::refreshStorageStats() {
    if (!_mounted) return;

    uint64_t totalBytes = (uint64_t)_sd.clusterCount() * _sd.bytesPerCluster();
    uint64_t freeBytes  = (uint64_t)_sd.freeClusterCount() * _sd.bytesPerCluster();
    uint64_t usedBytes  = (totalBytes >= freeBytes) ? (totalBytes - freeBytes) : 0;

    uint64_t tracksBytes = 0;
    uint32_t wgsCount    = 0;

    FsFile dir;
    if (dir.open(StorageConfig::TRACKS_DIR)) {
        FsFile entry;
        char fname[48];
        while (entry.openNext(&dir, O_RDONLY)) {
            if (!entry.isDir()) {
                tracksBytes += entry.fileSize();
                entry.getName(fname, sizeof(fname));
                if (fname[0] == 'W' && strstr(fname, ".gpx")) {
                    wgsCount++;
                }
            }
            entry.close();
        }
        dir.close();
    }

    SystemState& state = SystemState::instance();
    state.lock();
    auto& st = state.getContextUnsafe().storage;
    st.isMounted           = _mounted;
    st.isRecording         = _recording;
    st.totalBytes          = totalBytes;
    st.usedBytes           = usedBytes;
    st.tracksDirBytes      = tracksBytes;
    st.trackFilePairsCount = wgsCount;
    st.currentSegmentPoints= _pointsInFile;
    state.unlock();
}

bool TrackLogger::openNewTrackPair(const SystemContext& snap) {
    if (!_mounted) return false;
    closeActiveStreams();
    enforceStorageQuota50Percent();

    uint16_t y = (snap.gnss.year >= 2024) ? snap.gnss.year : 2026;
    uint8_t  m = (snap.gnss.month >= 1)   ? snap.gnss.month : 10;
    uint8_t  d = (snap.gnss.day >= 1)     ? snap.gnss.day : 30;
    uint8_t  hh = snap.gnss.hour;
    uint8_t  mm = snap.gnss.minute;
    uint8_t  ss = snap.gnss.second;

    char wName[40], cName[40];
    snprintf(wName, sizeof(wName), "W-%04u%02u%02u-%02u%02u%02u.gpx", y, m, d, hh, mm, ss);
    snprintf(cName, sizeof(cName), "C-%04u%02u%02u-%02u%02u%02u.gpx", y, m, d, hh, mm, ss);

    char wPath[64], cPath[64];
    snprintf(wPath, sizeof(wPath), "%s/%s", StorageConfig::TRACKS_DIR, wName);
    snprintf(cPath, sizeof(cPath), "%s/%s", StorageConfig::TRACKS_DIR, cName);

    if (!_fileWgs.open(wPath, O_RDWR | O_CREAT | O_TRUNC) ||
        !_fileGcj.open(cPath, O_RDWR | O_CREAT | O_TRUNC)) {
        _fileWgs.close();
        _fileGcj.close();
        return false;
    }

    char header[640];
    snprintf(header, sizeof(header),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<gpx version=\"1.1\" creator=\"M5Track-Tab5\"\n"
        "     xmlns=\"http://www.topografix.com/GPX/1/1\"\n"
        "     xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n"
        "     xmlns:gpxtpx=\"http://www.garmin.com/xmlschemas/TrackPointExtension/v1\"\n"
        "     xmlns:m5=\"http://m5stack.com/xmlschemas/VehicleTrack/v1\">\n"
        "  <metadata>\n"
        "    <name>Track %04u-%02u-%02u %02u:%02u:%02u</name>\n"
        "    <time>%04u-%02u-%02uT%02u:%02u:%02u.000Z</time>\n"
        "  </metadata>\n"
        "  <trk>\n"
        "    <name>M5Tab5 Vehicle Drive</name>\n"
        "    <trkseg>\n"
        "    </trkseg>\n"
        "  </trk>\n"
        "</gpx>\n",
        y, m, d, hh, mm, ss, y, m, d, hh, mm, ss);

    _fileWgs.write(header, strlen(header));
    _fileGcj.write(header, strlen(header));
    _fileWgs.sync();
    _fileGcj.sync();

    _recording       = true;
    _pointsInFile    = 0;
    _pointsSinceSync = 0;

    SystemState& state = SystemState::instance();
    state.lock();
    auto& st = state.getContextUnsafe().storage;
    st.isRecording = true;
    strncpy(st.currentWgsFile, wName, sizeof(st.currentWgsFile) - 1);
    strncpy(st.currentGcjFile, cName, sizeof(st.currentGcjFile) - 1);
    state.unlock();
    return true;
}

void TrackLogger::appendTrackPoint(FsFile& file, double lat, double lon, const SystemContext& snap) {
    if (!file.isOpen()) return;
    const char* footer = "    </trkseg>\n  </trk>\n</gpx>\n";
    uint64_t sz = file.fileSize();
    if (sz > strlen(footer)) {
        file.seekSet(sz - strlen(footer));
    }

    char pt[640];
    snprintf(pt, sizeof(pt),
        "      <trkpt lat=\"%.7f\" lon=\"%.7f\">\n"
        "        <ele>%.2f</ele>\n"
        "        <time>%04u-%02u-%02uT%02u:%02u:%02u.%03uZ</time>\n"
        "        <course>%.1f</course>\n"
        "        <speed>%.2f</speed>\n"
        "        <fix>3d</fix>\n"
        "        <sat>%u</sat>\n"
        "        <hdop>%.2f</hdop>\n"
        "        <vdop>%.2f</vdop>\n"
        "        <pdop>%.2f</pdop>\n"
        "        <extensions>\n"
        "          <gpxtpx:TrackPointExtension><gpxtpx:atemp>%.1f</gpxtpx:atemp></gpxtpx:TrackPointExtension>\n"
        "          <m5:pressure_hpa>%.2f</m5:pressure_hpa>\n"
        "          <m5:sats_bds>%u</m5:sats_bds>\n"
        "          <m5:sats_gps>%u</m5:sats_gps>\n"
        "          <m5:sats_glo>%u</m5:sats_glo>\n"
        "          <m5:sats_gal>%u</m5:sats_gal>\n"
        "          <m5:lateral_g>%.2f</m5:lateral_g>\n"
        "          <m5:long_g>%.2f</m5:long_g>\n"
        "          <m5:pitch_deg>%.1f</m5:pitch_deg>\n"
        "          <m5:roll_deg>%.1f</m5:roll_deg>\n"
        "          <m5:road_slope_percent>%+.1f</m5:road_slope_percent>\n"
        "          <m5:is_tunnel>%s</m5:is_tunnel>\n"
        "          <m5:battery_pct>%d</m5:battery_pct>\n"
        "        </extensions>\n"
        "      </trkpt>\n"
        "%s",
        lat, lon,
        snap.baro.fusedAltitudeM,
        snap.gnss.year >= 2024 ? snap.gnss.year : 2026,
        snap.gnss.month >= 1   ? snap.gnss.month : 10,
        snap.gnss.day >= 1     ? snap.gnss.day : 30,
        snap.gnss.hour, snap.gnss.minute, snap.gnss.second, snap.gnss.millisecond,
        snap.imu.fusedHeadingDeg,
        snap.gnss.speedKmh / 3.6f,
        snap.gnss.numSats,
        snap.gnss.hdop, snap.gnss.vdop, snap.gnss.pdop,
        snap.baro.temperatureC,
        snap.baro.pressureHpa,
        snap.gnss.numBdsSats, snap.gnss.numGpsSats, snap.gnss.numGloSats, snap.gnss.numGalSats,
        snap.imu.lateralG, snap.imu.longitudinalG,
        snap.imu.pitchDeg, snap.imu.rollDeg,
        snap.trip.roadSlopePercent,
        snap.gnss.isInTunnel ? "true" : "false",
        (int)roundf(snap.power.batterySocPercent),
        footer);

    file.write(pt, strlen(pt));
}

void TrackLogger::writeNewSegmentTag() {
    if (!_recording) return;
    const char* footer = "    </trkseg>\n  </trk>\n</gpx>\n";
    const char* segTag = "    </trkseg>\n    <trkseg>\n    </trkseg>\n  </trk>\n</gpx>\n";

    if (_fileWgs.isOpen() && _fileWgs.fileSize() > strlen(footer)) {
        _fileWgs.seekSet(_fileWgs.fileSize() - strlen(footer));
        _fileWgs.write(segTag, strlen(segTag));
        _fileWgs.sync();
    }
    if (_fileGcj.isOpen() && _fileGcj.fileSize() > strlen(footer)) {
        _fileGcj.seekSet(_fileGcj.fileSize() - strlen(footer));
        _fileGcj.write(segTag, strlen(segTag));
        _fileGcj.sync();
    }
}

void TrackLogger::closeActiveStreams() {
    if (_fileWgs.isOpen()) {
        _fileWgs.sync();
        _fileWgs.close();
    }
    if (_fileGcj.isOpen()) {
        _fileGcj.sync();
        _fileGcj.close();
    }
    _recording = false;
    SystemState& state = SystemState::instance();
    state.lock();
    state.getContextUnsafe().storage.isRecording = false;
    state.unlock();
}

void TrackLogger::update() {
    if (!_mounted) return;

    uint32_t curSec = millis() / 1000;
    if (curSec == _lastLogSec) return;
    _lastLogSec = curSec;

    SystemState& state = SystemState::instance();
    state.lock();
    bool reqNewSeg = state.getContextUnsafe().storage.requestNewSegment;
    if (reqNewSeg) {
        state.getContextUnsafe().storage.requestNewSegment = false;
    }
    state.unlock();

    if (reqNewSeg && _recording) {
        writeNewSegmentTag();
    }

    SystemContext snap = state.getSnapshot();
    bool canRecord = (snap.gnss.isValid || snap.gnss.isDeadReckoning) &&
                     (snap.gnss.fixType >= FIX_3D || snap.gnss.isDeadReckoning) &&
                     (snap.gnss.speedKmh >= MotionPowerConfig::RECORD_TRIGGER_SPEED_KMH);

    if (!canRecord) {
        return;
    }

    if (!_recording || _pointsInFile >= MotionPowerConfig::HOURLY_SPLIT_POINTS) {
        if (!openNewTrackPair(snap)) {
            return;
        }
    }

    double wLat = snap.gnss.latitude;
    double wLon = snap.gnss.longitude;
    double cLat = wLat, cLon = wLon;
    wgs84ToGcj02(wLat, wLon, cLat, cLon);

    appendTrackPoint(_fileWgs, wLat, wLon, snap);
    appendTrackPoint(_fileGcj, cLat, cLon, snap);

    _pointsInFile++;
    _pointsSinceSync++;

    if (_pointsSinceSync >= StorageConfig::SYNC_INTERVAL_POINTS) {
        _pointsSinceSync = 0;
        _fileWgs.sync();
        _fileGcj.sync();
        refreshStorageStats();
    }
}

size_t TrackLogger::listTrackFiles(TrackFileEntry* outList, size_t maxEntries) {
    if (!_mounted || !outList || maxEntries == 0) return 0;

    FsFile dir;
    if (!dir.open(StorageConfig::TRACKS_DIR)) return 0;

    size_t count = 0;
    FsFile entry;
    while (count < maxEntries && entry.openNext(&dir, O_RDONLY)) {
        if (!entry.isDir()) {
            char fname[44] = {0};
            entry.getName(fname, sizeof(fname));
            if (strstr(fname, ".gpx")) {
                strncpy(outList[count].name, fname, sizeof(outList[count].name) - 1);
                outList[count].sizeBytes = (uint32_t)entry.fileSize();
                outList[count].isGcj02   = (fname[0] == 'C');
                count++;
            }
        }
        entry.close();
    }
    dir.close();
    return count;
}

bool TrackLogger::openFileForRead(const char* filename, FsFile& outFile) {
    if (!_mounted || !filename) return false;
    char path[72];
    snprintf(path, sizeof(path), "%s/%s", StorageConfig::TRACKS_DIR, filename);
    return outFile.open(path, O_RDONLY);
}
