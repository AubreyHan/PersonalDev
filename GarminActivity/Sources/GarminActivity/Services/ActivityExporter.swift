import Foundation

public protocol ActivityExporting: Sendable {
    func exportSingle(parsed: ParsedActivityData, activity: ActivityItem, format: ExportFormat, to directoryURL: URL) throws -> URL
    func exportMerged(items: [(activity: ActivityItem, parsed: ParsedActivityData)], format: ExportFormat, to directoryURL: URL) throws -> URL
    func sanitizeFileNameComponent(_ name: String) -> String
}

public struct ActivityExporter: ActivityExporting, Sendable {
    public init() {}

    /// 单条活动导出为指定格式文件
    public func exportSingle(
        parsed: ParsedActivityData,
        activity: ActivityItem,
        format: ExportFormat,
        to directoryURL: URL
    ) throws -> URL {
        // 无 GPS 轨迹保护：当 format == .gpx 时，必须存在合法坐标点
        if format == .gpx && !parsed.hasValidGPSCoordinates {
            throw ExportError.missingGPSForGPX(activityNames: [activity.displayName])
        }

        let safeType = sanitizeFileNameComponent(activity.typeKey)
        let safeName = sanitizeFileNameComponent(activity.displayName)
        let fileName = "\(activity.datePrefix)_\(safeType)_\(safeName)_\(activity.activityId).\(format.fileExtension)"
        let fileURL = directoryURL.appendingPathComponent(fileName, isDirectory: false)

        let outputData: Data
        switch format {
        case .fit:
            if !parsed.rawFitData.isEmpty {
                outputData = parsed.rawFitData
            } else {
                let sport = Self.fitSportCode(for: activity.typeKey)
                outputData = FitParser.buildSyntheticFit(records: parsed.trackPoints, sport: sport)
            }
        case .gpx:
            let sortedPoints = Self.collectSortedPoints(from: parsed)
            let xml = Self.buildGPXXML(
                name: activity.displayName,
                typeKey: activity.typeKey,
                startTime: activity.startGMTDate ?? parsed.startTime,
                segments: [sortedPoints]
            )
            outputData = Data(xml.utf8)
        case .tcx:
            let sortedLaps = Self.normalizedLaps(for: parsed)
            let xml = Self.buildTCXXML(
                sport: Self.tcxSportName(for: activity.typeKey),
                idDate: activity.startGMTDate ?? parsed.startTime,
                laps: sortedLaps
            )
            outputData = Data(xml.utf8)
        }

        try writeData(outputData, to: fileURL)
        return fileURL
    }

    /// 同类型多条活动按开始时间升序合并导出为 1 个文件
    public func exportMerged(
        items: [(activity: ActivityItem, parsed: ParsedActivityData)],
        format: ExportFormat,
        to directoryURL: URL
    ) throws -> URL {
        guard items.count >= 2 else {
            throw ExportError.noActivitiesSelected
        }

        let uniqueTypes = Set(items.map { $0.activity.typeKey })
        guard uniqueTypes.count == 1, let commonTypeKey = items.first?.activity.typeKey else {
            throw ExportError.mismatchedTypesForMerge
        }

        // 严格按开始时间升序排序：按 item.activity.startGMTDate ?? item.parsed.startTime 升序排列
        let sortedItems = items.sorted { lhs, rhs in
            let lhsDate = lhs.activity.startGMTDate ?? lhs.parsed.startTime
            let rhsDate = rhs.activity.startGMTDate ?? rhs.parsed.startTime
            if lhsDate != rhsDate {
                return lhsDate < rhsDate
            }
            return lhs.activity.activityId < rhs.activity.activityId
        }

        // 无 GPS 轨迹保护：当 format == .gpx 时，检查所有待合并活动是否均具备合法 GPS 坐标
        if format == .gpx {
            let missingNames = sortedItems
                .filter { !$0.parsed.hasValidGPSCoordinates }
                .map { $0.activity.displayName }
            if !missingNames.isEmpty {
                throw ExportError.missingGPSForGPX(activityNames: missingNames)
            }
        }

        let earliestPrefix = sortedItems.first!.activity.datePrefix
        let latestPrefix = sortedItems.last!.activity.datePrefix
        let safeType = sanitizeFileNameComponent(commonTypeKey)
        let fileName = "\(earliestPrefix)_至_\(latestPrefix)_\(safeType)_合并(\(sortedItems.count)条).\(format.fileExtension)"
        let fileURL = directoryURL.appendingPathComponent(fileName, isDirectory: false)

        let earliestStartDate = sortedItems.first!.activity.startGMTDate ?? sortedItems.first!.parsed.startTime
        let mergedTitle = "\(commonTypeKey) 合并 (\(sortedItems.count)条)"

        let outputData: Data
        switch format {
        case .gpx:
            let segments: [[TrackPoint]] = sortedItems.map { item in
                Self.collectSortedPoints(from: item.parsed)
            }
            let xml = Self.buildGPXXML(
                name: mergedTitle,
                typeKey: commonTypeKey,
                startTime: earliestStartDate,
                segments: segments
            )
            outputData = Data(xml.utf8)

        case .tcx:
            var mergedLaps: [ActivityLap] = []
            var cumulativeDistanceOffset: Double = 0.0

            for item in sortedItems {
                let itemLaps = Self.normalizedLaps(for: item.parsed)
                var maxDistanceInActivity: Double = 0.0

                for var lap in itemLaps {
                    var adjustedPoints: [TrackPoint] = []
                    adjustedPoints.reserveCapacity(lap.trackPoints.count)

                    for var pt in lap.trackPoints {
                        if let d = pt.distanceMeters {
                            maxDistanceInActivity = max(maxDistanceInActivity, d)
                            pt.distanceMeters = cumulativeDistanceOffset + d
                        }
                        adjustedPoints.append(pt)
                    }
                    lap.trackPoints = adjustedPoints
                    mergedLaps.append(lap)
                }

                let activityDist = max(maxDistanceInActivity, item.parsed.totalDistanceMeters, item.activity.distance ?? 0)
                cumulativeDistanceOffset += activityDist
            }

            mergedLaps.sort { $0.startTime < $1.startTime }

            let xml = Self.buildTCXXML(
                sport: Self.tcxSportName(for: commonTypeKey),
                idDate: earliestStartDate,
                laps: mergedLaps
            )
            outputData = Data(xml.utf8)

        case .fit:
            var mergedPoints: [TrackPoint] = []
            var cumulativeDistanceOffset: Double = 0.0

            for item in sortedItems {
                let points = Self.collectSortedPoints(from: item.parsed)
                var maxDistanceInActivity: Double = 0.0

                for var pt in points {
                    if let d = pt.distanceMeters {
                        maxDistanceInActivity = max(maxDistanceInActivity, d)
                        pt.distanceMeters = cumulativeDistanceOffset + d
                    } else if cumulativeDistanceOffset > 0 {
                        pt.distanceMeters = cumulativeDistanceOffset
                    }
                    mergedPoints.append(pt)
                }

                let activityDist = max(maxDistanceInActivity, item.parsed.totalDistanceMeters, item.activity.distance ?? 0)
                cumulativeDistanceOffset += activityDist
            }

            mergedPoints.sort { $0.timestamp < $1.timestamp }
            let sport = Self.fitSportCode(for: commonTypeKey)
            outputData = FitParser.buildSyntheticFit(records: mergedPoints, sport: sport)
        }

        try writeData(outputData, to: fileURL)
        return fileURL
    }

    /// 过滤并替换文件名中的系统非法字符（`/`, `\`, `:`, `*`, `?`, `"`, `<`, `>`, `|` 及控制字符替换为 `_`）
    public func sanitizeFileNameComponent(_ name: String) -> String {
        let illegalCharacters = CharacterSet(charactersIn: "/\\:*?\"<>|")
            .union(.controlCharacters)
            .union(.newlines)

        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return "unnamed" }

        var sanitized = ""
        sanitized.reserveCapacity(trimmed.count)
        for scalar in trimmed.unicodeScalars {
            if illegalCharacters.contains(scalar) {
                sanitized.append("_")
            } else {
                sanitized.unicodeScalars.append(scalar)
            }
        }

        let collapsed = sanitized.trimmingCharacters(in: CharacterSet(charactersIn: ". "))
        return collapsed.isEmpty ? "unnamed" : collapsed
    }

    // MARK: - Private File & XML Builders

    private func writeData(_ data: Data, to fileURL: URL) throws {
        do {
            try FileManager.default.createDirectory(
                at: fileURL.deletingLastPathComponent(),
                withIntermediateDirectories: true
            )
            try data.write(to: fileURL, options: .atomic)
        } catch {
            throw ExportError.fileWriteFailed(error.localizedDescription)
        }
    }

    private static func collectSortedPoints(from parsed: ParsedActivityData) -> [TrackPoint] {
        if !parsed.trackPoints.isEmpty {
            return parsed.trackPoints.sorted { $0.timestamp < $1.timestamp }
        }
        let fromLaps = parsed.laps.flatMap { $0.trackPoints }
        return fromLaps.sorted { $0.timestamp < $1.timestamp }
    }

    private static func normalizedLaps(for parsed: ParsedActivityData) -> [ActivityLap] {
        if !parsed.laps.isEmpty {
            return parsed.laps
                .sorted { $0.startTime < $1.startTime }
                .map { lap in
                    var copy = lap
                    copy.trackPoints.sort { $0.timestamp < $1.timestamp }
                    return copy
                }
        }
        let sortedPoints = collectSortedPoints(from: parsed)
        return [
            ActivityLap(
                startTime: parsed.startTime,
                totalElapsedTimeSeconds: parsed.totalElapsedTimeSeconds,
                totalTimerTimeSeconds: parsed.totalTimerTimeSeconds,
                totalDistanceMeters: parsed.totalDistanceMeters,
                calories: parsed.totalCalories,
                trackPoints: sortedPoints
            )
        ]
    }

    private static func buildGPXXML(
        name: String,
        typeKey: String,
        startTime: Date,
        segments: [[TrackPoint]]
    ) -> String {
        var lines: [String] = []
        lines.append(#"<?xml version="1.0" encoding="UTF-8"?>"#)
        lines.append(
            #"<gpx creator="GarminActivityExporter" version="1.1" "#
            + #"xmlns="http://www.topografix.com/GPX/1/1" "#
            + #"xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" "#
            + #"xmlns:gpxtpx="http://www.garmin.com/xmlschemas/TrackPointExtension/v1" "#
            + #"xsi:schemaLocation="http://www.topografix.com/GPX/1/1 http://www.topografix.com/GPX/1/1/gpx.xsd http://www.garmin.com/xmlschemas/TrackPointExtension/v1 http://www.garmin.com/xmlschemas/TrackPointExtensionv1.xsd">"#
        )
        lines.append("  <metadata>")
        lines.append("    <time>\(iso8601String(from: startTime))</time>")
        lines.append("  </metadata>")
        lines.append("  <trk>")
        lines.append("    <name>\(escapeXML(name))</name>")
        lines.append("    <type>\(escapeXML(typeKey))</type>")

        for segment in segments {
            let validPoints = segment
                .filter { $0.hasValidGPSCoordinates }
                .sorted { $0.timestamp < $1.timestamp }
            guard !validPoints.isEmpty else { continue }

            lines.append("    <trkseg>")
            for pt in validPoints {
                guard let lat = pt.latitudeDegrees, let lon = pt.longitudeDegrees else { continue }
                let latStr = String(format: "%.7f", lat)
                let lonStr = String(format: "%.7f", lon)
                lines.append(#"      <trkpt lat="\#(latStr)" lon="\#(lonStr)">"#)
                if let ele = pt.altitudeMeters {
                    lines.append(String(format: "        <ele>%.2f</ele>", ele))
                }
                lines.append("        <time>\(iso8601String(from: pt.timestamp))</time>")

                if pt.heartRateBpm != nil || pt.cadenceRpm != nil || pt.temperatureCelsius != nil || pt.speedMps != nil {
                    lines.append("        <extensions>")
                    lines.append("          <gpxtpx:TrackPointExtension>")
                    if let temp = pt.temperatureCelsius {
                        lines.append("            <gpxtpx:atemp>\(temp)</gpxtpx:atemp>")
                    }
                    if let hr = pt.heartRateBpm {
                        lines.append("            <gpxtpx:hr>\(hr)</gpxtpx:hr>")
                    }
                    if let cad = pt.cadenceRpm {
                        lines.append("            <gpxtpx:cad>\(cad)</gpxtpx:cad>")
                    }
                    if let spd = pt.speedMps {
                        lines.append(String(format: "            <gpxtpx:speed>%.3f</gpxtpx:speed>", spd))
                    }
                    lines.append("          </gpxtpx:TrackPointExtension>")
                    lines.append("        </extensions>")
                }

                lines.append("      </trkpt>")
            }
            lines.append("    </trkseg>")
        }

        lines.append("  </trk>")
        lines.append("</gpx>")
        return lines.joined(separator: "\n") + "\n"
    }

    private static func buildTCXXML(
        sport: String,
        idDate: Date,
        laps: [ActivityLap]
    ) -> String {
        var lines: [String] = []
        lines.append(#"<?xml version="1.0" encoding="UTF-8"?>"#)
        lines.append(
            #"<TrainingCenterDatabase "#
            + #"xsi:schemaLocation="http://www.garmin.com/xmlschemas/TrainingCenterDatabase/v2 http://www.garmin.com/xmlschemas/TrainingCenterDatabasev2.xsd" "#
            + #"xmlns:ns5="http://www.garmin.com/xmlschemas/ActivityGoals/v1" "#
            + #"xmlns:ns3="http://www.garmin.com/xmlschemas/ActivityExtension/v2" "#
            + #"xmlns:ns2="http://www.garmin.com/xmlschemas/UserProfile/v2" "#
            + #"xmlns="http://www.garmin.com/xmlschemas/TrainingCenterDatabase/v2" "#
            + #"xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">"#
        )
        lines.append("  <Activities>")
        lines.append(#"    <Activity Sport="\#(escapeXML(sport))">"#)
        lines.append("      <Id>\(iso8601String(from: idDate))</Id>")

        for lap in laps {
            lines.append(#"      <Lap StartTime="\#(iso8601String(from: lap.startTime))">"#)
            lines.append(String(format: "        <TotalTimeSeconds>%.2f</TotalTimeSeconds>", lap.totalTimerTimeSeconds))
            lines.append(String(format: "        <DistanceMeters>%.2f</DistanceMeters>", lap.totalDistanceMeters))
            lines.append("        <Calories>\(lap.calories ?? 0)</Calories>")
            if let avgHR = lap.averageHeartRateBpm {
                lines.append("        <AverageHeartRateBpm><Value>\(avgHR)</Value></AverageHeartRateBpm>")
            }
            if let maxHR = lap.maxHeartRateBpm {
                lines.append("        <MaximumHeartRateBpm><Value>\(maxHR)</Value></MaximumHeartRateBpm>")
            }
            lines.append("        <Intensity>Active</Intensity>")
            lines.append("        <TriggerMethod>Manual</TriggerMethod>")
            lines.append("        <Track>")

            let sortedPoints = lap.trackPoints.sorted { $0.timestamp < $1.timestamp }
            for pt in sortedPoints {
                lines.append("          <Trackpoint>")
                lines.append("            <Time>\(iso8601String(from: pt.timestamp))</Time>")
                if pt.hasValidGPSCoordinates, let lat = pt.latitudeDegrees, let lon = pt.longitudeDegrees {
                    lines.append("            <Position>")
                    lines.append(String(format: "              <LatitudeDegrees>%.7f</LatitudeDegrees>", lat))
                    lines.append(String(format: "              <LongitudeDegrees>%.7f</LongitudeDegrees>", lon))
                    lines.append("            </Position>")
                }
                if let alt = pt.altitudeMeters {
                    lines.append(String(format: "            <AltitudeMeters>%.2f</AltitudeMeters>", alt))
                }
                if let dist = pt.distanceMeters {
                    lines.append(String(format: "            <DistanceMeters>%.2f</DistanceMeters>", dist))
                }
                if let hr = pt.heartRateBpm {
                    lines.append("            <HeartRateBpm><Value>\(hr)</Value></HeartRateBpm>")
                }
                if let cad = pt.cadenceRpm {
                    lines.append("            <Cadence>\(cad)</Cadence>")
                }
                if pt.speedMps != nil || pt.powerWatts != nil {
                    lines.append("            <Extensions>")
                    lines.append("              <ns3:TPX>")
                    if let spd = pt.speedMps {
                        lines.append(String(format: "                <ns3:Speed>%.3f</ns3:Speed>", spd))
                    }
                    if let watts = pt.powerWatts {
                        lines.append("                <ns3:Watts>\(watts)</ns3:Watts>")
                    }
                    lines.append("              </ns3:TPX>")
                    lines.append("            </Extensions>")
                }
                lines.append("          </Trackpoint>")
            }

            lines.append("        </Track>")
            lines.append("      </Lap>")
        }

        lines.append("    </Activity>")
        lines.append("  </Activities>")
        lines.append("</TrainingCenterDatabase>")
        return lines.joined(separator: "\n") + "\n"
    }

    private static func tcxSportName(for typeKey: String) -> String {
        let lower = typeKey.lowercased()
        if lower.contains("run") {
            return "Running"
        } else if lower.contains("cycl") || lower.contains("bik") || lower.contains("ride") {
            return "Biking"
        } else {
            return "Other"
        }
    }

    private static func fitSportCode(for typeKey: String) -> UInt8 {
        let lower = typeKey.lowercased()
        if lower.contains("run") {
            return 1 // running
        } else if lower.contains("cycl") || lower.contains("bik") || lower.contains("ride") {
            return 2 // cycling
        } else if lower.contains("swim") {
            return 5 // swimming
        } else if lower.contains("walk") || lower.contains("hik") {
            return 11 // walking / hiking
        } else {
            return 0 // generic
        }
    }

    private static func iso8601String(from date: Date) -> String {
        let formatter = ISO8601DateFormatter()
        formatter.timeZone = TimeZone(secondsFromGMT: 0)
        formatter.formatOptions = [.withInternetDateTime]
        return formatter.string(from: date)
    }

    private static func escapeXML(_ text: String) -> String {
        var result = text
        result = result.replacingOccurrences(of: "&", with: "&amp;")
        result = result.replacingOccurrences(of: "<", with: "&lt;")
        result = result.replacingOccurrences(of: ">", with: "&gt;")
        result = result.replacingOccurrences(of: "\"", with: "&quot;")
        result = result.replacingOccurrences(of: "'", with: "&apos;")
        return result
    }
}
