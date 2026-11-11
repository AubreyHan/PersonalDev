import Foundation

public struct FitParser: Sendable {
    /// Garmin FIT Epoch: 1989-12-31 00:00:00 UTC (Unix Epoch + 631,065,600 seconds)
    public static let garminFitEpochOffset: TimeInterval = 631_065_600

    public init() {}

    /// 解析标准 Garmin FIT 二进制字节流，还原轨迹点、分段 (Lap) 与汇总统计
    public static func parse(fitData: Data, activity: ActivityItem) throws -> ParsedActivityData {
        let bytes = [UInt8](fitData)
        guard bytes.count >= 12 else {
            throw ExportError.invalidFitFile("文件长度不足 12 字节")
        }

        let headerSize = Int(bytes[0])
        guard headerSize == 12 || headerSize == 14, bytes.count >= headerSize else {
            throw ExportError.invalidFitFile("无效的 FIT Header 长度 (\(headerSize))")
        }

        // 校验 ".FIT" 签名 (bytes 8..<12)
        guard bytes[8] == UInt8(ascii: "."),
              bytes[9] == UInt8(ascii: "F"),
              bytes[10] == UInt8(ascii: "I"),
              bytes[11] == UInt8(ascii: "T") else {
            throw ExportError.invalidFitFile("缺少 .FIT 签名头")
        }

        let declaredDataSize = Int(readUInt32LE(bytes, at: 4))
        let maxDataEnd = min(headerSize + declaredDataSize, bytes.count)
        let dataEnd: Int
        if headerSize + declaredDataSize <= bytes.count {
            dataEnd = maxDataEnd
        } else {
            // 若声明长度超出实际但末尾留有 2 字节 CRC，则解析到末尾前 2 字节或末尾
            dataEnd = max(headerSize, bytes.count - 2)
        }

        var definitions: [UInt8: MessageDefinition] = [:]
        var cursor = headerSize
        var lastTimestampRaw: UInt32?

        var parsedRecords: [TrackPoint] = []
        var rawLaps: [RawLapRecord] = []
        var sessionSummary: RawSessionRecord?

        while cursor < dataEnd {
            let recordHeader = bytes[cursor]
            cursor += 1

            if (recordHeader & 0x80) != 0 {
                // Compressed Timestamp Header
                // bit 7 = 1, bits 5-6 = local message type (0..3), bits 0-4 = time offset (0..31)
                let localMesgNum = (recordHeader >> 5) & 0x03
                let timeOffset = UInt32(recordHeader & 0x1F)

                guard let def = definitions[localMesgNum] else {
                    throw ExportError.invalidFitFile("未找到压缩时间戳消息对应的本地消息定义 #\(localMesgNum)")
                }
                guard cursor + def.totalByteSize <= bytes.count else {
                    break
                }

                var tsRaw = lastTimestampRaw
                if let prev = lastTimestampRaw {
                    let prevLow5 = prev & 0x1F
                    if timeOffset >= prevLow5 {
                        tsRaw = (prev & ~0x1F) + timeOffset
                    } else {
                        tsRaw = (prev & ~0x1F) + timeOffset + 0x20
                    }
                    lastTimestampRaw = tsRaw
                }

                processDataMessage(
                    def: def,
                    bytes: bytes,
                    startOffset: cursor,
                    compressedTimestamp: tsRaw,
                    lastTimestampRaw: &lastTimestampRaw,
                    records: &parsedRecords,
                    laps: &rawLaps,
                    session: &sessionSummary
                )

                cursor += def.totalByteSize
            } else if (recordHeader & 0x40) != 0 {
                // Definition Message
                let hasDeveloperData = (recordHeader & 0x20) != 0
                let localMesgNum = recordHeader & 0x0F

                guard cursor + 5 <= bytes.count else {
                    throw ExportError.invalidFitFile("Definition Message 头部截断")
                }

                // cursor + 0: reserved
                let arch = bytes[cursor + 1]
                let isBigEndian = (arch == 1)
                let globalMesgNum = readUInt16(bytes, at: cursor + 2, isBigEndian: isBigEndian)
                let numFields = Int(bytes[cursor + 4])
                cursor += 5

                guard cursor + numFields * 3 <= bytes.count else {
                    throw ExportError.invalidFitFile("Definition Message 字段定义截断")
                }

                var fields: [FieldDefinition] = []
                fields.reserveCapacity(numFields)
                for _ in 0..<numFields {
                    let fieldNum = bytes[cursor]
                    let size = Int(bytes[cursor + 1])
                    let baseType = bytes[cursor + 2]
                    fields.append(FieldDefinition(fieldDefNum: fieldNum, size: size, baseType: baseType))
                    cursor += 3
                }

                var devTotalBytes = 0
                if hasDeveloperData {
                    guard cursor < bytes.count else {
                        throw ExportError.invalidFitFile("Developer Data 字段计数截断")
                    }
                    let numDevFields = Int(bytes[cursor])
                    cursor += 1
                    guard cursor + numDevFields * 3 <= bytes.count else {
                        throw ExportError.invalidFitFile("Developer Data 字段定义截断")
                    }
                    for _ in 0..<numDevFields {
                        let devSize = Int(bytes[cursor + 1])
                        devTotalBytes += devSize
                        cursor += 3
                    }
                }

                let def = MessageDefinition(
                    globalMesgNum: globalMesgNum,
                    isBigEndian: isBigEndian,
                    fields: fields,
                    developerBytesCount: devTotalBytes
                )
                definitions[localMesgNum] = def
            } else {
                // Normal Data Message
                let localMesgNum = recordHeader & 0x0F
                guard let def = definitions[localMesgNum] else {
                    throw ExportError.invalidFitFile("未找到本地消息类型 #\(localMesgNum) 的 Definition Message")
                }
                guard cursor + def.totalByteSize <= bytes.count else {
                    break
                }

                processDataMessage(
                    def: def,
                    bytes: bytes,
                    startOffset: cursor,
                    compressedTimestamp: nil,
                    lastTimestampRaw: &lastTimestampRaw,
                    records: &parsedRecords,
                    laps: &rawLaps,
                    session: &sessionSummary
                )

                cursor += def.totalByteSize
            }
        }

        // 按时间戳升序排列轨迹点
        parsedRecords.sort { $0.timestamp < $1.timestamp }

        let fallbackStartDate = activity.startGMTDate ?? Date(timeIntervalSince1970: garminFitEpochOffset)
        let activityStartTime = sessionSummary?.startTime
            ?? parsedRecords.first?.timestamp
            ?? rawLaps.first?.startTime
            ?? fallbackStartDate

        let computedElapsed: Double
        if let first = parsedRecords.first?.timestamp, let last = parsedRecords.last?.timestamp {
            computedElapsed = max(0, last.timeIntervalSince(first))
        } else {
            computedElapsed = activity.duration ?? 0
        }

        let totalElapsedTime = sessionSummary?.totalElapsedTime
            ?? (computedElapsed > 0 ? computedElapsed : (activity.duration ?? 0))
        let totalTimerTime = sessionSummary?.totalTimerTime ?? totalElapsedTime
        let totalDistance = sessionSummary?.totalDistance
            ?? parsedRecords.last?.distanceMeters
            ?? activity.distance
            ?? 0
        let totalCalories = sessionSummary?.totalCalories

        // 构造 Laps 并将对应时间区间的 TrackPoint 归入 laps
        var finalLaps: [ActivityLap] = []
        if !rawLaps.isEmpty {
            let sortedRawLaps = rawLaps.sorted {
                ($0.startTime ?? activityStartTime) < ($1.startTime ?? activityStartTime)
            }
            for (index, rawLap) in sortedRawLaps.enumerated() {
                let lapStart = rawLap.startTime
                    ?? (index == 0 ? activityStartTime : (sortedRawLaps[index - 1].endTime ?? activityStartTime))
                let lapElapsed = rawLap.totalElapsedTime ?? totalElapsedTime
                let lapTimer = rawLap.totalTimerTime ?? lapElapsed
                let lapEnd: Date
                if let explicitEnd = rawLap.endTime, explicitEnd >= lapStart {
                    lapEnd = explicitEnd
                } else {
                    lapEnd = lapStart.addingTimeInterval(max(0, lapElapsed))
                }

                let isLastLap = (index == sortedRawLaps.count - 1)
                let lapPoints = parsedRecords.filter { pt in
                    if isLastLap {
                        return pt.timestamp >= lapStart && pt.timestamp <= lapEnd.addingTimeInterval(1.0)
                    } else {
                        return pt.timestamp >= lapStart && pt.timestamp < lapEnd
                    }
                }

                let lapDist = rawLap.totalDistance ?? {
                    if let firstDist = lapPoints.first?.distanceMeters, let lastDist = lapPoints.last?.distanceMeters {
                        return max(0, lastDist - firstDist)
                    }
                    return sortedRawLaps.count == 1 ? totalDistance : 0
                }()

                finalLaps.append(
                    ActivityLap(
                        startTime: lapStart,
                        totalElapsedTimeSeconds: lapElapsed,
                        totalTimerTimeSeconds: lapTimer,
                        totalDistanceMeters: lapDist,
                        calories: rawLap.calories,
                        averageHeartRateBpm: rawLap.avgHeartRate,
                        maxHeartRateBpm: rawLap.maxHeartRate,
                        trackPoints: lapPoints
                    )
                )
            }

            // 如果因为时间戳微小偏差导致有轨迹点但全部分段都没匹配上，则在单分段时直接归入全部轨迹点
            if finalLaps.count == 1 && finalLaps[0].trackPoints.isEmpty && !parsedRecords.isEmpty {
                finalLaps[0].trackPoints = parsedRecords
            }
        } else {
            // 若 FIT 中无显式 lap 消息，则用全部 trackPoints 构造 1 个默认 ActivityLap
            let avgHR: UInt8? = {
                let hrs = parsedRecords.compactMap { $0.heartRateBpm.map(Int.init) }
                guard !hrs.isEmpty else { return nil }
                return UInt8(clamping: hrs.reduce(0, +) / hrs.count)
            }()
            let maxHR = parsedRecords.compactMap { $0.heartRateBpm }.max()

            finalLaps = [
                ActivityLap(
                    startTime: activityStartTime,
                    totalElapsedTimeSeconds: totalElapsedTime,
                    totalTimerTimeSeconds: totalTimerTime,
                    totalDistanceMeters: totalDistance,
                    calories: totalCalories,
                    averageHeartRateBpm: avgHR,
                    maxHeartRateBpm: maxHR,
                    trackPoints: parsedRecords
                )
            ]
        }

        return ParsedActivityData(
            activityId: activity.activityId,
            activityName: activity.displayName,
            typeKey: activity.typeKey,
            startTime: activityStartTime,
            totalElapsedTimeSeconds: totalElapsedTime,
            totalTimerTimeSeconds: totalTimerTime,
            totalDistanceMeters: totalDistance,
            totalCalories: totalCalories,
            laps: finalLaps,
            trackPoints: parsedRecords,
            rawFitData: fitData
        )
    }

    /// 供单元测试构造合法的 .fit 二进制及 FIT 合并输出
    /// 生成包含标准 14 字节 FIT Header、Definition/Data Messages (file_id, session, lap, record) 与末尾 16-bit FIT CRC 的合法 .fit 文件
    public static func buildSyntheticFit(records: [TrackPoint], sport: UInt8 = 1) -> Data {
        let sortedRecords = records.sorted { $0.timestamp < $1.timestamp }
        let startTime = sortedRecords.first?.timestamp ?? Date(timeIntervalSince1970: garminFitEpochOffset + 1_000_000_000)
        let endTime = sortedRecords.last?.timestamp ?? startTime
        let totalElapsedSeconds = max(0, endTime.timeIntervalSince(startTime))
        let totalDistanceMeters = sortedRecords.last?.distanceMeters ?? 0

        let hrs = sortedRecords.compactMap { $0.heartRateBpm }
        let avgHR: UInt8 = hrs.isEmpty ? 0xFF : UInt8(clamping: hrs.map(Int.init).reduce(0, +) / hrs.count)
        let maxHR: UInt8 = hrs.max() ?? 0xFF

        let startFitTs = dateToFitTimestamp(startTime)
        let endFitTs = dateToFitTimestamp(endTime)
        let elapsedRaw = UInt32(clamping: Int64((totalElapsedSeconds * 1000.0).rounded()))
        let distanceRaw = UInt32(clamping: Int64((totalDistanceMeters * 100.0).rounded()))

        var payload = Data()

        // 1. file_id (global mesg 0, local mesg 0)
        // Definition: field 0 (type, enum, 1B), field 1 (manufacturer, uint16, 2B), field 4 (time_created, uint32, 4B)
        payload.append(0x40 | 0x00) // Def header, local 0
        payload.append(0x00)        // Reserved
        payload.append(0x00)        // Little Endian
        appendUInt16LE(&payload, 0) // Global mesg num = 0 (file_id)
        payload.append(3)           // 3 fields
        payload.append(contentsOf: [0, 1, 0x00]) // field 0: type (enum)
        payload.append(contentsOf: [1, 2, 0x84]) // field 1: manufacturer (uint16)
        payload.append(contentsOf: [4, 4, 0x86]) // field 4: time_created (uint32)

        // Data for file_id (local 0)
        payload.append(0x00)
        payload.append(4) // type = 4 (activity)
        appendUInt16LE(&payload, 1) // manufacturer = 1 (garmin)
        appendUInt32LE(&payload, startFitTs)

        // 2. record (global mesg 20, local mesg 1)
        // Fields:
        // 253: timestamp (uint32, 4B, 0x86)
        // 0: position_lat (sint32, 4B, 0x85)
        // 1: position_long (sint32, 4B, 0x85)
        // 2: altitude (uint16, 2B, 0x84)
        // 3: heart_rate (uint8, 1B, 0x02)
        // 4: cadence (uint8, 1B, 0x02)
        // 5: distance (uint32, 4B, 0x86)
        // 6: speed (uint16, 2B, 0x84)
        // 7: power (uint16, 2B, 0x84)
        // 13: temperature (sint8, 1B, 0x01)
        payload.append(0x40 | 0x01) // Def header, local 1
        payload.append(0x00)
        payload.append(0x00)        // Little Endian
        appendUInt16LE(&payload, 20) // Global mesg num = 20 (record)
        payload.append(10)          // 10 fields
        payload.append(contentsOf: [253, 4, 0x86])
        payload.append(contentsOf: [0, 4, 0x85])
        payload.append(contentsOf: [1, 4, 0x85])
        payload.append(contentsOf: [2, 2, 0x84])
        payload.append(contentsOf: [3, 1, 0x02])
        payload.append(contentsOf: [4, 1, 0x02])
        payload.append(contentsOf: [5, 4, 0x86])
        payload.append(contentsOf: [6, 2, 0x84])
        payload.append(contentsOf: [7, 2, 0x84])
        payload.append(contentsOf: [13, 1, 0x01])

        for pt in sortedRecords {
            payload.append(0x01) // Data header, local 1
            let ts = dateToFitTimestamp(pt.timestamp)
            appendUInt32LE(&payload, ts)

            let latSemicircles: Int32 = {
                guard let lat = pt.latitudeDegrees, (-90.0...90.0).contains(lat) else {
                    return Int32.max // 0x7FFFFFFF
                }
                return degreesToSemicircles(lat)
            }()
            let lonSemicircles: Int32 = {
                guard let lon = pt.longitudeDegrees, (-180.0...180.0).contains(lon) else {
                    return Int32.max // 0x7FFFFFFF
                }
                return degreesToSemicircles(lon)
            }()
            appendInt32LE(&payload, latSemicircles)
            appendInt32LE(&payload, lonSemicircles)

            let altRaw: UInt16 = {
                guard let alt = pt.altitudeMeters else { return 0xFFFF }
                let scaled = ((alt + 500.0) * 5.0).rounded()
                guard scaled >= 0, scaled <= 65534 else { return 0xFFFF }
                return UInt16(scaled)
            }()
            appendUInt16LE(&payload, altRaw)

            payload.append(pt.heartRateBpm ?? 0xFF)
            payload.append(pt.cadenceRpm ?? 0xFF)

            let distRaw: UInt32 = {
                guard let dist = pt.distanceMeters, dist >= 0 else { return 0xFFFF_FFFF }
                return UInt32(clamping: Int64((dist * 100.0).rounded()))
            }()
            appendUInt32LE(&payload, distRaw)

            let speedRaw: UInt16 = {
                guard let spd = pt.speedMps, spd >= 0 else { return 0xFFFF }
                let scaled = (spd * 1000.0).rounded()
                guard scaled >= 0, scaled <= 65534 else { return 0xFFFF }
                return UInt16(scaled)
            }()
            appendUInt16LE(&payload, speedRaw)

            appendUInt16LE(&payload, pt.powerWatts ?? 0xFFFF)

            let tempRaw: Int8 = pt.temperatureCelsius ?? 0x7F
            payload.append(UInt8(bitPattern: tempRaw))
        }

        // 3. lap (global mesg 19, local mesg 2)
        // Fields:
        // 253: timestamp (uint32, 4B)
        // 2: start_time (uint32, 4B)
        // 7: total_elapsed_time (uint32, scale 1000, 4B)
        // 8: total_timer_time (uint32, scale 1000, 4B)
        // 9: total_distance (uint32, scale 100, 4B)
        // 11: total_calories (uint16, 2B)
        // 15: avg_heart_rate (uint8, 1B)
        // 16: max_heart_rate (uint8, 1B)
        payload.append(0x40 | 0x02) // Def header, local 2
        payload.append(0x00)
        payload.append(0x00)
        appendUInt16LE(&payload, 19) // Global mesg num = 19 (lap)
        payload.append(8)
        payload.append(contentsOf: [253, 4, 0x86])
        payload.append(contentsOf: [2, 4, 0x86])
        payload.append(contentsOf: [7, 4, 0x86])
        payload.append(contentsOf: [8, 4, 0x86])
        payload.append(contentsOf: [9, 4, 0x86])
        payload.append(contentsOf: [11, 2, 0x84])
        payload.append(contentsOf: [15, 1, 0x02])
        payload.append(contentsOf: [16, 1, 0x02])

        payload.append(0x02) // Data header, local 2
        appendUInt32LE(&payload, endFitTs)
        appendUInt32LE(&payload, startFitTs)
        appendUInt32LE(&payload, elapsedRaw)
        appendUInt32LE(&payload, elapsedRaw)
        appendUInt32LE(&payload, distanceRaw)
        appendUInt16LE(&payload, 0xFFFF) // calories optional
        payload.append(avgHR)
        payload.append(maxHR)

        // 4. session (global mesg 18, local mesg 3)
        // Fields:
        // 253: timestamp (uint32, 4B)
        // 2: start_time (uint32, 4B)
        // 5: sport (enum, 1B)
        // 7: total_elapsed_time (uint32, scale 1000, 4B)
        // 8: total_timer_time (uint32, scale 1000, 4B)
        // 9: total_distance (uint32, scale 100, 4B)
        // 11: total_calories (uint16, 2B)
        payload.append(0x40 | 0x03) // Def header, local 3
        payload.append(0x00)
        payload.append(0x00)
        appendUInt16LE(&payload, 18) // Global mesg num = 18 (session)
        payload.append(7)
        payload.append(contentsOf: [253, 4, 0x86])
        payload.append(contentsOf: [2, 4, 0x86])
        payload.append(contentsOf: [5, 1, 0x00])
        payload.append(contentsOf: [7, 4, 0x86])
        payload.append(contentsOf: [8, 4, 0x86])
        payload.append(contentsOf: [9, 4, 0x86])
        payload.append(contentsOf: [11, 2, 0x84])

        payload.append(0x03) // Data header, local 3
        appendUInt32LE(&payload, endFitTs)
        appendUInt32LE(&payload, startFitTs)
        payload.append(sport)
        appendUInt32LE(&payload, elapsedRaw)
        appendUInt32LE(&payload, elapsedRaw)
        appendUInt32LE(&payload, distanceRaw)
        appendUInt16LE(&payload, 0xFFFF)

        // Construct 14-byte FIT Header
        var header = Data()
        header.append(14)   // Header size = 14 bytes
        header.append(0x20) // Protocol version 2.0
        appendUInt16LE(&header, 2113) // Profile version
        appendUInt32LE(&header, UInt32(payload.count))
        header.append(contentsOf: [UInt8(ascii: "."), UInt8(ascii: "F"), UInt8(ascii: "I"), UInt8(ascii: "T")])
        let headerCRC = fitCRC16(header)
        appendUInt16LE(&header, headerCRC)

        var fullFile = Data()
        fullFile.append(header)
        fullFile.append(payload)
        let fileCRC = fitCRC16(fullFile)
        appendUInt16LE(&fullFile, fileCRC)

        return fullFile
    }

    // MARK: - Internal FIT Structures & Parsing Helpers

    private struct FieldDefinition {
        let fieldDefNum: UInt8
        let size: Int
        let baseType: UInt8
    }

    private struct MessageDefinition {
        let globalMesgNum: UInt16
        let isBigEndian: Bool
        let fields: [FieldDefinition]
        let developerBytesCount: Int

        var totalByteSize: Int {
            fields.reduce(0) { $0 + $1.size } + developerBytesCount
        }
    }

    private struct RawLapRecord {
        var timestamp: Date?
        var startTime: Date?
        var totalElapsedTime: Double?
        var totalTimerTime: Double?
        var totalDistance: Double?
        var calories: UInt16?
        var avgHeartRate: UInt8?
        var maxHeartRate: UInt8?

        var endTime: Date? {
            if let start = startTime, let elapsed = totalElapsedTime {
                return start.addingTimeInterval(elapsed)
            }
            return timestamp
        }
    }

    private struct RawSessionRecord {
        var timestamp: Date?
        var startTime: Date?
        var totalElapsedTime: Double?
        var totalTimerTime: Double?
        var totalDistance: Double?
        var totalCalories: UInt16?
    }

    private static func processDataMessage(
        def: MessageDefinition,
        bytes: [UInt8],
        startOffset: Int,
        compressedTimestamp: UInt32?,
        lastTimestampRaw: inout UInt32?,
        records: inout [TrackPoint],
        laps: inout [RawLapRecord],
        session: inout RawSessionRecord?
    ) {
        switch def.globalMesgNum {
        case 20: // record
            var timestampRaw: UInt32? = compressedTimestamp
            var latSemicircles: Int32?
            var lonSemicircles: Int32?
            var standardAltitudeMeters: Double?
            var enhancedAltitudeMeters: Double?
            var heartRate: UInt8?
            var cadence: UInt8?
            var distanceMeters: Double?
            var standardSpeedMps: Double?
            var enhancedSpeedMps: Double?
            var powerWatts: UInt16?
            var temperatureC: Int8?

            var fieldOffset = startOffset
            for field in def.fields {
                defer { fieldOffset += field.size }
                switch field.fieldDefNum {
                case 253: // timestamp (uint32)
                    if let raw = readValidUInt32(bytes, at: fieldOffset, size: field.size, isBigEndian: def.isBigEndian), raw > 0 {
                        timestampRaw = raw
                        lastTimestampRaw = raw
                    }
                case 0: // position_lat (sint32 semicircles)
                    if let raw = readValidInt32(bytes, at: fieldOffset, size: field.size, isBigEndian: def.isBigEndian) {
                        latSemicircles = raw
                    }
                case 1: // position_long (sint32 semicircles)
                    if let raw = readValidInt32(bytes, at: fieldOffset, size: field.size, isBigEndian: def.isBigEndian) {
                        lonSemicircles = raw
                    }
                case 2: // altitude (uint16, scale 5, offset 500)
                    if let raw = readValidUInt16(bytes, at: fieldOffset, size: field.size, isBigEndian: def.isBigEndian) {
                        standardAltitudeMeters = Double(raw) / 5.0 - 500.0
                    }
                case 78: // enhanced_altitude (uint32, scale 5, offset 500)
                    if let raw = readValidUInt32(bytes, at: fieldOffset, size: field.size, isBigEndian: def.isBigEndian) {
                        enhancedAltitudeMeters = Double(raw) / 5.0 - 500.0
                    }
                case 3: // heart_rate (uint8, invalid 0xFF)
                    if let raw = readValidUInt8(bytes, at: fieldOffset, size: field.size) {
                        heartRate = raw
                    }
                case 4: // cadence (uint8, invalid 0xFF)
                    if let raw = readValidUInt8(bytes, at: fieldOffset, size: field.size) {
                        cadence = raw
                    }
                case 5: // distance (uint32, scale 100)
                    if let raw = readValidUInt32(bytes, at: fieldOffset, size: field.size, isBigEndian: def.isBigEndian) {
                        distanceMeters = Double(raw) / 100.0
                    }
                case 6: // speed (uint16, scale 1000)
                    if let raw = readValidUInt16(bytes, at: fieldOffset, size: field.size, isBigEndian: def.isBigEndian) {
                        standardSpeedMps = Double(raw) / 1000.0
                    }
                case 73: // enhanced_speed (uint32, scale 1000)
                    if let raw = readValidUInt32(bytes, at: fieldOffset, size: field.size, isBigEndian: def.isBigEndian) {
                        enhancedSpeedMps = Double(raw) / 1000.0
                    }
                case 7: // power (uint16, invalid 0xFFFF)
                    if let raw = readValidUInt16(bytes, at: fieldOffset, size: field.size, isBigEndian: def.isBigEndian) {
                        powerWatts = raw
                    }
                case 13: // temperature (sint8, invalid 0x7F)
                    if let raw = readValidInt8(bytes, at: fieldOffset, size: field.size) {
                        temperatureC = raw
                    }
                default:
                    break
                }
            }

            let resolvedTimestamp = fitTimestampToDate(timestampRaw ?? lastTimestampRaw ?? 0)
            let latDeg = latSemicircles.map(semicirclesToDegrees)
            let lonDeg = lonSemicircles.map(semicirclesToDegrees)
            let validLat: Double? = (latDeg != nil && lonDeg != nil && (-90.0...90.0).contains(latDeg!)) ? latDeg : nil
            let validLon: Double? = (latDeg != nil && lonDeg != nil && (-180.0...180.0).contains(lonDeg!)) ? lonDeg : nil

            let point = TrackPoint(
                timestamp: resolvedTimestamp,
                latitudeDegrees: validLat,
                longitudeDegrees: validLon,
                altitudeMeters: enhancedAltitudeMeters ?? standardAltitudeMeters,
                distanceMeters: distanceMeters,
                heartRateBpm: heartRate,
                cadenceRpm: cadence,
                powerWatts: powerWatts,
                speedMps: enhancedSpeedMps ?? standardSpeedMps,
                temperatureCelsius: temperatureC
            )
            records.append(point)

        case 19: // lap
            var lap = RawLapRecord()
            if let compressed = compressedTimestamp {
                lap.timestamp = fitTimestampToDate(compressed)
            }
            var fieldOffset = startOffset
            for field in def.fields {
                defer { fieldOffset += field.size }
                switch field.fieldDefNum {
                case 253: // timestamp
                    if let raw = readValidUInt32(bytes, at: fieldOffset, size: field.size, isBigEndian: def.isBigEndian) {
                        lastTimestampRaw = raw
                        lap.timestamp = fitTimestampToDate(raw)
                    }
                case 2: // start_time
                    if let raw = readValidUInt32(bytes, at: fieldOffset, size: field.size, isBigEndian: def.isBigEndian) {
                        lap.startTime = fitTimestampToDate(raw)
                    }
                case 7: // total_elapsed_time (uint32, scale 1000)
                    if let raw = readValidUInt32(bytes, at: fieldOffset, size: field.size, isBigEndian: def.isBigEndian) {
                        lap.totalElapsedTime = Double(raw) / 1000.0
                    }
                case 8: // total_timer_time (uint32, scale 1000)
                    if let raw = readValidUInt32(bytes, at: fieldOffset, size: field.size, isBigEndian: def.isBigEndian) {
                        lap.totalTimerTime = Double(raw) / 1000.0
                    }
                case 9: // total_distance (uint32, scale 100)
                    if let raw = readValidUInt32(bytes, at: fieldOffset, size: field.size, isBigEndian: def.isBigEndian) {
                        lap.totalDistance = Double(raw) / 100.0
                    }
                case 11: // total_calories (uint16)
                    if let raw = readValidUInt16(bytes, at: fieldOffset, size: field.size, isBigEndian: def.isBigEndian) {
                        lap.calories = raw
                    }
                case 15: // avg_heart_rate (uint8)
                    if let raw = readValidUInt8(bytes, at: fieldOffset, size: field.size) {
                        lap.avgHeartRate = raw
                    }
                case 16: // max_heart_rate (uint8)
                    if let raw = readValidUInt8(bytes, at: fieldOffset, size: field.size) {
                        lap.maxHeartRate = raw
                    }
                default:
                    break
                }
            }
            laps.append(lap)

        case 18: // session
            var sess = session ?? RawSessionRecord()
            if let compressed = compressedTimestamp {
                sess.timestamp = fitTimestampToDate(compressed)
            }
            var fieldOffset = startOffset
            for field in def.fields {
                defer { fieldOffset += field.size }
                switch field.fieldDefNum {
                case 253: // timestamp
                    if let raw = readValidUInt32(bytes, at: fieldOffset, size: field.size, isBigEndian: def.isBigEndian) {
                        lastTimestampRaw = raw
                        sess.timestamp = fitTimestampToDate(raw)
                    }
                case 2: // start_time
                    if let raw = readValidUInt32(bytes, at: fieldOffset, size: field.size, isBigEndian: def.isBigEndian) {
                        sess.startTime = fitTimestampToDate(raw)
                    }
                case 7: // total_elapsed_time (uint32, scale 1000)
                    if let raw = readValidUInt32(bytes, at: fieldOffset, size: field.size, isBigEndian: def.isBigEndian) {
                        sess.totalElapsedTime = Double(raw) / 1000.0
                    }
                case 8: // total_timer_time (uint32, scale 1000)
                    if let raw = readValidUInt32(bytes, at: fieldOffset, size: field.size, isBigEndian: def.isBigEndian) {
                        sess.totalTimerTime = Double(raw) / 1000.0
                    }
                case 9: // total_distance (uint32, scale 100)
                    if let raw = readValidUInt32(bytes, at: fieldOffset, size: field.size, isBigEndian: def.isBigEndian) {
                        sess.totalDistance = Double(raw) / 100.0
                    }
                case 11: // total_calories (uint16)
                    if let raw = readValidUInt16(bytes, at: fieldOffset, size: field.size, isBigEndian: def.isBigEndian) {
                        sess.totalCalories = raw
                    }
                default:
                    break
                }
            }
            session = sess

        default:
            // 对其他消息（如 0 file_id），若带 timestamp 253 则更新 lastTimestampRaw
            var fieldOffset = startOffset
            for field in def.fields {
                defer { fieldOffset += field.size }
                if field.fieldDefNum == 253,
                   let raw = readValidUInt32(bytes, at: fieldOffset, size: field.size, isBigEndian: def.isBigEndian) {
                    lastTimestampRaw = raw
                }
            }
        }
    }

    // MARK: - Unit Conversions & Binary Helpers

    public static func semicirclesToDegrees(_ semicircles: Int32) -> Double {
        Double(semicircles) * (180.0 / Double(1 << 31))
    }

    public static func degreesToSemicircles(_ degrees: Double) -> Int32 {
        let ratio = degrees * (Double(1 << 31) / 180.0)
        return Int32(clamping: Int64(ratio.rounded()))
    }

    private static func fitTimestampToDate(_ fitTimestamp: UInt32) -> Date {
        Date(timeIntervalSince1970: garminFitEpochOffset + TimeInterval(fitTimestamp))
    }

    private static func dateToFitTimestamp(_ date: Date) -> UInt32 {
        let delta = max(0, date.timeIntervalSince1970 - garminFitEpochOffset)
        return UInt32(clamping: Int64(delta.rounded()))
    }

    private static func readValidUInt8(_ bytes: [UInt8], at offset: Int, size: Int) -> UInt8? {
        guard size >= 1 else { return nil }
        let val = bytes[offset]
        return val == 0xFF ? nil : val
    }

    private static func readValidInt8(_ bytes: [UInt8], at offset: Int, size: Int) -> Int8? {
        guard size >= 1 else { return nil }
        let val = Int8(bitPattern: bytes[offset])
        return val == 0x7F ? nil : val
    }

    private static func readValidUInt16(_ bytes: [UInt8], at offset: Int, size: Int, isBigEndian: Bool) -> UInt16? {
        guard size >= 2 else { return nil }
        let val = readUInt16(bytes, at: offset, isBigEndian: isBigEndian)
        return val == 0xFFFF ? nil : val
    }

    private static func readValidUInt32(_ bytes: [UInt8], at offset: Int, size: Int, isBigEndian: Bool) -> UInt32? {
        guard size >= 4 else { return nil }
        let val = readUInt32(bytes, at: offset, isBigEndian: isBigEndian)
        return (val == 0xFFFF_FFFF || val == 0x0000_0000) ? nil : val
    }

    private static func readValidInt32(_ bytes: [UInt8], at offset: Int, size: Int, isBigEndian: Bool) -> Int32? {
        guard size >= 4 else { return nil }
        let raw = readUInt32(bytes, at: offset, isBigEndian: isBigEndian)
        let val = Int32(bitPattern: raw)
        return val == 0x7FFF_FFFF ? nil : val
    }

    private static func readUInt16(_ bytes: [UInt8], at offset: Int, isBigEndian: Bool) -> UInt16 {
        if isBigEndian {
            return (UInt16(bytes[offset]) << 8) | UInt16(bytes[offset + 1])
        } else {
            return UInt16(bytes[offset]) | (UInt16(bytes[offset + 1]) << 8)
        }
    }

    private static func readUInt32(_ bytes: [UInt8], at offset: Int, isBigEndian: Bool) -> UInt32 {
        if isBigEndian {
            return (UInt32(bytes[offset]) << 24)
                | (UInt32(bytes[offset + 1]) << 16)
                | (UInt32(bytes[offset + 2]) << 8)
                | UInt32(bytes[offset + 3])
        } else {
            return UInt32(bytes[offset])
                | (UInt32(bytes[offset + 1]) << 8)
                | (UInt32(bytes[offset + 2]) << 16)
                | (UInt32(bytes[offset + 3]) << 24)
        }
    }

    private static func readUInt32LE(_ bytes: [UInt8], at offset: Int) -> UInt32 {
        readUInt32(bytes, at: offset, isBigEndian: false)
    }

    private static func appendUInt16LE(_ data: inout Data, _ value: UInt16) {
        var le = value.littleEndian
        withUnsafeBytes(of: &le) { data.append(contentsOf: $0) }
    }

    private static func appendUInt32LE(_ data: inout Data, _ value: UInt32) {
        var le = value.littleEndian
        withUnsafeBytes(of: &le) { data.append(contentsOf: $0) }
    }

    private static func appendInt32LE(_ data: inout Data, _ value: Int32) {
        var le = value.littleEndian
        withUnsafeBytes(of: &le) { data.append(contentsOf: $0) }
    }

    /// Garmin FIT 16-bit CRC 算法
    public static func fitCRC16(_ data: Data) -> UInt16 {
        let crcTable: [UInt16] = [
            0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
            0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400
        ]
        var crc: UInt16 = 0
        for byte in data {
            // Compute checksum of lower four bits of byte
            var tmp = crcTable[Int(crc & 0xF)]
            crc = (crc >> 4) & 0x0FFF
            crc = crc ^ tmp ^ crcTable[Int(byte & 0xF)]

            // Now compute checksum of upper four bits of byte
            tmp = crcTable[Int(crc & 0xF)]
            crc = (crc >> 4) & 0x0FFF
            crc = crc ^ tmp ^ crcTable[Int((byte >> 4) & 0xF)]
        }
        return crc
    }
}
