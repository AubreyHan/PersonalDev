import Foundation
import XCTest
@testable import GarminActivity

private final class MockURLProtocol: URLProtocol {
    typealias RequestHandler = @Sendable (URLRequest) throws -> (HTTPURLResponse, Data)

    private static let lock = NSLock()
    private static var requestHandler: RequestHandler?
    private static var recordedRequests: [URLRequest] = []

    static func setHandler(_ handler: @escaping RequestHandler) {
        lock.lock()
        defer { lock.unlock() }
        recordedRequests.removeAll()
        requestHandler = handler
    }

    static func reset() {
        lock.lock()
        defer { lock.unlock() }
        recordedRequests.removeAll()
        requestHandler = nil
    }

    static func getRecordedRequests() -> [URLRequest] {
        lock.lock()
        defer { lock.unlock() }
        return recordedRequests
    }

    override class func canInit(with request: URLRequest) -> Bool {
        true
    }

    override class func canonicalRequest(for request: URLRequest) -> URLRequest {
        request
    }

    override func startLoading() {
        MockURLProtocol.lock.lock()
        MockURLProtocol.recordedRequests.append(request)
        let handler = MockURLProtocol.requestHandler
        MockURLProtocol.lock.unlock()

        guard let handler = handler else {
            client?.urlProtocol(self, didFailWithError: URLError(.badServerResponse))
            return
        }

        do {
            let (response, data) = try handler(request)
            client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
            client?.urlProtocol(self, didLoad: data)
            client?.urlProtocolDidFinishLoading(self)
        } catch {
            client?.urlProtocol(self, didFailWithError: error)
        }
    }

    override func stopLoading() {}
}

final class AuthAndApiTests: XCTestCase {

    override func tearDown() {
        MockURLProtocol.reset()
        super.tearDown()
    }

    // MARK: - Helpers

    private func makeMockSession() -> URLSession {
        let config = URLSessionConfiguration.ephemeral
        config.protocolClasses = [MockURLProtocol.self]
        return URLSession(configuration: config)
    }

    private func makeJWT(exp: Int? = nil) -> String {
        let headerJSON = #"{"alg":"HS256","typ":"JWT"}"#
        let payloadJSON: String
        if let exp = exp {
            payloadJSON = #"{"sub":"garmin-user","exp":\#(exp)}"#
        } else {
            payloadJSON = #"{"sub":"garmin-user"}"#
        }
        return "\(base64URLEncode(Data(headerJSON.utf8))).\(base64URLEncode(Data(payloadJSON.utf8))).mock_signature"
    }

    private func base64URLEncode(_ data: Data) -> String {
        data.base64EncodedString()
            .replacingOccurrences(of: "+", with: "-")
            .replacingOccurrences(of: "/", with: "_")
            .replacingOccurrences(of: "=", with: "")
    }

    // MARK: - 1. Auto Region Detection & Invalid API Key Tests

    func testAuthenticateDetectsGlobalRegion() async throws {
        let expectedExp = Int(Date().addingTimeInterval(7200).timeIntervalSince1970)
        let jwt = makeJWT(exp: expectedExp)

        MockURLProtocol.setHandler { request in
            let url = try XCTUnwrap(request.url)
            XCTAssertEqual(request.httpMethod, "POST")
            XCTAssertEqual(request.value(forHTTPHeaderField: "Authorization"), "ApiKey test-global-key")
            XCTAssertEqual(request.value(forHTTPHeaderField: "Accept"), "*/*")
            XCTAssertEqual(url.path, "/personalapitoken-service/personal-api/personal-api-key/token")

            if url.host == "personal.connectapi.garmin.com" {
                let response = HTTPURLResponse(url: url, statusCode: 200, httpVersion: nil, headerFields: nil)!
                return (response, Data(jwt.utf8))
            } else {
                let response = HTTPURLResponse(url: url, statusCode: 401, httpVersion: nil, headerFields: nil)!
                return (response, Data())
            }
        }

        let authService = PersonalApiAuthService(urlSession: makeMockSession())
        let session = try await authService.authenticateAndDetectRegion(apiKey: "  test-global-key \n")

        XCTAssertEqual(session.apiKey, "test-global-key")
        XCTAssertEqual(session.region, .global)
        XCTAssertEqual(session.accessToken, jwt)
        XCTAssertEqual(Int(session.expiresAt.timeIntervalSince1970), expectedExp)
    }

    func testAuthenticateDetectsChinaRegionWhenGlobalReturns401() async throws {
        let expectedExp = Int(Date().addingTimeInterval(10800).timeIntervalSince1970)
        let jwt = makeJWT(exp: expectedExp)

        MockURLProtocol.setHandler { request in
            let url = try XCTUnwrap(request.url)
            if url.host == "personal.connectapi.garmin.com" {
                let response = HTTPURLResponse(url: url, statusCode: 401, httpVersion: nil, headerFields: nil)!
                return (response, Data("Unauthorized".utf8))
            } else if url.host == "personal.connectapi.garmin.cn" {
                let response = HTTPURLResponse(url: url, statusCode: 200, httpVersion: nil, headerFields: nil)!
                return (response, Data(jwt.utf8))
            }
            XCTFail("Unexpected host: \(url.host ?? "nil")")
            throw URLError(.badURL)
        }

        let authService = PersonalApiAuthService(urlSession: makeMockSession())
        let session = try await authService.authenticateAndDetectRegion(apiKey: "test-china-key")

        XCTAssertEqual(session.apiKey, "test-china-key")
        XCTAssertEqual(session.region, .china)
        XCTAssertEqual(session.accessToken, jwt)
        XCTAssertEqual(Int(session.expiresAt.timeIntervalSince1970), expectedExp)
    }

    func testAuthenticateThrowsInvalidApiKeyWhenBothRegionsReturn401() async {
        MockURLProtocol.setHandler { request in
            let url = try XCTUnwrap(request.url)
            let response = HTTPURLResponse(url: url, statusCode: 401, httpVersion: nil, headerFields: nil)!
            return (response, Data("Unauthorized".utf8))
        }

        let authService = PersonalApiAuthService(urlSession: makeMockSession())
        do {
            _ = try await authService.authenticateAndDetectRegion(apiKey: "bad-key")
            XCTFail("Expected invalidApiKey error")
        } catch let error as AuthServiceError {
            XCTAssertEqual(error, .invalidApiKey)
        } catch {
            XCTFail("Unexpected error type: \(error)")
        }
    }

    func testAuthenticateThrowsEmptyApiKeyForWhitespaceInput() async {
        let authService = PersonalApiAuthService(urlSession: makeMockSession())
        do {
            _ = try await authService.authenticateAndDetectRegion(apiKey: "   \t\n  ")
            XCTFail("Expected emptyApiKey error")
        } catch let error as AuthServiceError {
            XCTAssertEqual(error, .emptyApiKey)
        } catch {
            XCTFail("Unexpected error type: \(error)")
        }
    }

    // MARK: - 2. JWT exp Parsing, Cache Reuse & Expiration Refresh Tests

    func testObtainValidTokenReusesCachedSessionWhenRemainingTimeExceedsOneHour() async throws {
        let validExp = Int(Date().addingTimeInterval(7200).timeIntervalSince1970) // 2 hours remaining
        let firstJWT = makeJWT(exp: validExp)

        MockURLProtocol.setHandler { request in
            let url = try XCTUnwrap(request.url)
            let response = HTTPURLResponse(url: url, statusCode: 200, httpVersion: nil, headerFields: nil)!
            return (response, Data(firstJWT.utf8))
        }

        let authService = PersonalApiAuthService(urlSession: makeMockSession())
        let initialSession = try await authService.obtainValidToken(
            apiKey: "cache-key",
            region: .global,
            forceRefresh: false
        )
        XCTAssertEqual(MockURLProtocol.getRecordedRequests().count, 1)

        // Second call without forceRefresh should hit memory cache without network request
        let cachedSession = try await authService.obtainValidToken(
            apiKey: "cache-key",
            region: .global,
            forceRefresh: false
        )
        XCTAssertEqual(cachedSession, initialSession)
        XCTAssertEqual(MockURLProtocol.getRecordedRequests().count, 1)
    }

    func testObtainValidTokenRefreshesWhenRemainingTimeIsUnderOneHourOrForced() async throws {
        let nearExpiryExp = Int(Date().addingTimeInterval(1800).timeIntervalSince1970) // 30 mins remaining (< 3600s)
        let refreshedExp = Int(Date().addingTimeInterval(86400).timeIntervalSince1970) // 24 hours remaining
        let nearExpiryJWT = makeJWT(exp: nearExpiryExp)
        let refreshedJWT = makeJWT(exp: refreshedExp)

        let counterLock = NSLock()
        var callCount = 0

        MockURLProtocol.setHandler { request in
            counterLock.lock()
            callCount += 1
            let currentCall = callCount
            counterLock.unlock()

            let url = try XCTUnwrap(request.url)
            let response = HTTPURLResponse(url: url, statusCode: 200, httpVersion: nil, headerFields: nil)!
            let tokenToReturn = (currentCall == 1) ? nearExpiryJWT : refreshedJWT
            return (response, Data(tokenToReturn.utf8))
        }

        let authService = PersonalApiAuthService(urlSession: makeMockSession())

        // 1st call returns token with only 30 mins left
        let session1 = try await authService.obtainValidToken(
            apiKey: "refresh-key",
            region: .china,
            forceRefresh: false
        )
        XCTAssertEqual(session1.accessToken, nearExpiryJWT)
        XCTAssertEqual(MockURLProtocol.getRecordedRequests().count, 1)

        // 2nd call sees <= 3600s remaining and automatically refreshes
        let session2 = try await authService.obtainValidToken(
            apiKey: "refresh-key",
            region: .china,
            forceRefresh: false
        )
        XCTAssertEqual(session2.accessToken, refreshedJWT)
        XCTAssertEqual(MockURLProtocol.getRecordedRequests().count, 2)

        // 3rd call with forceRefresh == true refreshes even though 24h remain
        _ = try await authService.obtainValidToken(
            apiKey: "refresh-key",
            region: .china,
            forceRefresh: true
        )
        XCTAssertEqual(MockURLProtocol.getRecordedRequests().count, 3)
    }

    func testJWTWithoutExpDefaultsTo24Hours() async throws {
        let jwtWithoutExp = makeJWT(exp: nil)
        let beforeRequest = Date()

        MockURLProtocol.setHandler { request in
            let url = try XCTUnwrap(request.url)
            let response = HTTPURLResponse(url: url, statusCode: 200, httpVersion: nil, headerFields: nil)!
            return (response, Data(jwtWithoutExp.utf8))
        }

        let authService = PersonalApiAuthService(urlSession: makeMockSession())
        let session = try await authService.obtainValidToken(apiKey: "no-exp-key", region: .global)

        let expectedMin = beforeRequest.addingTimeInterval(24 * 3600 - 5)
        let expectedMax = Date().addingTimeInterval(24 * 3600 + 5)
        XCTAssertGreaterThanOrEqual(session.expiresAt, expectedMin)
        XCTAssertLessThanOrEqual(session.expiresAt, expectedMax)
    }

    // MARK: - 3. fetchActivities Pagination & startDate Formatting Tests

    func testFetchActivitiesPassesStartDateAndPaginatesAcrossPagesOf99() async throws {
        var components = DateComponents()
        components.year = 2026
        components.month = 11
        components.day = 5
        components.hour = 14
        components.minute = 30
        let startDate = try XCTUnwrap(Calendar.current.date(from: components))

        let page1Items: [[String: Any]] = (1...99).map { idx in
            [
                "activityId": Int64(1000 + idx),
                "activityName": "Run #\(idx)",
                "startTimeLocal": "2026-11-05 08:00:00",
                "startTimeGMT": "2026-11-05 00:00:00",
                "activityType": ["typeId": 1, "typeKey": "running", "parentTypeId": 17],
                "distance": 5000.0,
                "duration": 1500.0
            ]
        }

        // Include 1 duplicate ID (1099) plus 2 new items (2001, 2002) on page 2 (< 99 items -> stops pagination)
        let page2Items: [[String: Any]] = [
            [
                "activityId": Int64(1099),
                "activityName": "Run #99 Duplicate",
                "startTimeLocal": "2026-11-05 08:00:00",
                "activityType": ["typeId": 1, "typeKey": "running"]
            ],
            [
                "activityId": Int64(2001),
                "activityName": "Ride #1",
                "startTimeLocal": "2026-11-06 09:00:00",
                "activityType": ["typeId": 2, "typeKey": "cycling"]
            ],
            [
                "activityId": Int64(2002),
                "activityName": "Ride #2",
                "startTimeLocal": "2026-11-07 09:00:00",
                "activityType": ["typeId": 2, "typeKey": "cycling"]
            ]
        ]

        MockURLProtocol.setHandler { request in
            let url = try XCTUnwrap(request.url)
            XCTAssertEqual(request.httpMethod, "GET")
            XCTAssertEqual(request.value(forHTTPHeaderField: "Authorization"), "Bearer test-access-token")
            XCTAssertEqual(request.value(forHTTPHeaderField: "Accept"), "application/json")
            XCTAssertEqual(url.path, "/activitylist-service/activities/search/activities")

            let urlComponents = try XCTUnwrap(URLComponents(url: url, resolvingAgainstBaseURL: false))
            let queryMap = Dictionary(
                uniqueKeysWithValues: (urlComponents.queryItems ?? []).map { ($0.name, $0.value ?? "") }
            )
            XCTAssertEqual(queryMap["limit"], "99")
            XCTAssertEqual(queryMap["startDate"], "2026-11-05")

            let startParam = queryMap["start"]
            let payload: [[String: Any]]
            if startParam == "0" {
                payload = page1Items
            } else if startParam == "99" {
                payload = page2Items
            } else {
                XCTFail("Unexpected start offset: \(startParam ?? "nil")")
                payload = []
            }

            let data = try JSONSerialization.data(withJSONObject: payload)
            let response = HTTPURLResponse(url: url, statusCode: 200, httpVersion: nil, headerFields: nil)!
            return (response, data)
        }

        let client = GarminApiClient(urlSession: makeMockSession())
        let activities = try await client.fetchActivities(
            from: startDate,
            region: .global,
            accessToken: "test-access-token"
        )

        let recordedRequests = MockURLProtocol.getRecordedRequests()
        XCTAssertEqual(recordedRequests.count, 2)
        XCTAssertEqual(activities.count, 101) // 99 from page 1 + 2 unique from page 2
        XCTAssertEqual(activities.first?.activityId, 1001)
        XCTAssertEqual(activities.last?.activityId, 2002)
    }

    func testFetchActivitiesThrowsUnauthorizedOn401() async {
        MockURLProtocol.setHandler { request in
            let url = try XCTUnwrap(request.url)
            let response = HTTPURLResponse(url: url, statusCode: 401, httpVersion: nil, headerFields: nil)!
            return (response, Data())
        }

        let client = GarminApiClient(urlSession: makeMockSession())
        do {
            _ = try await client.fetchActivities(from: Date(), region: .china, accessToken: "expired-token")
            XCTFail("Expected unauthorized error")
        } catch let error as GarminApiError {
            XCTAssertEqual(error, .unauthorized)
        } catch {
            XCTFail("Unexpected error: \(error)")
        }
    }

    // MARK: - 4. downloadActivityZip Success & Error Handling Tests

    func testDownloadActivityZipReturnsDataOn200() async throws {
        let expectedZipBytes = Data([0x50, 0x4B, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00])

        MockURLProtocol.setHandler { request in
            let url = try XCTUnwrap(request.url)
            XCTAssertEqual(request.httpMethod, "GET")
            XCTAssertEqual(url.host, "personal.connectapi.garmin.cn")
            XCTAssertEqual(url.path, "/download-service/files/activity/16892401234")
            XCTAssertEqual(request.value(forHTTPHeaderField: "Authorization"), "Bearer valid-cn-token")
            XCTAssertEqual(
                request.value(forHTTPHeaderField: "Accept"),
                "application/zip, application/x-zip-compressed, application/octet-stream"
            )

            let response = HTTPURLResponse(url: url, statusCode: 200, httpVersion: nil, headerFields: nil)!
            return (response, expectedZipBytes)
        }

        let client = GarminApiClient(urlSession: makeMockSession())
        let data = try await client.downloadActivityZip(
            activityId: 16892401234,
            region: .china,
            accessToken: "valid-cn-token"
        )
        XCTAssertEqual(data, expectedZipBytes)
    }

    func testDownloadActivityZipThrowsUnauthorizedOn401AndEmptyDownloadOnEmptyBody() async {
        MockURLProtocol.setHandler { request in
            let url = try XCTUnwrap(request.url)
            if url.path.hasSuffix("/401") {
                let response = HTTPURLResponse(url: url, statusCode: 401, httpVersion: nil, headerFields: nil)!
                return (response, Data())
            } else {
                let response = HTTPURLResponse(url: url, statusCode: 200, httpVersion: nil, headerFields: nil)!
                return (response, Data())
            }
        }

        let client = GarminApiClient(urlSession: makeMockSession())

        do {
            _ = try await client.downloadActivityZip(activityId: 401, region: .global, accessToken: "expired")
            XCTFail("Expected unauthorized error")
        } catch let error as GarminApiError {
            XCTAssertEqual(error, .unauthorized)
        } catch {
            XCTFail("Unexpected error: \(error)")
        }

        do {
            _ = try await client.downloadActivityZip(activityId: 200, region: .global, accessToken: "valid")
            XCTFail("Expected emptyDownload error")
        } catch let error as GarminApiError {
            XCTAssertEqual(error, .emptyDownload)
        } catch {
            XCTFail("Unexpected error: \(error)")
        }
    }
}
