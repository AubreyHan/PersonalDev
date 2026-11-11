import Foundation

public struct TrackPoint: Sendable, Equatable {
    public var timestamp: Date
    public var latitudeDegrees: Double?
    public var longitudeDegrees: Double?
    public var altitudeMeters: Double?
    public var distanceMeters: Double?
    public var heartRateBpm: UInt8?
    public var cadenceRpm: UInt8?
    public var powerWatts: UInt16?
    public var speedMps: Double?
    public var temperatureCelsius: Int8?

    public init(
        timestamp: Date,
        latitudeDegrees: Double? = nil,
        longitudeDegrees: Double? = nil,
        altitudeMeters: Double? = nil,
        distanceMeters: Double? = nil,
        heartRateBpm: UInt8? = nil,
        cadenceRpm: UInt8? = nil,
        powerWatts: UInt16? = nil,
        speedMps: Double? = nil,
        temperatureCelsius: Int8? = nil
    ) {
        self.timestamp = timestamp
        self.latitudeDegrees = latitudeDegrees
        self.longitudeDegrees = longitudeDegrees
        self.altitudeMeters = altitudeMeters
        self.distanceMeters = distanceMeters
        self.heartRateBpm = heartRateBpm
        self.cadenceRpm = cadenceRpm
        self.powerWatts = powerWatts
        self.speedMps = speedMps
        self.temperatureCelsius = temperatureCelsius
    }

    public var hasValidGPSCoordinates: Bool {
        guard let lat = latitudeDegrees, let lon = longitudeDegrees else {
            return false
        }
        guard !lat.isNaN, !lon.isNaN else {
            return false
        }
        return (-90.0...90.0).contains(lat) && (-180.0...180.0).contains(lon)
    }
}

public struct ActivityLap: Sendable, Equatable {
    public var startTime: Date
    public var totalElapsedTimeSeconds: Double
    public var totalTimerTimeSeconds: Double
    public var totalDistanceMeters: Double
    public var calories: UInt16?
    public var averageHeartRateBpm: UInt8?
    public var maxHeartRateBpm: UInt8?
    public var trackPoints: [TrackPoint]

    public init(
        startTime: Date,
        totalElapsedTimeSeconds: Double,
        totalTimerTimeSeconds: Double,
        totalDistanceMeters: Double,
        calories: UInt16? = nil,
        averageHeartRateBpm: UInt8? = nil,
        maxHeartRateBpm: UInt8? = nil,
        trackPoints: [TrackPoint] = []
    ) {
        self.startTime = startTime
        self.totalElapsedTimeSeconds = totalElapsedTimeSeconds
        self.totalTimerTimeSeconds = totalTimerTimeSeconds
        self.totalDistanceMeters = totalDistanceMeters
        self.calories = calories
        self.averageHeartRateBpm = averageHeartRateBpm
        self.maxHeartRateBpm = maxHeartRateBpm
        self.trackPoints = trackPoints
    }
}

public struct ParsedActivityData: Sendable, Equatable {
    public var activityId: Int64
    public var activityName: String
    public var typeKey: String
    public var startTime: Date
    public var totalElapsedTimeSeconds: Double
    public var totalTimerTimeSeconds: Double
    public var totalDistanceMeters: Double
    public var totalCalories: UInt16?
    public var laps: [ActivityLap]
    public var trackPoints: [TrackPoint]
    public var rawFitData: Data

    public init(
        activityId: Int64,
        activityName: String,
        typeKey: String,
        startTime: Date,
        totalElapsedTimeSeconds: Double,
        totalTimerTimeSeconds: Double,
        totalDistanceMeters: Double,
        totalCalories: UInt16? = nil,
        laps: [ActivityLap] = [],
        trackPoints: [TrackPoint] = [],
        rawFitData: Data = Data()
    ) {
        self.activityId = activityId
        self.activityName = activityName
        self.typeKey = typeKey
        self.startTime = startTime
        self.totalElapsedTimeSeconds = totalElapsedTimeSeconds
        self.totalTimerTimeSeconds = totalTimerTimeSeconds
        self.totalDistanceMeters = totalDistanceMeters
        self.totalCalories = totalCalories
        self.laps = laps
        self.trackPoints = trackPoints
        self.rawFitData = rawFitData
    }

    public var hasValidGPSCoordinates: Bool {
        if trackPoints.contains(where: { $0.hasValidGPSCoordinates }) {
            return true
        }
        return laps.contains { lap in
            lap.trackPoints.contains(where: { $0.hasValidGPSCoordinates })
        }
    }
}
