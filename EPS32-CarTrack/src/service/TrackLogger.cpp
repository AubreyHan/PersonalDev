#include "TrackLogger.h"
#include "../driver/RtcDriver.h"
#include <Preferences.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {
    constexpr const char* GPX_CLOSING_TAIL = "    </trkseg>\n  </trk>\n</gpx>\n";
    constexpr size_t      GPX_CLOSING_TAIL_LEN = 28U; // strlen("    </trkseg>\n  </trk>\n</gpx>\n")

    constexpr double KRASOVSKY_A  = 6378245.0;
    constexpr double KRASOVSKY_EE = 0.00669342162296594323;
    constexpr double PI_VAL       = 3.14159265358979323846;

    double transformLat(double x, double y) {
        double ret = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y
                   + 0.1 * x * y + 0.2 * sqrt(fabs(x));
        ret += (20.0 * sin(6.0 * x * PI_VAL) + 20.0 * sin(2.0 * x * PI_VAL)) * 2.0 / 3.0;
        ret += (20.0 * sin(y * PI_VAL) + 40.0 * sin(y / 3.0 * PI_VAL)) * 2.0 / 3.0;
        ret += (160.0 * sin(y / 12.0 * PI_VAL) + 320.0 * sin(y * PI_VAL / 30.0)) * 2.0 / 3.0;
        return ret;
    }

    double transformLon(double x, double y) {
        double ret = 300.0 + x + 2.0 * y + 0.1 * x * x
                   + 0.1 * x * y + 0.1 * sqrt(fabs(x));
        ret += (20.0 * sin(6.0 * x * PI_VAL) + 20.0 * sin(2.0 * x * PI_VAL)) * 2.0 / 3.0;
        ret += (20.0 * sin(x * PI_VAL) + 40.0 * sin(x / 3.0 * PI_VAL)) * 2.0 / 3.0;
        ret += (150.0 * sin(x / 12.0 * PI_VAL) + 300.0 * sin(x / 30.0 * PI_VAL)) * 2.0 / 3.0;
        return ret;
    }

    void formatLocalTimestamp(uint32_t epochUtc, int16_t tzOffsetMin, char* outBuf, size_t outSize) {
        if (epochUtc == 0UL) {
            snprintf(outBuf, outSize, "2026-01-01 00:00:00");
            return;
        }
        const int64_t shifted = static_cast<int64_t>(epochUtc) + static_cast<int64_t>(tzOffsetMin) * 60LL;
        const uint32_t localSec = (shifted > 0) ? static_cast<uint32_t>(shifted) : 0UL;
        RtcDateTime dt{};
        RtcDriver::fromUnixEpoch(localSec, dt);
        snprintf(outBuf, outSize, "%04u-%02u-%02u %02u:%02u:%02u",
                 dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    }

    void formatDurationHms(uint32_t totalSec, char* outBuf, size_t outSize) {
        const uint32_t hh = totalSec / 3600UL;
        const uint32_t mm = (totalSec % 3600UL) / 60UL;
        const uint32_t ss = totalSec % 60UL;
        snprintf(outBuf, outSize, "%02lu:%02lu:%02lu",
                 static_cast<unsigned long>(hh),
                 static_cast<unsigned long>(mm),
                 static_cast<unsigned long>(ss));
    }

    bool endsWithIgnoreCase(const char* str, const char* suffix) {
        if (!str || !suffix) {
            return false;
        }
        const size_t lenS = strlen(str);
        const size_t lenX = strlen(suffix);
        if (lenX > lenS) {
            return false;
        }
        return strcasecmp(str + (lenS - lenX), suffix) == 0;
    }
} // namespace

TrackLogger& TrackLogger::instance() {
    static TrackLogger s_instance;
    return s_instance;
}

TrackLogger::TrackLogger()
    : _sdMutex(xSemaphoreCreateRecursiveMutex()),
      _sdSpi(FSPI) {}

void TrackLogger::fatDateTimeCallback(uint16_t* date, uint16_t* time) {
    uint16_t y  = 2026;
    uint8_t  mo = 1, d = 1, h = 0, mi = 0, s = 0;
    {
        StateGuard guard(SystemState::instance(), pdMS_TO_TICKS(5));
        if (guard.locked()) {
            const TimeData& t = SystemState::instance().data().time;
            if (t.localYear >= 1980U && t.localYear <= 2107U) {
                y  = t.localYear;
                mo = t.localMonth;
                d  = t.localDay;
                h  = t.localHour;
                mi = t.localMinute;
                s  = t.localSecond;
            }
        }
    }
    *date = FS_DATE(y, mo, d);
    *time = FS_TIME(h, mi, s);
}

bool TrackLogger::isInsideChina(double lat, double lon) {
    return (lon >= 72.004 && lon <= 137.8347 && lat >= 0.8293 && lat <= 55.8271);
}

void TrackLogger::wgs84ToGcj02(double wgsLat, double wgsLon, double& outGcjLat, double& outGcjLon) {
    if (!isInsideChina(wgsLat, wgsLon)) {
        outGcjLat = wgsLat;
        outGcjLon = wgsLon;
        return;
    }
    double dLat = transformLat(wgsLon - 105.0, wgsLat - 35.0);
    double dLon = transformLon(wgsLon - 105.0, wgsLat - 35.0);
    const double radLat = wgsLat / 180.0 * PI_VAL;
    double magic = sin(radLat);
    magic = 1.0 - KRASOVSKY_EE * magic * magic;
    const double sqrtMagic = sqrt(magic);
    dLat = (dLat * 180.0) / ((KRASOVSKY_A * (1.0 - KRASOVSKY_EE)) / (magic * sqrtMagic) * PI_VAL);
    dLon = (dLon * 180.0) / (KRASOVSKY_A / sqrtMagic * cos(radLat) * PI_VAL);
    outGcjLat = wgsLat + dLat;
    outGcjLon = wgsLon + dLon;
}

void TrackLogger::loadNvsState() {
    Preferences prefs;
    if (!prefs.begin(NvsConfig::NAMESPACE, true)) {
        return;
    }
    const bool     autoRec   = prefs.getBool(NvsConfig::KEY_AUTO_RECORD, true);
    const bool     manAct    = prefs.getBool(NvsConfig::KEY_MAN_TRIP_ACT, false);
    const uint16_t subIdx    = prefs.getUShort(NvsConfig::KEY_SUB_IDX, 0U);
    const uint16_t subSplit  = prefs.getUShort(NvsConfig::KEY_SUB_SPLIT_IDX, 1U);
    const uint32_t lastDay   = prefs.getUInt(NvsConfig::KEY_LAST_DAY_INT, 0UL);
    const uint8_t  viewCat   = prefs.getUChar(NvsConfig::KEY_ACT_TRIP_VIEW, 0U);
    prefs.end();

    _manualTripActive = manAct;
    _subIndex         = subIdx;
    _subVolumeIndex   = (subSplit >= 1U) ? subSplit : 1U;
    _currentDailyYmd  = lastDay;

    StateGuard guard(SystemState::instance());
    SystemStateSnapshot& st = SystemState::instance().data();
    st.storage.autoRecordEnabled     = autoRec;
    st.settings.autoRecordOnMove     = autoRec;
    st.storage.subIndex              = _subIndex;
    st.storage.subVolumeIndex        = _subVolumeIndex;
    st.tripControl.manualTripActive  = _manualTripActive;
    st.tripControl.manualSubIndex    = _subIndex;
    st.tripControl.manualSplitIndex  = _subVolumeIndex;
    st.tripControl.lastLocalDayYmd   = _currentDailyYmd;
    if (viewCat < TRIP_CATEGORY_COUNT) {
        if (viewCat == TRIP_MANUAL && !_manualTripActive) {
            st.tripControl.activeViewCategory = TRIP_SINGLE;
        } else {
            st.tripControl.activeViewCategory = static_cast<TripCategory>(viewCat);
        }
    }
    _seenResetVersion = st.tripControl.tripResetVersion;
}

void TrackLogger::saveSubTripNvsState(bool active, uint16_t subIdx, uint16_t splitIdx) {
    Preferences prefs;
    if (!prefs.begin(NvsConfig::NAMESPACE, false)) {
        return;
    }
    prefs.putBool(NvsConfig::KEY_MAN_TRIP_ACT, active);
    prefs.putUShort(NvsConfig::KEY_SUB_IDX, subIdx);
    prefs.putUShort(NvsConfig::KEY_SUB_SPLIT_IDX, splitIdx);
    prefs.end();
}

void TrackLogger::saveDailyNvsState(uint32_t ymd, float dayDistKm) {
    Preferences prefs;
    if (!prefs.begin(NvsConfig::NAMESPACE, false)) {
        return;
    }
    prefs.putUInt(NvsConfig::KEY_LAST_DAY_INT, ymd);
    prefs.putFloat(NvsConfig::KEY_DAY_DIST_KM, dayDistKm);
    prefs.end();
}

bool TrackLogger::mountCardLocked() {
    // Dedicated SPI setup per Board #3 §1.4:
    // Pull up unused SDMMC DAT1/DAT2 lines to prevent floating current/noise
    pinMode(SdCardConfig::PIN_DAT1_PULLUP, INPUT_PULLUP);
    pinMode(SdCardConfig::PIN_DAT2_PULLUP, INPUT_PULLUP);
    _sdSpi.begin(SdCardConfig::PIN_SCK,
                 SdCardConfig::PIN_MISO,
                 SdCardConfig::PIN_MOSI,
                 SdCardConfig::PIN_CS);

    FsDateTime::setCallback(&TrackLogger::fatDateTimeCallback);

    SdSpiConfig primaryCfg(SdCardConfig::PIN_CS,
                           DEDICATED_SPI,
                           SD_SCK_MHZ(SdCardConfig::SPI_FREQ_MHZ),
                           &_sdSpi);
    bool cardOk = _sd.cardBegin(primaryCfg);
    if (!cardOk) {
        SdSpiConfig fallbackCfg(SdCardConfig::PIN_CS,
                                DEDICATED_SPI,
                                SD_SCK_MHZ(SdCardConfig::SPI_FALLBACK_MHZ),
                                &_sdSpi);
        cardOk = _sd.cardBegin(fallbackCfg);
    }

    _cardPresent    = cardOk && (_sd.card() != nullptr) && (_sd.sdErrorCode() == 0);
    _mounted        = false;
    _formatIncompat = false;
    _fsType         = STORAGE_FS_NONE;

    if (!_cardPresent) {
        return false;
    }

    const bool volOk = _sd.volumeBegin();
    if (!volOk) {
        _fsType         = STORAGE_FS_UNSUPPORTED;
        _formatIncompat = true;
        return false;
    }

    const uint8_t ft = _sd.fatType();
    if (ft == FAT_TYPE_FAT32) {
        _fsType         = STORAGE_FS_FAT32;
        _mounted        = true;
        _formatIncompat = false;
        return true;
    } else if (ft == FAT_TYPE_EXFAT) {
        _fsType         = STORAGE_FS_EXFAT;
        _mounted        = false;
        _formatIncompat = true;
        return false;
    } else if (ft == FAT_TYPE_FAT16) {
        // Allow FAT16 only on tiny <=2GB cards where FAT32 is illegal per FAT spec
        const uint64_t sectors = (_sd.card() != nullptr) ? _sd.card()->sectorCount() : 0ULL;
        if (sectors > 0ULL && sectors < 0x400000ULL) {
            _fsType         = STORAGE_FS_FAT16;
            _mounted        = true;
            _formatIncompat = false;
            return true;
        }
        _fsType         = STORAGE_FS_FAT16;
        _mounted        = false;
        _formatIncompat = true;
        return false;
    } else {
        _fsType         = STORAGE_FS_UNSUPPORTED;
        _mounted        = false;
        _formatIncompat = true;
        return false;
    }
}

bool TrackLogger::ensureDirectoriesAndLedgerLocked() {
    if (!_mounted) {
        return false;
    }
    if (!_sd.exists(SdCardConfig::DIR_TRACKS_ROOT)) {
        _sd.mkdir(SdCardConfig::DIR_TRACKS_ROOT, true);
    }
    if (!_sd.exists(SdCardConfig::DIR_SINGLE)) {
        _sd.mkdir(SdCardConfig::DIR_SINGLE, true);
    }
    if (!_sd.exists(SdCardConfig::DIR_DAILY)) {
        _sd.mkdir(SdCardConfig::DIR_DAILY, true);
    }
    if (!_sd.exists(SdCardConfig::DIR_SUB)) {
        _sd.mkdir(SdCardConfig::DIR_SUB, true);
    }

    if (!_sd.exists(SdCardConfig::FILE_TRIPS_CSV)) {
        FsFile csv = _sd.open(SdCardConfig::FILE_TRIPS_CSV, O_WRONLY | O_CREAT | O_TRUNC);
        if (csv.isOpen()) {
            csv.write(SdCardConfig::TRIPS_CSV_HEADER, strlen(SdCardConfig::TRIPS_CSV_HEADER));
            csv.write("\n", 1);
            csv.sync();
            csv.close();
        }
    }
    return true;
}

bool TrackLogger::healSingleGpxFileLocked(const char* fullPath) {
    FsFile file = _sd.open(fullPath, O_RDWR);
    if (!file.isOpen()) {
        return false;
    }
    const uint64_t fileLen = file.fileSize();
    if (fileLen < 32ULL) {
        file.close();
        return false;
    }

    const size_t tailReadLen = (fileLen > 320ULL) ? 320U : static_cast<size_t>(fileLen);
    const uint64_t tailOffset = fileLen - tailReadLen;
    char tailBuf[324] = {};
    if (!file.seekSet(tailOffset)) {
        file.close();
        return false;
    }
    const int nRead = file.read(tailBuf, tailReadLen);
    if (nRead <= 0) {
        file.close();
        return false;
    }
    tailBuf[nRead] = '\0';

    if (strstr(tailBuf, "</gpx>") != nullptr) {
        file.close();
        return false; // Already cleanly closed
    }

    // Scan backwards inside last 2KB to find last complete </trkpt>, <trkseg>, or <trk>
    const size_t scanLen = (fileLen > 2048ULL) ? 2048U : static_cast<size_t>(fileLen);
    const uint64_t scanOffset = fileLen - scanLen;
    char* scanBuf = static_cast<char*>(malloc(scanLen + 1U));
    if (scanBuf == nullptr) {
        file.seekEnd();
        file.write("\n", 1);
        file.write(GPX_CLOSING_TAIL, strlen(GPX_CLOSING_TAIL));
        file.sync();
        file.close();
        return true;
    }

    uint64_t validCutPos = fileLen;
    bool needTrksegClose = true;
    if (file.seekSet(scanOffset)) {
        const int sRead = file.read(scanBuf, scanLen);
        if (sRead > 0) {
            scanBuf[sRead] = '\0';
            const char* lastTrkptEnd = nullptr;
            const char* p = scanBuf;
            while ((p = strstr(p, "</trkpt>")) != nullptr) {
                lastTrkptEnd = p;
                p += 8;
            }
            if (lastTrkptEnd != nullptr) {
                validCutPos = scanOffset + static_cast<uint64_t>((lastTrkptEnd - scanBuf) + 8);
                needTrksegClose = true;
            } else {
                const char* lastSegEnd = strstr(scanBuf, "</trkseg>");
                if (lastSegEnd != nullptr) {
                    validCutPos = scanOffset + static_cast<uint64_t>((lastSegEnd - scanBuf) + 9);
                    needTrksegClose = false;
                } else {
                    const char* segStart = strstr(scanBuf, "<trkseg>");
                    if (segStart != nullptr) {
                        validCutPos = scanOffset + static_cast<uint64_t>((segStart - scanBuf) + 8);
                        needTrksegClose = true;
                    }
                }
            }
        }
    }
    free(scanBuf);

    file.truncate(validCutPos);
    file.seekSet(validCutPos);
    file.write("\n", 1);
    if (needTrksegClose) {
        file.write(GPX_CLOSING_TAIL, strlen(GPX_CLOSING_TAIL));
    } else {
        const char* trkClose = "  </trk>\n</gpx>\n";
        file.write(trkClose, strlen(trkClose));
    }
    file.sync();
    file.close();
    return true;
}

uint16_t TrackLogger::healUnclosedGpxFilesLocked() {
    if (!_mounted) {
        return 0U;
    }
    static constexpr const char* kDirs[3] = {
        SdCardConfig::DIR_SINGLE,
        SdCardConfig::DIR_DAILY,
        SdCardConfig::DIR_SUB
    };
    uint16_t healed = 0;
    for (size_t i = 0; i < 3; ++i) {
        FsFile dir = _sd.open(kDirs[i], O_RDONLY);
        if (!dir.isOpen() || !dir.isDirectory()) {
            continue;
        }
        FsFile entry;
        char nameBuf[64] = {};
        char fullPath[112] = {};
        while (entry.openNext(&dir, O_RDONLY)) {
            const bool isSubDir = entry.isDirectory();
            entry.getName(nameBuf, sizeof(nameBuf));
            entry.close();
            if (isSubDir || !endsWithIgnoreCase(nameBuf, ".gpx")) {
                continue;
            }
            snprintf(fullPath, sizeof(fullPath), "%s/%s", kDirs[i], nameBuf);
            if (healSingleGpxFileLocked(fullPath)) {
                healed++;
            }
        }
        dir.close();
    }
    return healed;
}

bool TrackLogger::begin() {
    loadNvsState();

    uint16_t healedCount = 0;
    {
        SdGuard sdLock(_sdMutex);
        if (mountCardLocked()) {
            ensureDirectoriesAndLedgerLocked();
            healedCount = healUnclosedGpxFilesLocked();
            enforceStorageQuota50PercentLocked();
        }
    }

    {
        StateGuard guard(SystemState::instance());
        SystemStateSnapshot& st = SystemState::instance().data();
        st.boot.tfCardMounted            = _mounted;
        st.boot.tfCardFormatIncompatible = _formatIncompat;
        st.storage.isMounted             = _mounted;
        st.storage.formatIncompatible    = _formatIncompat;
        st.storage.fsType                = _fsType;
        st.storage.healedFilesOnBoot     = healedCount;
    }

    if (_mounted) {
        refreshStorageStats();
        char bootNote[64];
        snprintf(bootNote, sizeof(bootNote), "FAT32 mounted, healed=%u", healedCount);
        appendPowerLog("BOOT", bootNote);
    }
    return _mounted;
}

bool TrackLogger::formatToFat32() {
    {
        StateGuard guard(SystemState::instance());
        SystemState::instance().data().storage.isFormatting = true;
        SystemState::instance().data().storage.lastFormatOk = false;
    }

    bool fmtOk = false;
    {
        SdGuard sdLock(_sdMutex);
        flushAndCloseAllStreams(false);

        if (!_cardPresent || _sd.card() == nullptr) {
            mountCardLocked();
        }

        if (_sd.card() != nullptr) {
            FatFormatter fatFmt;
            uint8_t* secBuffer = _sd.FsVolume::end();
            const bool switchSpi = _sd.hasDedicatedSpi() && !_sd.isDedicatedSpi();
            bool spiOk = true;
            if (switchSpi) {
                spiOk = _sd.setDedicatedSpi(true);
            }
            if (spiOk && secBuffer != nullptr) {
                fmtOk = fatFmt.format(_sd.card(), secBuffer, nullptr);
            }
            if (switchSpi) {
                _sd.setDedicatedSpi(false);
            }
            if (fmtOk && _sd.volumeBegin()) {
                const uint8_t ft = _sd.fatType();
                _fsType         = (ft == FAT_TYPE_FAT32) ? STORAGE_FS_FAT32 : STORAGE_FS_FAT16;
                _mounted        = (ft == FAT_TYPE_FAT32 || ft == FAT_TYPE_FAT16);
                _formatIncompat = !_mounted;
                if (_mounted) {
                    ensureDirectoriesAndLedgerLocked();
                } else {
                    fmtOk = false;
                }
            } else {
                fmtOk           = false;
                _mounted        = false;
                _formatIncompat = true;
            }
        }
    }

    {
        StateGuard guard(SystemState::instance());
        SystemStateSnapshot& st = SystemState::instance().data();
        st.boot.tfCardMounted            = _mounted;
        st.boot.tfCardFormatIncompatible = _formatIncompat;
        st.storage.isMounted             = _mounted;
        st.storage.formatIncompatible    = _formatIncompat;
        st.storage.fsType                = _fsType;
        st.storage.isFormatting          = false;
        st.storage.lastFormatOk          = fmtOk;
        st.storage.cmdFormatFat32        = false;
    }

    if (fmtOk) {
        refreshStorageStats();
        appendPowerLog("FORMAT_FAT32", "In-device FAT32 format & directory tree initialized");
    }
    return fmtOk;
}

bool TrackLogger::openNewDualStreamLocked(DualGpxStream& stream,
                                          const char* relWgs,
                                          const char* relGcj,
                                          const char* trkName,
                                          uint32_t nowUtc) {
    closeDualStreamLocked(stream);

    char fullWgs[96] = {};
    char fullGcj[96] = {};
    snprintf(fullWgs, sizeof(fullWgs), "%s/%s", SdCardConfig::DIR_TRACKS_ROOT, relWgs);
    snprintf(fullGcj, sizeof(fullGcj), "%s/%s", SdCardConfig::DIR_TRACKS_ROOT, relGcj);

    if (!stream.wgsFile.open(&_sd, fullWgs, O_RDWR | O_CREAT | O_TRUNC)) {
        return false;
    }
    if (!stream.gcjFile.open(&_sd, fullGcj, O_RDWR | O_CREAT | O_TRUNC)) {
        stream.wgsFile.close();
        _sd.remove(fullWgs);
        return false;
    }

    RtcDateTime utcDt{};
    if (nowUtc > 0UL) {
        RtcDriver::fromUnixEpoch(nowUtc, utcDt);
    }

    char wgsHdr[512];
    const int wgsLen = snprintf(
        wgsHdr, sizeof(wgsHdr),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<gpx version=\"1.1\" creator=\"M5Track-Tab5-M135\" "
        "xmlns=\"http://www.topografix.com/GPX/1/1\" "
        "xmlns:gpxtpx=\"http://www.garmin.com/xmlschemas/TrackPointExtension/v1\" "
        "xmlns:m5=\"https://m5stack.com/schema/track/v1\">\n"
        "  <metadata>\n"
        "    <name>%s (WGS-84)</name>\n"
        "    <time>%04u-%02u-%02uT%02u:%02u:%02uZ</time>\n"
        "  </metadata>\n"
        "  <trk>\n"
        "    <name>%s</name>\n"
        "    <trkseg>\n",
        trkName,
        utcDt.year, utcDt.month, utcDt.day, utcDt.hour, utcDt.minute, utcDt.second,
        trkName);

    char gcjHdr[512];
    const int gcjLen = snprintf(
        gcjHdr, sizeof(gcjHdr),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<gpx version=\"1.1\" creator=\"M5Track-Tab5-M135-GCJ02\" "
        "xmlns=\"http://www.topografix.com/GPX/1/1\" "
        "xmlns:gpxtpx=\"http://www.garmin.com/xmlschemas/TrackPointExtension/v1\" "
        "xmlns:m5=\"https://m5stack.com/schema/track/v1\">\n"
        "  <metadata>\n"
        "    <name>%s (GCJ-02)</name>\n"
        "    <time>%04u-%02u-%02uT%02u:%02u:%02uZ</time>\n"
        "  </metadata>\n"
        "  <trk>\n"
        "    <name>%s</name>\n"
        "    <trkseg>\n",
        trkName,
        utcDt.year, utcDt.month, utcDt.day, utcDt.hour, utcDt.minute, utcDt.second,
        trkName);

    stream.wgsFile.write(wgsHdr, static_cast<size_t>(wgsLen));
    stream.wgsInsertPos = stream.wgsFile.curPosition();
    stream.wgsFile.write(GPX_CLOSING_TAIL, GPX_CLOSING_TAIL_LEN);
    stream.wgsFile.sync();

    stream.gcjFile.write(gcjHdr, static_cast<size_t>(gcjLen));
    stream.gcjInsertPos = stream.gcjFile.curPosition();
    stream.gcjFile.write(GPX_CLOSING_TAIL, GPX_CLOSING_TAIL_LEN);
    stream.gcjFile.sync();

    strncpy(stream.relWgsPath, relWgs, sizeof(stream.relWgsPath) - 1);
    stream.relWgsPath[sizeof(stream.relWgsPath) - 1] = '\0';
    strncpy(stream.relGcjPath, relGcj, sizeof(stream.relGcjPath) - 1);
    stream.relGcjPath[sizeof(stream.relGcjPath) - 1] = '\0';
    stream.pointsInFile  = 0;
    stream.startEpochUtc = nowUtc;
    stream.isOpen        = true;
    return true;
}

bool TrackLogger::prepareExistingGpxForAppendLocked(FsFile& file, uint64_t& outInsertPos) {
    const uint64_t fSize = file.fileSize();
    if (fSize < 64ULL) {
        return false;
    }
    const size_t readLen = (fSize > 256ULL) ? 256U : static_cast<size_t>(fSize);
    const uint64_t basePos = fSize - readLen;
    char tail[260] = {};
    if (!file.seekSet(basePos)) {
        return false;
    }
    const int n = file.read(tail, readLen);
    if (n <= 0) {
        return false;
    }
    tail[n] = '\0';

    // Locate last </trk> so we can append a fresh <trkseg> cleanly before </trk></gpx>
    const char* trkClose = nullptr;
    const char* p = tail;
    while ((p = strstr(p, "</trk>")) != nullptr) {
        trkClose = p;
        p += 6;
    }
    if (trkClose == nullptr) {
        return false;
    }
    // Rewind over any preceding spaces on the "  </trk>" line
    while (trkClose > tail && (*(trkClose - 1) == ' ' || *(trkClose - 1) == '\t')) {
        --trkClose;
    }

    const uint64_t cutOffset = basePos + static_cast<uint64_t>(trkClose - tail);
    if (!file.seekSet(cutOffset)) {
        return false;
    }
    static constexpr const char* kOpenSeg = "    <trkseg>\n";
    file.write(kOpenSeg, strlen(kOpenSeg));
    outInsertPos = file.curPosition();
    file.write(GPX_CLOSING_TAIL, GPX_CLOSING_TAIL_LEN);
    file.truncate(file.curPosition());
    file.sync();
    return true;
}

bool TrackLogger::openOrAppendDualStreamLocked(DualGpxStream& stream,
                                               const char* relWgs,
                                               const char* relGcj,
                                               const char* trkName,
                                               uint32_t nowUtc) {
    closeDualStreamLocked(stream);

    char fullWgs[96] = {};
    char fullGcj[96] = {};
    snprintf(fullWgs, sizeof(fullWgs), "%s/%s", SdCardConfig::DIR_TRACKS_ROOT, relWgs);
    snprintf(fullGcj, sizeof(fullGcj), "%s/%s", SdCardConfig::DIR_TRACKS_ROOT, relGcj);

    if (_sd.exists(fullWgs) && _sd.exists(fullGcj)) {
        if (stream.wgsFile.open(&_sd, fullWgs, O_RDWR) &&
            stream.gcjFile.open(&_sd, fullGcj, O_RDWR)) {
            uint64_t wPos = 0, cPos = 0;
            if (prepareExistingGpxForAppendLocked(stream.wgsFile, wPos) &&
                prepareExistingGpxForAppendLocked(stream.gcjFile, cPos)) {
                stream.wgsInsertPos  = wPos;
                stream.gcjInsertPos  = cPos;
                stream.pointsInFile  = 0;
                stream.startEpochUtc = nowUtc;
                strncpy(stream.relWgsPath, relWgs, sizeof(stream.relWgsPath) - 1);
                stream.relWgsPath[sizeof(stream.relWgsPath) - 1] = '\0';
                strncpy(stream.relGcjPath, relGcj, sizeof(stream.relGcjPath) - 1);
                stream.relGcjPath[sizeof(stream.relGcjPath) - 1] = '\0';
                stream.isOpen = true;
                return true;
            }
            stream.wgsFile.close();
            stream.gcjFile.close();
        }
    }
    return openNewDualStreamLocked(stream, relWgs, relGcj, trkName, nowUtc);
}

bool TrackLogger::breakSegmentInDualStreamLocked(DualGpxStream& stream) {
    if (!stream.isOpen) {
        return false;
    }
    static constexpr const char* kBreak = "    </trkseg>\n    <trkseg>\n";
    const size_t breakLen = strlen(kBreak);

    if (stream.wgsFile.seekSet(stream.wgsInsertPos)) {
        stream.wgsFile.write(kBreak, breakLen);
        stream.wgsInsertPos = stream.wgsFile.curPosition();
        stream.wgsFile.write(GPX_CLOSING_TAIL, GPX_CLOSING_TAIL_LEN);
        stream.wgsFile.sync();
    }
    if (stream.gcjFile.seekSet(stream.gcjInsertPos)) {
        stream.gcjFile.write(kBreak, breakLen);
        stream.gcjInsertPos = stream.gcjFile.curPosition();
        stream.gcjFile.write(GPX_CLOSING_TAIL, GPX_CLOSING_TAIL_LEN);
        stream.gcjFile.sync();
    }
    return true;
}

void TrackLogger::closeDualStreamLocked(DualGpxStream& stream) {
    if (!stream.isOpen) {
        return;
    }
    if (stream.wgsFile.isOpen()) {
        if (stream.wgsFile.seekSet(stream.wgsInsertPos)) {
            stream.wgsFile.write(GPX_CLOSING_TAIL, GPX_CLOSING_TAIL_LEN);
            stream.wgsFile.truncate(stream.wgsFile.curPosition());
        }
        stream.wgsFile.sync();
        stream.wgsFile.close();
    }
    if (stream.gcjFile.isOpen()) {
        if (stream.gcjFile.seekSet(stream.gcjInsertPos)) {
            stream.gcjFile.write(GPX_CLOSING_TAIL, GPX_CLOSING_TAIL_LEN);
            stream.gcjFile.truncate(stream.gcjFile.curPosition());
        }
        stream.gcjFile.sync();
        stream.gcjFile.close();
    }
    stream.isOpen        = false;
    stream.wgsInsertPos  = 0;
    stream.gcjInsertPos  = 0;
    stream.pointsInFile  = 0;
    stream.relWgsPath[0] = '\0';
    stream.relGcjPath[0] = '\0';
}

void TrackLogger::syncOpenStreamsLocked() {
    if (_singleStream.isOpen) {
        _singleStream.wgsFile.sync();
        _singleStream.gcjFile.sync();
    }
    if (_dailyStream.isOpen) {
        _dailyStream.wgsFile.sync();
        _dailyStream.gcjFile.sync();
    }
    if (_subStream.isOpen) {
        _subStream.wgsFile.sync();
        _subStream.gcjFile.sync();
    }
}

bool TrackLogger::writeTrackPointToStreamLocked(DualGpxStream& stream,
                                                const SystemStateSnapshot& snap,
                                                uint32_t monoEpochUtc,
                                                uint16_t monoMs,
                                                double gcjLat,
                                                double gcjLon) {
    if (!stream.isOpen) {
        return false;
    }

    RtcDateTime utcDt{};
    RtcDriver::fromUnixEpoch(monoEpochUtc, utcDt);

    const float altM = snap.baro.isOnline ? snap.baro.fusedAltitudeM : snap.gnss.altitudeMeters;
    const char* fixTag = "3d";
    if (snap.gnss.isDeadReckoning || snap.gnss.fixType == FIX_DEAD_REC) {
        fixTag = "dr";
    } else if (snap.gnss.fixType == FIX_2D) {
        fixTag = "2d";
    }

    char commonBody[384];
    snprintf(
        commonBody, sizeof(commonBody),
        "        <ele>%.1f</ele>\n"
        "        <time>%04u-%02u-%02uT%02u:%02u:%02u.%03uZ</time>\n"
        "        <fix>%s</fix>\n"
        "        <sat>%u</sat>\n"
        "        <hdop>%.2f</hdop>\n"
        "        <extensions>\n"
        "          <gpxtpx:TrackPointExtension><gpxtpx:atemp>%.1f</gpxtpx:atemp></gpxtpx:TrackPointExtension>\n"
        "          <m5:speed_kmh>%.1f</m5:speed_kmh>\n"
        "          <m5:course>%.1f</m5:course>\n"
        "          <m5:slope_pct>%.1f</m5:slope_pct>\n"
        "          <m5:lat_g>%.2f</m5:lat_g>\n"
        "          <m5:lon_g>%.2f</m5:lon_g>\n"
        "        </extensions>\n"
        "      </trkpt>\n",
        altM,
        utcDt.year, utcDt.month, utcDt.day, utcDt.hour, utcDt.minute, utcDt.second,
        static_cast<unsigned>(monoMs % 1000U),
        fixTag,
        static_cast<unsigned>(snap.gnss.numSats),
        snap.gnss.hdop,
        snap.baro.temperatureC,
        snap.gnss.speedKmh,
        snap.gnss.headingDeg,
        snap.baro.roadSlopePercent,
        snap.imu.lateralG,
        snap.imu.longitudinalG);

    char wgsPt[480];
    const int wgsPtLen = snprintf(wgsPt, sizeof(wgsPt),
                                  "      <trkpt lat=\"%.7f\" lon=\"%.7f\">\n%s",
                                  snap.gnss.latitude, snap.gnss.longitude, commonBody);

    char gcjPt[480];
    const int gcjPtLen = snprintf(gcjPt, sizeof(gcjPt),
                                  "      <trkpt lat=\"%.7f\" lon=\"%.7f\">\n%s",
                                  gcjLat, gcjLon, commonBody);

    // Stage 1 Power-Loss Protection: overwrite previous footer with new <trkpt> + fresh footer
    if (!stream.wgsFile.seekSet(stream.wgsInsertPos)) {
        return false;
    }
    if (stream.wgsFile.write(wgsPt, static_cast<size_t>(wgsPtLen)) != static_cast<size_t>(wgsPtLen)) {
        return false;
    }
    stream.wgsInsertPos = stream.wgsFile.curPosition();
    stream.wgsFile.write(GPX_CLOSING_TAIL, GPX_CLOSING_TAIL_LEN);

    if (!stream.gcjFile.seekSet(stream.gcjInsertPos)) {
        return false;
    }
    if (stream.gcjFile.write(gcjPt, static_cast<size_t>(gcjPtLen)) != static_cast<size_t>(gcjPtLen)) {
        return false;
    }
    stream.gcjInsertPos = stream.gcjFile.curPosition();
    stream.gcjFile.write(GPX_CLOSING_TAIL, GPX_CLOSING_TAIL_LEN);

    stream.pointsInFile++;
    return true;
}

bool TrackLogger::appendTripCsvRowLocked(const char* categoryLabel,
                                         const TripStats& stats,
                                         uint32_t endEpochUtc,
                                         int16_t tzOffsetMin,
                                         const char* gpxRelPath) {
    if (!_mounted) {
        return false;
    }
    if (stats.tripDistanceKm < SdCardConfig::MIN_LOG_TRIP_DIST_KM &&
        stats.movingDurationSec < SdCardConfig::MIN_LOG_TRIP_MOVING_SEC) {
        return false; // Debounce stationary micro-movements
    }

    ensureDirectoriesAndLedgerLocked();
    FsFile csv = _sd.open(SdCardConfig::FILE_TRIPS_CSV, O_WRONLY | O_CREAT | O_APPEND);
    if (!csv.isOpen()) {
        return false;
    }

    char startStr[24] = {};
    char endStr[24]   = {};
    char driveHms[16] = {};
    char idleHms[16]  = {};
    const uint32_t startUtc = (stats.startTimestampUtc > 0UL) ? stats.startTimestampUtc : endEpochUtc;
    formatLocalTimestamp(startUtc, tzOffsetMin, startStr, sizeof(startStr));
    formatLocalTimestamp(endEpochUtc, tzOffsetMin, endStr, sizeof(endStr));
    formatDurationHms(stats.movingDurationSec, driveHms, sizeof(driveHms));
    formatDurationHms(stats.idleDurationSec, idleHms, sizeof(idleHms));

    char row[320];
    const int rowLen = snprintf(
        row, sizeof(row),
        "%s,%s,%s,%.2f,%s,%s,%.1f,%.1f,%.1f,%.1f,%.1f,%.2f,%.2f,%s\n",
        startStr,
        endStr,
        categoryLabel ? categoryLabel : "SINGLE",
        stats.tripDistanceKm,
        driveHms,
        idleHms,
        stats.avgSpeedKmh,
        stats.maxSpeedKmh,
        stats.cumulativeClimbM,
        stats.maxAltitudeM,
        stats.minAltitudeM,
        stats.maxLateralG,
        stats.maxLongitudinalG,
        (gpxRelPath && gpxRelPath[0] != '\0') ? gpxRelPath : "-");

    csv.write(row, static_cast<size_t>(rowLen));
    csv.sync();
    csv.close();
    return true;
}

bool TrackLogger::appendPowerLog(const char* eventName, const char* details) {
    SdGuard sdLock(_sdMutex, pdMS_TO_TICKS(500));
    if (!sdLock.locked() || !_mounted) {
        return false;
    }

    uint32_t nowUtc = 0;
    int16_t  tzMin  = 480;
    float    battV  = 0.0f;
    float    socPct = 0.0f;
    bool     extPwr = false;
    {
        StateGuard guard(SystemState::instance(), pdMS_TO_TICKS(20));
        if (guard.locked()) {
            const SystemStateSnapshot& st = SystemState::instance().data();
            nowUtc = st.time.epochUtc;
            tzMin  = st.time.localTzOffsetMin;
            battV  = st.power.busVoltageV;
            socPct = st.power.socPercent;
            extPwr = st.power.isExternalPower;
        }
    }

    char tsStr[24] = {};
    formatLocalTimestamp(nowUtc, tzMin, tsStr, sizeof(tsStr));

    FsFile logFile = _sd.open(SdCardConfig::FILE_POWER_LOG, O_RDWR | O_CREAT | O_APPEND);
    if (!logFile.isOpen()) {
        return false;
    }

    char line[224];
    const int len = snprintf(line, sizeof(line),
                             "[%s] [%-17s] V=%.2fV SoC=%.0f%% EXT=%d | %s\n",
                             tsStr,
                             eventName ? eventName : "INFO",
                             battV,
                             socPct,
                             extPwr ? 1 : 0,
                             details ? details : "");
    logFile.write(line, static_cast<size_t>(len));
    const uint64_t totalSize = logFile.fileSize();
    logFile.sync();
    logFile.close();

    // 100KB FIFO Ring Cap: trim oldest ~20KB aligned to line break into power.tmp
    if (totalSize > SdCardConfig::POWER_LOG_MAX_BYTES) {
        FsFile src = _sd.open(SdCardConfig::FILE_POWER_LOG, O_RDONLY);
        if (src.isOpen()) {
            uint64_t startPos = SdCardConfig::POWER_LOG_TRIM_BYTES;
            if (startPos < totalSize && src.seekSet(startPos)) {
                char ch = 0;
                while (src.read(&ch, 1) == 1) {
                    startPos++;
                    if (ch == '\n' || startPos >= totalSize) {
                        break;
                    }
                }
                if (startPos < totalSize && src.seekSet(startPos)) {
                    _sd.remove(SdCardConfig::FILE_POWER_TMP);
                    FsFile dst = _sd.open(SdCardConfig::FILE_POWER_TMP, O_WRONLY | O_CREAT | O_TRUNC);
                    if (dst.isOpen()) {
                        uint8_t copyBuf[512];
                        int n = 0;
                        while ((n = src.read(copyBuf, sizeof(copyBuf))) > 0) {
                            dst.write(copyBuf, static_cast<size_t>(n));
                        }
                        dst.sync();
                        dst.close();
                        src.close();
                        _sd.remove(SdCardConfig::FILE_POWER_LOG);
                        _sd.rename(SdCardConfig::FILE_POWER_TMP, SdCardConfig::FILE_POWER_LOG);
                        return true;
                    }
                }
            }
            src.close();
        }
    }
    return true;
}

uint64_t TrackLogger::computeFolderBytesLocked(const char* dirPath, uint16_t* outWgsCount) {
    if (outWgsCount != nullptr) {
        *outWgsCount = 0;
    }
    FsFile dir = _sd.open(dirPath, O_RDONLY);
    if (!dir.isOpen() || !dir.isDirectory()) {
        return 0ULL;
    }
    uint64_t sum = 0ULL;
    uint16_t wgsPairs = 0;
    FsFile entry;
    char name[64] = {};
    while (entry.openNext(&dir, O_RDONLY)) {
        if (!entry.isDirectory()) {
            sum += entry.fileSize();
            entry.getName(name, sizeof(name));
            if (strncmp(name, "W-", 2) == 0 && endsWithIgnoreCase(name, ".gpx")) {
                wgsPairs++;
            }
        }
        entry.close();
    }
    dir.close();
    if (outWgsCount != nullptr) {
        *outWgsCount = wgsPairs;
    }
    return sum;
}

void TrackLogger::enforceStorageQuota50PercentLocked() {
    if (!_mounted) {
        return;
    }
    const uint64_t clusterBytes = _sd.bytesPerCluster();
    const uint64_t totalClusters = _sd.clusterCount();
    const uint64_t totalCardBytes = totalClusters * clusterBytes;
    if (totalCardBytes == 0ULL) {
        return;
    }

    uint16_t singlePairs = 0, dailyPairs = 0, subPairs = 0;
    uint64_t tracksBytes = computeFolderBytesLocked(SdCardConfig::DIR_SINGLE, &singlePairs)
                         + computeFolderBytesLocked(SdCardConfig::DIR_DAILY, &dailyPairs)
                         + computeFolderBytesLocked(SdCardConfig::DIR_SUB, &subPairs);

    // Include root /tracks metadata files (trips.csv, power.log, .synced_ledger)
    FsFile rootTracks = _sd.open(SdCardConfig::DIR_TRACKS_ROOT, O_RDONLY);
    if (rootTracks.isOpen() && rootTracks.isDirectory()) {
        FsFile f;
        while (f.openNext(&rootTracks, O_RDONLY)) {
            if (!f.isDirectory()) {
                tracksBytes += f.fileSize();
            }
            f.close();
        }
        rootTracks.close();
    }

    const uint64_t maxQuotaBytes = static_cast<uint64_t>(
        static_cast<double>(totalCardBytes) * static_cast<double>(SdCardConfig::TRACK_STORAGE_QUOTA_RATIO));
    const uint64_t targetRecoverBytes = static_cast<uint64_t>(
        static_cast<double>(totalCardBytes) * static_cast<double>(SdCardConfig::TRACK_RECOVERY_QUOTA_RATIO));

    if (tracksBytes < maxQuotaBytes) {
        return;
    }

    // Evict oldest closed W-/C- file pairs across single, daily, and sub until <= 40%
    static constexpr const char* kSubFolders[3] = { "single", "daily", "sub" };

    for (uint16_t guardIter = 0; guardIter < 256U && tracksBytes > targetRecoverBytes; ++guardIter) {
        uint32_t oldestKey   = 0xFFFFFFFFUL;
        char     oldestName[64] = "";
        char     oldestFolder[12] = "";
        char     oldestRelWgs[64] = "";

        for (size_t d = 0; d < 3; ++d) {
            char dirFull[48];
            snprintf(dirFull, sizeof(dirFull), "%s/%s", SdCardConfig::DIR_TRACKS_ROOT, kSubFolders[d]);
            FsFile dir = _sd.open(dirFull, O_RDONLY);
            if (!dir.isOpen() || !dir.isDirectory()) {
                continue;
            }
            FsFile entry;
            char name[48] = {};
            while (entry.openNext(&dir, O_RDONLY)) {
                if (!entry.isDirectory()) {
                    entry.getName(name, sizeof(name));
                    if (strncmp(name, "W-", 2) == 0 && endsWithIgnoreCase(name, ".gpx")) {
                        char relCandidate[64];
                        snprintf(relCandidate, sizeof(relCandidate), "%s/%s", kSubFolders[d], name);
                        if (!isPathActiveOpen(relCandidate)) {
                            uint16_t pdate = 0, ptime = 0;
                            entry.getModifyDateTime(&pdate, &ptime);
                            const uint32_t key = (static_cast<uint32_t>(pdate) << 16) | ptime;
                            if (oldestRelWgs[0] == '\0' || key < oldestKey ||
                                (key == oldestKey && strcmp(name, oldestName) < 0)) {
                                oldestKey = key;
                                strncpy(oldestName, name, sizeof(oldestName) - 1);
                                strncpy(oldestFolder, kSubFolders[d], sizeof(oldestFolder) - 1);
                                strncpy(oldestRelWgs, relCandidate, sizeof(oldestRelWgs) - 1);
                            }
                        }
                    }
                }
                entry.close();
            }
            dir.close();
        }

        if (oldestRelWgs[0] == '\0') {
            break; // No closed pairs left to evict
        }

        char fullW[96] = {};
        char fullC[96] = {};
        snprintf(fullW, sizeof(fullW), "%s/%s/%s", SdCardConfig::DIR_TRACKS_ROOT, oldestFolder, oldestName);
        snprintf(fullC, sizeof(fullC), "%s/%s/C-%s", SdCardConfig::DIR_TRACKS_ROOT, oldestFolder, oldestName + 2);

        uint64_t freed = 0ULL;
        FsFile fw = _sd.open(fullW, O_RDONLY);
        if (fw.isOpen()) {
            freed += fw.fileSize();
            fw.close();
            _sd.remove(fullW);
        }
        FsFile fc = _sd.open(fullC, O_RDONLY);
        if (fc.isOpen()) {
            freed += fc.fileSize();
            fc.close();
            _sd.remove(fullC);
        }

        if (freed == 0ULL) {
            break;
        }
        tracksBytes = (tracksBytes > freed) ? (tracksBytes - freed) : 0ULL;
    }
}

void TrackLogger::refreshStorageStats() {
    SdGuard sdLock(_sdMutex, pdMS_TO_TICKS(500));
    if (!sdLock.locked()) {
        return;
    }

    uint64_t totalB  = 0ULL;
    uint64_t usedB   = 0ULL;
    uint64_t tracksB = 0ULL;
    uint16_t sPairs  = 0, dPairs = 0, subPairs = 0;

    if (_mounted) {
        const uint64_t bpc = _sd.bytesPerCluster();
        const uint64_t cc  = _sd.clusterCount();
        totalB = cc * bpc;
        const int32_t freeC = _sd.freeClusterCount();
        if (freeC >= 0 && static_cast<uint64_t>(freeC) <= cc) {
            usedB = (cc - static_cast<uint64_t>(freeC)) * bpc;
        }

        tracksB = computeFolderBytesLocked(SdCardConfig::DIR_SINGLE, &sPairs)
                + computeFolderBytesLocked(SdCardConfig::DIR_DAILY, &dPairs)
                + computeFolderBytesLocked(SdCardConfig::DIR_SUB, &subPairs);

        FsFile rootTracks = _sd.open(SdCardConfig::DIR_TRACKS_ROOT, O_RDONLY);
        if (rootTracks.isOpen() && rootTracks.isDirectory()) {
            FsFile f;
            while (f.openNext(&rootTracks, O_RDONLY)) {
                if (!f.isDirectory()) {
                    tracksB += f.fileSize();
                }
                f.close();
            }
            rootTracks.close();
        }
    }

    const float ratio = (totalB > 0ULL)
        ? static_cast<float>(static_cast<double>(tracksB) / static_cast<double>(totalB))
        : 0.0f;

    StateGuard guard(SystemState::instance());
    StorageData& st = SystemState::instance().data().storage;
    st.isMounted          = _mounted;
    st.formatIncompatible = _formatIncompat;
    st.fsType             = _fsType;
    st.totalBytes         = totalB;
    st.usedBytes          = usedB;
    st.tracksDirBytes     = tracksB;
    st.tracksQuotaRatio   = ratio;
    st.singlePairCount    = sPairs;
    st.dailyPairCount     = dPairs;
    st.subPairCount       = subPairs;
    st.totalTrackPairs    = static_cast<uint16_t>(sPairs + dPairs + subPairs);
}

void TrackLogger::syncActivePathsToState() {
    const uint32_t subBytes = (_subStream.isOpen && _subStream.wgsFile.isOpen())
        ? static_cast<uint32_t>(_subStream.wgsFile.fileSize())
        : 0UL;

    StateGuard guard(SystemState::instance());
    StorageData& st = SystemState::instance().data().storage;
    st.isRecording        = _singleStream.isOpen || _dailyStream.isOpen || _subStream.isOpen;
    st.singleSplitSeq     = _singleSplitSeq;
    st.singlePointsInFile = _singleStream.pointsInFile;
    st.subIndex           = _subIndex;
    st.subVolumeIndex     = _subVolumeIndex;
    st.subCurrentBytes    = subBytes;

    strncpy(st.activeSingleWgsPath, _singleStream.relWgsPath, sizeof(st.activeSingleWgsPath) - 1);
    st.activeSingleWgsPath[sizeof(st.activeSingleWgsPath) - 1] = '\0';
    strncpy(st.activeSingleGcjPath, _singleStream.relGcjPath, sizeof(st.activeSingleGcjPath) - 1);
    st.activeSingleGcjPath[sizeof(st.activeSingleGcjPath) - 1] = '\0';

    strncpy(st.activeDailyWgsPath, _dailyStream.relWgsPath, sizeof(st.activeDailyWgsPath) - 1);
    st.activeDailyWgsPath[sizeof(st.activeDailyWgsPath) - 1] = '\0';
    strncpy(st.activeDailyGcjPath, _dailyStream.relGcjPath, sizeof(st.activeDailyGcjPath) - 1);
    st.activeDailyGcjPath[sizeof(st.activeDailyGcjPath) - 1] = '\0';

    strncpy(st.activeSubWgsPath, _subStream.relWgsPath, sizeof(st.activeSubWgsPath) - 1);
    st.activeSubWgsPath[sizeof(st.activeSubWgsPath) - 1] = '\0';
    strncpy(st.activeSubGcjPath, _subStream.relGcjPath, sizeof(st.activeSubGcjPath) - 1);
    st.activeSubGcjPath[sizeof(st.activeSubGcjPath) - 1] = '\0';
}

void TrackLogger::flushAndCloseAllStreams(bool writeSingleTripCsv) {
    uint32_t nowUtc = 0;
    int16_t  tzMin  = 480;
    {
        StateGuard guard(SystemState::instance(), pdMS_TO_TICKS(50));
        if (guard.locked()) {
            const SystemStateSnapshot& st = SystemState::instance().data();
            nowUtc = st.time.epochUtc;
            tzMin  = st.time.localTzOffsetMin;
            if (st.trips[TRIP_SINGLE].tripDistanceKm > _cachedSingleStats.tripDistanceKm) {
                _cachedSingleStats = st.trips[TRIP_SINGLE];
            }
        }
    }

    {
        SdGuard sdLock(_sdMutex);
        if (writeSingleTripCsv && (_singleStream.isOpen || _lastSingleWgsRel[0] != '\0')) {
            const char* refPath = _singleStream.isOpen ? _singleStream.relWgsPath : _lastSingleWgsRel;
            appendTripCsvRowLocked("SINGLE", _cachedSingleStats, nowUtc, tzMin, refPath);
            _cachedSingleStats = TripStats{};
            _lastSingleWgsRel[0] = '\0';
        }
        closeDualStreamLocked(_singleStream);
        closeDualStreamLocked(_dailyStream);
        closeDualStreamLocked(_subStream);
    }
    syncActivePathsToState();
    refreshStorageStats();
}

void TrackLogger::handlePendingCommands(const SystemStateSnapshot& snap) {
    // 1. Format FAT32 request
    if (snap.storage.cmdFormatFat32) {
        formatToFat32();
        return;
    }

    // 2. Delete all tracks request
    if (snap.storage.cmdDeleteAllTracks) {
        {
            StateGuard guard(SystemState::instance());
            SystemState::instance().data().storage.cmdDeleteAllTracks = false;
        }
        deleteAllTracks();
    }

    // 3. Delete single relative track / pair request
    if (snap.storage.cmdDeleteRelPath[0] != '\0') {
        char targetRel[64] = {};
        {
            StateGuard guard(SystemState::instance());
            strncpy(targetRel,
                    SystemState::instance().data().storage.cmdDeleteRelPath,
                    sizeof(targetRel) - 1);
            SystemState::instance().data().storage.cmdDeleteRelPath[0] = '\0';
        }
        deleteTrackOrPair(targetRel);
    }

    // 4. Refresh storage stats request
    if (snap.storage.cmdRefreshStorage) {
        {
            StateGuard guard(SystemState::instance());
            SystemState::instance().data().storage.cmdRefreshStorage = false;
        }
        refreshStorageStats();
    }

    // 5. Manual Sub-Trip Start ([+ Sub Start])
    if (snap.tripControl.cmdStartManualTrip) {
        {
            SdGuard sdLock(_sdMutex);
            if (_manualTripActive && (_subStream.isOpen || _lastSubWgsRel[0] != '\0')) {
                const char* subRef = _subStream.isOpen ? _subStream.relWgsPath : _lastSubWgsRel;
                appendTripCsvRowLocked("SUB", _cachedSubStats, snap.time.epochUtc, snap.time.localTzOffsetMin, subRef);
            }
            closeDualStreamLocked(_subStream);
        }

        _subIndex         = static_cast<uint16_t>(_subIndex + 1U);
        _subVolumeIndex   = 1U;
        _manualTripActive = true;
        _cachedSubStats   = TripStats{};
        _lastSubWgsRel[0] = '\0';
        saveSubTripNvsState(true, _subIndex, _subVolumeIndex);

        const float curAlt = snap.baro.isOnline ? snap.baro.fusedAltitudeM : snap.gnss.altitudeMeters;
        {
            StateGuard guard(SystemState::instance());
            SystemStateSnapshot& st = SystemState::instance().data();
            st.tripControl.cmdStartManualTrip = false;
            st.tripControl.manualTripActive   = true;
            st.tripControl.manualSubIndex     = _subIndex;
            st.tripControl.manualSplitIndex   = _subVolumeIndex;
            st.tripControl.activeViewCategory = TRIP_MANUAL;
            st.trips[TRIP_MANUAL].resetWithAnchor(
                snap.time.epochUtc, snap.gnss.latitude, snap.gnss.longitude,
                snap.gnss.isValid, curAlt, snap.gnss.speedKmh);
        }
        SystemState::instance().resetTrail(TRIP_MANUAL);
        syncActivePathsToState();
    }

    // 6. Manual Sub-Trip Stop ([Stop Sub])
    if (snap.tripControl.cmdStopManualTrip) {
        {
            SdGuard sdLock(_sdMutex);
            const char* subRef = _subStream.isOpen ? _subStream.relWgsPath : _lastSubWgsRel;
            appendTripCsvRowLocked("SUB", _cachedSubStats, snap.time.epochUtc, snap.time.localTzOffsetMin, subRef);
            closeDualStreamLocked(_subStream);
        }

        _manualTripActive = false;
        _cachedSubStats   = TripStats{};
        _lastSubWgsRel[0] = '\0';
        saveSubTripNvsState(false, _subIndex, _subVolumeIndex);

        {
            StateGuard guard(SystemState::instance());
            SystemStateSnapshot& st = SystemState::instance().data();
            st.tripControl.cmdStopManualTrip = false;
            st.tripControl.manualTripActive  = false;
            if (st.tripControl.activeViewCategory == TRIP_MANUAL) {
                st.tripControl.activeViewCategory = TRIP_SINGLE;
            }
        }
        syncActivePathsToState();
        refreshStorageStats();
    }

    // 7. Full Trip Reset / GPX Segment Break (Page 1 2s hold or standby wakeup)
    const bool resetOccurred = snap.tripControl.cmdGpxBreakSegment ||
                               (snap.tripControl.tripResetVersion != _seenResetVersion);
    if (resetOccurred) {
        // Fallback: if GnssDriver hasn't consumed cmdFullTripReset yet, let GnssDriver handle
        // executeFullTripReset first unless GnssDriver is offline!
        if (snap.tripControl.cmdFullTripReset && !snap.boot.gnssUartOnline) {
            FullTripResetPayload payload{};
            payload.timestampUtc      = snap.time.epochUtc;
            payload.newStartLatitude  = snap.gnss.latitude;
            payload.newStartLongitude = snap.gnss.longitude;
            payload.currentAltitudeM  = snap.baro.isOnline ? snap.baro.fusedAltitudeM : snap.gnss.altitudeMeters;
            payload.currentSpeedKmh   = snap.gnss.speedKmh;
            SystemState::instance().executeFullTripReset(payload);
        }

        {
            SdGuard sdLock(_sdMutex);
            if (_singleStream.isOpen || _lastSingleWgsRel[0] != '\0') {
                const char* refPath = _singleStream.isOpen ? _singleStream.relWgsPath : _lastSingleWgsRel;
                appendTripCsvRowLocked("SINGLE", _cachedSingleStats, snap.time.epochUtc, snap.time.localTzOffsetMin, refPath);
            }
            // Close single-trip GPX so next movement opens a brand-new timestamped single GPX pair
            closeDualStreamLocked(_singleStream);
            _singleSplitSeq      = 1U;
            _cachedSingleStats   = TripStats{};
            _lastSingleWgsRel[0] = '\0';

            // Break <trkseg> inside daily & manual sub streams to mark trip boundary cleanly
            breakSegmentInDualStreamLocked(_dailyStream);
            breakSegmentInDualStreamLocked(_subStream);
        }

        {
            StateGuard guard(SystemState::instance());
            SystemStateSnapshot& st = SystemState::instance().data();
            st.tripControl.cmdGpxBreakSegment = false;
            _seenResetVersion = st.tripControl.tripResetVersion;
        }
        syncActivePathsToState();
        refreshStorageStats();
    }

    // 8. 20-Minute Standstill Pre-Sleep Settlement Handshake (PowerMonitorService -> TrackLogger)
    if (snap.power.preSleepSettleReq && !snap.power.preSleepSettleAck) {
        flushAndCloseAllStreams(true);
        {
            StateGuard guard(SystemState::instance());
            SystemState::instance().data().power.preSleepSettleAck = true;
        }
    }
}

void TrackLogger::handleDailyRollover(const SystemStateSnapshot& snap, uint32_t localYmd) {
    if (localYmd < 20250101UL || !snap.time.isCalibrated) {
        return;
    }
    if (_currentDailyYmd == 0UL) {
        _currentDailyYmd = localYmd;
        saveDailyNvsState(_currentDailyYmd, snap.trips[TRIP_DAILY].tripDistanceKm);
        StateGuard guard(SystemState::instance());
        SystemState::instance().data().tripControl.lastLocalDayYmd = _currentDailyYmd;
        return;
    }

    if (localYmd != _currentDailyYmd) {
        // Local midnight crossed: settle yesterday's daily GPX & CSV, reset TRIP_DAILY
        {
            SdGuard sdLock(_sdMutex);
            if (_dailyStream.isOpen || _lastDailyWgsRel[0] != '\0') {
                const char* ref = _dailyStream.isOpen ? _dailyStream.relWgsPath : _lastDailyWgsRel;
                appendTripCsvRowLocked("DAILY", _cachedDailyStats, snap.time.epochUtc, snap.time.localTzOffsetMin, ref);
            }
            closeDualStreamLocked(_dailyStream);
        }

        _currentDailyYmd     = localYmd;
        _cachedDailyStats    = TripStats{};
        _lastDailyWgsRel[0]  = '\0';
        saveDailyNvsState(_currentDailyYmd, 0.0f);

        const float curAlt = snap.baro.isOnline ? snap.baro.fusedAltitudeM : snap.gnss.altitudeMeters;
        {
            StateGuard guard(SystemState::instance());
            SystemStateSnapshot& st = SystemState::instance().data();
            st.tripControl.lastLocalDayYmd = _currentDailyYmd;
            st.trips[TRIP_DAILY].resetWithAnchor(
                snap.time.epochUtc, snap.gnss.latitude, snap.gnss.longitude,
                snap.gnss.isValid, curAlt, snap.gnss.speedKmh);
        }
        SystemState::instance().resetTrail(TRIP_DAILY);
        syncActivePathsToState();
        refreshStorageStats();
    }
}

void TrackLogger::update() {
    SystemStateSnapshot snap;
    if (!SystemState::instance().getSnapshot(snap, pdMS_TO_TICKS(20))) {
        return;
    }

    handlePendingCommands(snap);

    if (!_mounted) {
        return;
    }

    // Keep pre-reset trip stats snapshots fresh whenever no reset is pending
    if (!snap.tripControl.cmdFullTripReset &&
        !snap.tripControl.cmdGpxBreakSegment &&
        snap.tripControl.tripResetVersion == _seenResetVersion) {
        _cachedSingleStats = snap.trips[TRIP_SINGLE];
    }
    _cachedDailyStats = snap.trips[TRIP_DAILY];
    if (_manualTripActive) {
        _cachedSubStats = snap.trips[TRIP_MANUAL];
    }

    const uint32_t localYmd = static_cast<uint32_t>(snap.time.localYear) * 10000UL
                            + static_cast<uint32_t>(snap.time.localMonth) * 100UL
                            + static_cast<uint32_t>(snap.time.localDay);
    handleDailyRollover(snap, localYmd);

    // 1Hz recording cadence gate
    const uint32_t nowMs = millis();
    if (nowMs - _lastRecordTickMs < 1000UL) {
        return;
    }
    _lastRecordTickMs = nowMs;

    const bool autoRec = snap.storage.autoRecordEnabled && snap.settings.autoRecordOnMove;
    const bool validNav = snap.gnss.isValid &&
                          ((snap.gnss.fixType >= FIX_3D && snap.gnss.gnssFixOk) || snap.gnss.isDeadReckoning);
    const bool movingAboveStart = (snap.gnss.speedKmh > GnssConfig::RECORD_START_SPEED_KMH);
    const bool alreadyOpen = _singleStream.isOpen || _dailyStream.isOpen || _subStream.isOpen;
    const bool movingContinue = alreadyOpen && (snap.gnss.speedKmh > GnssConfig::STATIC_DEADBAND_KMH);

    if (!autoRec || !validNav || (!movingAboveStart && !movingContinue) ||
        snap.power.standbyPhase != STANDBY_PHASE_ACTIVE) {
        // Still run periodic 10s sync on open files while waiting at traffic lights
        if (alreadyOpen && (nowMs - _lastSyncMs >= SdCardConfig::GPX_SYNC_INTERVAL_MS)) {
            SdGuard sdLock(_sdMutex, pdMS_TO_TICKS(200));
            if (sdLock.locked()) {
                syncOpenStreamsLocked();
                _lastSyncMs = nowMs;
            }
        }
        return;
    }

    // Stage 4 Power-Loss Pipeline: Strictly monotonic UTC timestamp enforcement
    uint32_t ptEpoch = snap.time.epochUtc;
    if (ptEpoch == 0UL && snap.gnss.utcDateValid && snap.gnss.utcTimeValid) {
        RtcDateTime gdt{ snap.gnss.year, snap.gnss.month, snap.gnss.day, 0,
                         snap.gnss.hour, snap.gnss.minute, snap.gnss.second };
        ptEpoch = RtcDriver::toUnixEpoch(gdt);
    }
    uint16_t ptMs = snap.gnss.millisecond % 1000U;
    if (ptEpoch < _lastMonotonicUtc) {
        ptEpoch = _lastMonotonicUtc;
        ptMs    = static_cast<uint16_t>((_lastMonotonicMs + 10U) % 1000U);
        if (ptMs < _lastMonotonicMs) {
            ptEpoch++;
        }
    } else if (ptEpoch == _lastMonotonicUtc && ptMs <= _lastMonotonicMs) {
        if (_lastMonotonicMs < 999U) {
            ptMs = static_cast<uint16_t>(_lastMonotonicMs + 1U);
        } else {
            ptEpoch++;
            ptMs = 0U;
        }
    }
    _lastMonotonicUtc = ptEpoch;
    _lastMonotonicMs  = ptMs;

    double gcjLat = 0.0, gcjLon = 0.0;
    wgs84ToGcj02(snap.gnss.latitude, snap.gnss.longitude, gcjLat, gcjLon);

    {
        SdGuard sdLock(_sdMutex, pdMS_TO_TICKS(300));
        if (!sdLock.locked()) {
            return;
        }

        // 1) /tracks/single/W-yyyymmdd-hhmmss.gpx & C-yyyymmdd-hhmmss.gpx (3600-point hourly split)
        if (_singleStream.isOpen && _singleStream.pointsInFile >= SdCardConfig::HOURLY_SPLIT_POINTS) {
            closeDualStreamLocked(_singleStream);
            _singleSplitSeq++;
        }
        if (!_singleStream.isOpen) {
            char relW[48], relC[48], trkTitle[48];
            snprintf(relW, sizeof(relW), "single/W-%04u%02u%02u-%02u%02u%02u.gpx",
                     snap.time.localYear, snap.time.localMonth, snap.time.localDay,
                     snap.time.localHour, snap.time.localMinute, snap.time.localSecond);
            snprintf(relC, sizeof(relC), "single/C-%04u%02u%02u-%02u%02u%02u.gpx",
                     snap.time.localYear, snap.time.localMonth, snap.time.localDay,
                     snap.time.localHour, snap.time.localMinute, snap.time.localSecond);
            snprintf(trkTitle, sizeof(trkTitle), "Single-%04u%02u%02u-%02u%02u%02u",
                     snap.time.localYear, snap.time.localMonth, snap.time.localDay,
                     snap.time.localHour, snap.time.localMinute, snap.time.localSecond);
            if (openNewDualStreamLocked(_singleStream, relW, relC, trkTitle, ptEpoch)) {
                strncpy(_lastSingleWgsRel, relW, sizeof(_lastSingleWgsRel) - 1);
            }
        }
        writeTrackPointToStreamLocked(_singleStream, snap, ptEpoch, ptMs, gcjLat, gcjLon);

        // 2) /tracks/daily/W-yyyymmdd.gpx & C-yyyymmdd.gpx (same-day <trkseg> append)
        if (!_dailyStream.isOpen) {
            char relW[48], relC[48], trkTitle[48];
            snprintf(relW, sizeof(relW), "daily/W-%04u%02u%02u.gpx",
                     snap.time.localYear, snap.time.localMonth, snap.time.localDay);
            snprintf(relC, sizeof(relC), "daily/C-%04u%02u%02u.gpx",
                     snap.time.localYear, snap.time.localMonth, snap.time.localDay);
            snprintf(trkTitle, sizeof(trkTitle), "Daily-%04u%02u%02u",
                     snap.time.localYear, snap.time.localMonth, snap.time.localDay);
            if (openOrAppendDualStreamLocked(_dailyStream, relW, relC, trkTitle, ptEpoch)) {
                strncpy(_lastDailyWgsRel, relW, sizeof(_lastDailyWgsRel) - 1);
            }
        }
        writeTrackPointToStreamLocked(_dailyStream, snap, ptEpoch, ptMs, gcjLat, gcjLon);

        // 3) /tracks/sub/W-subXXX_partN.gpx & C-subXXX_partN.gpx (strict 10 MiB volume cap!)
        if (_manualTripActive) {
            if (_subIndex == 0U) {
                _subIndex = 1U;
            }
            if (_subStream.isOpen) {
                const uint64_t nextWgsSize = _subStream.wgsInsertPos + 540ULL + GPX_CLOSING_TAIL_LEN;
                const uint64_t nextGcjSize = _subStream.gcjInsertPos + 540ULL + GPX_CLOSING_TAIL_LEN;
                if (nextWgsSize >= SdCardConfig::SUB_TRIP_MAX_VOLUME_BYTES ||
                    nextGcjSize >= SdCardConfig::SUB_TRIP_MAX_VOLUME_BYTES) {
                    closeDualStreamLocked(_subStream);
                    _subVolumeIndex++;
                    saveSubTripNvsState(true, _subIndex, _subVolumeIndex);
                    StateGuard guard(SystemState::instance());
                    SystemState::instance().data().tripControl.manualSplitIndex = _subVolumeIndex;
                }
            }
            if (!_subStream.isOpen) {
                char relW[48], relC[48], trkTitle[48];
                if (_subVolumeIndex <= 1U) {
                    snprintf(relW, sizeof(relW), "sub/W-sub%03u.gpx", _subIndex);
                    snprintf(relC, sizeof(relC), "sub/C-sub%03u.gpx", _subIndex);
                } else {
                    snprintf(relW, sizeof(relW), "sub/W-sub%03u_part%u.gpx", _subIndex, _subVolumeIndex);
                    snprintf(relC, sizeof(relC), "sub/C-sub%03u_part%u.gpx", _subIndex, _subVolumeIndex);
                }
                snprintf(trkTitle, sizeof(trkTitle), "Sub%03u-Part%u", _subIndex, _subVolumeIndex);
                if (openOrAppendDualStreamLocked(_subStream, relW, relC, trkTitle, ptEpoch)) {
                    strncpy(_lastSubWgsRel, relW, sizeof(_lastSubWgsRel) - 1);
                }
            }
            writeTrackPointToStreamLocked(_subStream, snap, ptEpoch, ptMs, gcjLat, gcjLon);
        }

        // Stage 2 Power-Loss Pipeline: Periodic 10s sync()
        if (nowMs - _lastSyncMs >= SdCardConfig::GPX_SYNC_INTERVAL_MS) {
            syncOpenStreamsLocked();
            _lastSyncMs = nowMs;
        }

        // Periodic 30s 50% TF card quota inspection & enforcement
        if (nowMs - _lastQuotaCheckMs >= 30000UL) {
            enforceStorageQuota50PercentLocked();
            _lastQuotaCheckMs = nowMs;
        }
    }

    syncActivePathsToState();
    if (nowMs - _lastQuotaCheckMs < 1100UL) {
        refreshStorageStats();
    }
}

bool TrackLogger::isPathActiveOpen(const char* relPath) const {
    if (!relPath || relPath[0] == '\0') {
        return false;
    }
    const char* cleanRel = relPath;
    if (strncmp(cleanRel, "/tracks/", 8) == 0) {
        cleanRel += 8;
    } else if (cleanRel[0] == '/') {
        cleanRel += 1;
    }
    return (_singleStream.isOpen && (strcmp(cleanRel, _singleStream.relWgsPath) == 0 ||
                                     strcmp(cleanRel, _singleStream.relGcjPath) == 0)) ||
           (_dailyStream.isOpen  && (strcmp(cleanRel, _dailyStream.relWgsPath) == 0 ||
                                     strcmp(cleanRel, _dailyStream.relGcjPath) == 0)) ||
           (_subStream.isOpen    && (strcmp(cleanRel, _subStream.relWgsPath) == 0 ||
                                     strcmp(cleanRel, _subStream.relGcjPath) == 0));
}

void TrackLogger::resolveFullTracksPath(const char* relOrAbsPath, char* outFull, size_t outSize) const {
    if (!relOrAbsPath || relOrAbsPath[0] == '\0') {
        outFull[0] = '\0';
        return;
    }
    // Reject path traversal sequences
    if (strstr(relOrAbsPath, "..") != nullptr) {
        outFull[0] = '\0';
        return;
    }
    if (strncmp(relOrAbsPath, "/tracks/", 8) == 0 || strcmp(relOrAbsPath, "/tracks") == 0) {
        snprintf(outFull, outSize, "%s", relOrAbsPath);
    } else if (relOrAbsPath[0] == '/') {
        snprintf(outFull, outSize, "%s%s", SdCardConfig::DIR_TRACKS_ROOT, relOrAbsPath);
    } else {
        snprintf(outFull, outSize, "%s/%s", SdCardConfig::DIR_TRACKS_ROOT, relOrAbsPath);
    }
}

size_t TrackLogger::listTrackFiles(TrackFileEntry* outList, size_t maxEntries, const char* folderFilter) {
    if (outList == nullptr || maxEntries == 0U) {
        return 0U;
    }
    SdGuard sdLock(_sdMutex, pdMS_TO_TICKS(500));
    if (!sdLock.locked() || !_mounted) {
        return 0U;
    }

    static constexpr const char* kFolders[3] = { "single", "daily", "sub" };
    size_t count = 0U;

    for (size_t i = 0; i < 3 && count < maxEntries; ++i) {
        if (folderFilter != nullptr && folderFilter[0] != '\0' &&
            strcasecmp(folderFilter, kFolders[i]) != 0) {
            continue;
        }
        char dirFull[48];
        snprintf(dirFull, sizeof(dirFull), "%s/%s", SdCardConfig::DIR_TRACKS_ROOT, kFolders[i]);
        FsFile dir = _sd.open(dirFull, O_RDONLY);
        if (!dir.isOpen() || !dir.isDirectory()) {
            continue;
        }
        FsFile f;
        char name[48] = {};
        while (count < maxEntries && f.openNext(&dir, O_RDONLY)) {
            if (!f.isDirectory()) {
                f.getName(name, sizeof(name));
                TrackFileEntry& item = outList[count];
                snprintf(item.relPath, sizeof(item.relPath), "%s/%s", kFolders[i], name);
                item.sizeBytes = f.fileSize();
                f.getModifyDateTime(&item.fatDate, &item.fatTime);
                item.isActive  = isPathActiveOpen(item.relPath);
                count++;
            }
            f.close();
        }
        dir.close();
    }

    // Include trips.csv & power.log when listing all files (folderFilter == nullptr)
    if ((folderFilter == nullptr || folderFilter[0] == '\0') && count < maxEntries) {
        static constexpr const char* kRootFiles[2] = { "trips.csv", "power.log" };
        for (size_t r = 0; r < 2 && count < maxEntries; ++r) {
            char full[48];
            snprintf(full, sizeof(full), "%s/%s", SdCardConfig::DIR_TRACKS_ROOT, kRootFiles[r]);
            FsFile f = _sd.open(full, O_RDONLY);
            if (f.isOpen()) {
                TrackFileEntry& item = outList[count];
                snprintf(item.relPath, sizeof(item.relPath), "%s", kRootFiles[r]);
                item.sizeBytes = f.fileSize();
                f.getModifyDateTime(&item.fatDate, &item.fatTime);
                item.isActive  = false;
                count++;
                f.close();
            }
        }
    }
    return count;
}

bool TrackLogger::getFileSize(const char* relOrAbsPath, uint64_t& outSize) {
    outSize = 0ULL;
    char full[112] = {};
    resolveFullTracksPath(relOrAbsPath, full, sizeof(full));
    if (full[0] == '\0') {
        return false;
    }

    SdGuard sdLock(_sdMutex, pdMS_TO_TICKS(300));
    if (!sdLock.locked() || !_mounted) {
        return false;
    }
    FsFile f = _sd.open(full, O_RDONLY);
    if (!f.isOpen() || f.isDirectory()) {
        return false;
    }
    outSize = f.fileSize();
    f.close();
    return true;
}

int32_t TrackLogger::readFileChunk(const char* relOrAbsPath, uint64_t offset, uint8_t* dstBuf, size_t maxLen) {
    if (dstBuf == nullptr || maxLen == 0U) {
        return -1;
    }
    char full[112] = {};
    resolveFullTracksPath(relOrAbsPath, full, sizeof(full));
    if (full[0] == '\0') {
        return -1;
    }

    SdGuard sdLock(_sdMutex, pdMS_TO_TICKS(500));
    if (!sdLock.locked() || !_mounted) {
        return -1;
    }
    FsFile f = _sd.open(full, O_RDONLY);
    if (!f.isOpen() || f.isDirectory()) {
        return -1;
    }
    const uint64_t fSize = f.fileSize();
    if (offset >= fSize) {
        f.close();
        return 0;
    }
    if (!f.seekSet(offset)) {
        f.close();
        return -1;
    }
    const int n = f.read(dstBuf, maxLen);
    f.close();
    return static_cast<int32_t>(n);
}

bool TrackLogger::deleteTrackOrPair(const char* relPath) {
    if (!relPath || relPath[0] == '\0' || isPathActiveOpen(relPath)) {
        return false;
    }
    char full[112] = {};
    resolveFullTracksPath(relPath, full, sizeof(full));
    if (full[0] == '\0') {
        return false;
    }

    bool ok = false;
    {
        SdGuard sdLock(_sdMutex, pdMS_TO_TICKS(500));
        if (!sdLock.locked() || !_mounted) {
            return false;
        }
        ok = _sd.remove(full);

        // If deleting W-*.gpx or C-*.gpx, also remove its companion stream pair
        char* slash = strrchr(full, '/');
        if (slash != nullptr && (strncmp(slash + 1, "W-", 2) == 0 || strncmp(slash + 1, "C-", 2) == 0)) {
            slash[1] = (slash[1] == 'W') ? 'C' : 'W';
            if (!isPathActiveOpen(full + strlen(SdCardConfig::DIR_TRACKS_ROOT) + 1)) {
                _sd.remove(full);
            }
        }
    }
    refreshStorageStats();
    return ok;
}

bool TrackLogger::deleteAllTracks() {
    {
        SdGuard sdLock(_sdMutex);
        if (!_mounted) {
            return false;
        }
        flushAndCloseAllStreams(false);

        static constexpr const char* kDirs[3] = {
            SdCardConfig::DIR_SINGLE,
            SdCardConfig::DIR_DAILY,
            SdCardConfig::DIR_SUB
        };
        for (size_t i = 0; i < 3; ++i) {
            FsFile dir = _sd.open(kDirs[i], O_RDONLY);
            if (!dir.isOpen() || !dir.isDirectory()) {
                continue;
            }
            FsFile f;
            char name[64] = {};
            char full[112] = {};
            while (f.openNext(&dir, O_RDONLY)) {
                const bool isSub = f.isDirectory();
                f.getName(name, sizeof(name));
                f.close();
                if (!isSub) {
                    snprintf(full, sizeof(full), "%s/%s", kDirs[i], name);
                    _sd.remove(full);
                }
            }
            dir.close();
        }
        _sd.remove(SdCardConfig::FILE_TRIPS_CSV);
        _sd.remove(SdCardConfig::FILE_SYNCED_LEDGER);
        ensureDirectoriesAndLedgerLocked();
    }
    refreshStorageStats();
    appendPowerLog("CLEAR_TRACKS", "All closed GPX tracks and synced ledger cleared");
    return true;
}
