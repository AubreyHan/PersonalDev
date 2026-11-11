import Compression
import Foundation

public enum ExportError: LocalizedError, Equatable, Sendable {
    case invalidZipArchive
    case fitFileNotFoundInZip
    case invalidFitFile(String)
    case noActivitiesSelected
    case mismatchedTypesForMerge
    case missingGPSForGPX(activityNames: [String])
    case fileWriteFailed(String)

    public var errorDescription: String? {
        switch self {
        case .invalidZipArchive:
            return "无效或损坏的 ZIP 压缩包数据"
        case .fitFileNotFoundInZip:
            return "ZIP 压缩包中未找到有效的 .fit 活动文件"
        case .invalidFitFile(let reason):
            return "无效的 FIT 文件：\(reason)"
        case .noActivitiesSelected:
            return "未选择任何活动"
        case .mismatchedTypesForMerge:
            return "所选活动的运动类型不一致，无法合并为一个文件"
        case .missingGPSForGPX(let activityNames):
            let joined = activityNames.joined(separator: "、")
            return "所选活动（\(joined)）不含 GPS 轨迹，GPX 格式无法保存无坐标数据点，请改用 TCX 或 FIT 格式导出"
        case .fileWriteFailed(let reason):
            return "文件写入失败：\(reason)"
        }
    }
}

public struct ZipExtractor: Sendable {
    public init() {}

    /// 从 Garmin 下载接口返回的 ZIP 字节流（或已是原始 .FIT 字节流）中提取 .fit 二进制数据
    public static func extractFitData(from zipData: Data) throws -> Data {
        guard !zipData.isEmpty else {
            throw ExportError.invalidZipArchive
        }

        // 若传入的 Data 本身已经是 .FIT 头标识则直接返回
        if isFitHeader(zipData) {
            return zipData
        }

        // 检查 PKZIP 签名 ("PK\x03\x04" 或包含 End of Central Directory "PK\x05\x06")
        let bytes = [UInt8](zipData)
        guard bytes.count >= 4 else {
            throw ExportError.invalidZipArchive
        }

        // 优先通过 Central Directory 解析，回退为顺序扫描 Local File Header
        if let extracted = try extractViaCentralDirectory(bytes: bytes) {
            return extracted
        }

        if let extracted = try extractViaLocalHeaders(bytes: bytes) {
            return extracted
        }

        throw ExportError.fitFileNotFoundInZip
    }

    /// 供单元测试构造包含单个文件的真实 PKZIP 测试包（支持 Deflate / Stored）
    public static func createSingleFileZip(fileName: String, fileData: Data) -> Data {
        let nameBytes = [UInt8](fileName.utf8)
        let uncompressedBytes = [UInt8](fileData)
        let crc = crc32(uncompressedBytes)
        let uncompressedSize = UInt32(uncompressedBytes.count)

        let compressionMethod: UInt16
        let payloadBytes: [UInt8]

        if let deflated = deflateRaw(fileData), deflated.count < fileData.count {
            compressionMethod = 8
            payloadBytes = [UInt8](deflated)
        } else {
            compressionMethod = 0
            payloadBytes = uncompressedBytes
        }

        let compressedSize = UInt32(payloadBytes.count)

        var archive = Data()

        // 1. Local File Header (30 bytes + fileName)
        appendUInt32LE(&archive, 0x04034B50) // PK\x03\x04
        appendUInt16LE(&archive, 20)         // Version needed to extract (2.0)
        appendUInt16LE(&archive, 0)          // General purpose bit flag
        appendUInt16LE(&archive, compressionMethod)
        appendUInt16LE(&archive, 0)          // Last mod file time
        appendUInt16LE(&archive, 0)          // Last mod file date
        appendUInt32LE(&archive, crc)        // CRC-32
        appendUInt32LE(&archive, compressedSize)
        appendUInt32LE(&archive, uncompressedSize)
        appendUInt16LE(&archive, UInt16(nameBytes.count))
        appendUInt16LE(&archive, 0)          // Extra field length
        archive.append(contentsOf: nameBytes)

        // 2. File Data
        archive.append(contentsOf: payloadBytes)

        // 3. Central Directory File Header (46 bytes + fileName)
        let centralDirOffset = UInt32(archive.count)
        appendUInt32LE(&archive, 0x02014B50) // PK\x01\x02
        appendUInt16LE(&archive, 20)         // Version made by
        appendUInt16LE(&archive, 20)         // Version needed to extract
        appendUInt16LE(&archive, 0)          // General purpose bit flag
        appendUInt16LE(&archive, compressionMethod)
        appendUInt16LE(&archive, 0)          // Last mod file time
        appendUInt16LE(&archive, 0)          // Last mod file date
        appendUInt32LE(&archive, crc)        // CRC-32
        appendUInt32LE(&archive, compressedSize)
        appendUInt32LE(&archive, uncompressedSize)
        appendUInt16LE(&archive, UInt16(nameBytes.count))
        appendUInt16LE(&archive, 0)          // Extra field length
        appendUInt16LE(&archive, 0)          // File comment length
        appendUInt16LE(&archive, 0)          // Disk number start
        appendUInt16LE(&archive, 0)          // Internal file attributes
        appendUInt32LE(&archive, 0)          // External file attributes
        appendUInt32LE(&archive, 0)          // Relative offset of local header
        archive.append(contentsOf: nameBytes)

        let centralDirSize = UInt32(archive.count) - centralDirOffset

        // 4. End of Central Directory Record (22 bytes)
        appendUInt32LE(&archive, 0x06054B50) // PK\x05\x06
        appendUInt16LE(&archive, 0)          // Number of this disk
        appendUInt16LE(&archive, 0)          // Disk where central directory starts
        appendUInt16LE(&archive, 1)          // Number of central directory records on this disk
        appendUInt16LE(&archive, 1)          // Total number of central directory records
        appendUInt32LE(&archive, centralDirSize)
        appendUInt32LE(&archive, centralDirOffset)
        appendUInt16LE(&archive, 0)          // Comment length

        return archive
    }

    // MARK: - Private Helpers

    private static func isFitHeader(_ data: Data) -> Bool {
        guard data.count >= 12 else { return false }
        let headerSize = data[data.startIndex]
        guard headerSize == 12 || headerSize == 14 else { return false }
        let sigStart = data.startIndex + 8
        guard data.count >= sigStart + 4 else { return false }
        let sig = data[sigStart..<(sigStart + 4)]
        return sig.elementsEqual([UInt8(ascii: "."), UInt8(ascii: "F"), UInt8(ascii: "I"), UInt8(ascii: "T")])
    }

    private static func extractViaCentralDirectory(bytes: [UInt8]) throws -> Data? {
        guard bytes.count >= 22 else { return nil }

        // 从末尾向前查找 End of Central Directory (PK\x05\x06 = 0x06054b50)
        let maxCommentLength = min(bytes.count - 22, 65535)
        var eocdOffset: Int?
        for backOffset in 0...maxCommentLength {
            let idx = bytes.count - 22 - backOffset
            if readUInt32LE(bytes, at: idx) == 0x06054B50 {
                eocdOffset = idx
                break
            }
        }

        guard let eocd = eocdOffset else { return nil }
        let totalEntries = Int(readUInt16LE(bytes, at: eocd + 10))
        let cdOffset = Int(readUInt32LE(bytes, at: eocd + 16))
        guard cdOffset >= 0, cdOffset < bytes.count else {
            throw ExportError.invalidZipArchive
        }

        var cursor = cdOffset
        var fallbackCandidate: Data?

        for _ in 0..<totalEntries {
            guard cursor + 46 <= bytes.count, readUInt32LE(bytes, at: cursor) == 0x02014B50 else {
                break
            }
            let method = readUInt16LE(bytes, at: cursor + 10)
            let compressedSize = Int(readUInt32LE(bytes, at: cursor + 20))
            let uncompressedSize = Int(readUInt32LE(bytes, at: cursor + 24))
            let nameLen = Int(readUInt16LE(bytes, at: cursor + 28))
            let extraLen = Int(readUInt16LE(bytes, at: cursor + 30))
            let commentLen = Int(readUInt16LE(bytes, at: cursor + 32))
            let localHeaderOffset = Int(readUInt32LE(bytes, at: cursor + 42))

            guard cursor + 46 + nameLen <= bytes.count else {
                throw ExportError.invalidZipArchive
            }
            let nameBytes = Array(bytes[(cursor + 46)..<(cursor + 46 + nameBytesCount(nameLen))])
            let fileName = String(bytes: nameBytes, encoding: .utf8) ?? ""

            cursor += 46 + nameLen + extraLen + commentLen

            // 跳过目录条目
            if fileName.hasSuffix("/") { continue }

            guard localHeaderOffset + 30 <= bytes.count,
                  readUInt32LE(bytes, at: localHeaderOffset) == 0x04034B50 else {
                throw ExportError.invalidZipArchive
            }
            let localNameLen = Int(readUInt16LE(bytes, at: localHeaderOffset + 26))
            let localExtraLen = Int(readUInt16LE(bytes, at: localHeaderOffset + 28))
            let dataStart = localHeaderOffset + 30 + localNameLen + localExtraLen
            let dataEnd = dataStart + compressedSize

            guard dataStart >= 0, dataEnd <= bytes.count else {
                throw ExportError.invalidZipArchive
            }

            let compressedSlice = Data(bytes[dataStart..<dataEnd])
            let decompressed = try decompressEntry(
                data: compressedSlice,
                method: method,
                uncompressedSize: uncompressedSize
            )

            if fileName.lowercased().hasSuffix(".fit") || isFitHeader(decompressed) {
                return decompressed
            }
            if fallbackCandidate == nil && !decompressed.isEmpty {
                fallbackCandidate = decompressed
            }
        }

        if let candidate = fallbackCandidate, isFitHeader(candidate) {
            return candidate
        }
        return nil
    }

    private static func extractViaLocalHeaders(bytes: [UInt8]) throws -> Data? {
        guard bytes.count >= 30, readUInt32LE(bytes, at: 0) == 0x04034B50 else {
            throw ExportError.invalidZipArchive
        }

        var cursor = 0
        var fallbackCandidate: Data?

        while cursor + 30 <= bytes.count {
            let sig = readUInt32LE(bytes, at: cursor)
            if sig != 0x04034B50 {
                break
            }

            let flags = readUInt16LE(bytes, at: cursor + 6)
            // 若使用了 Data Descriptor (bit 3) 且無 Central Directory，则 compressedSize 可能为 0
            if (flags & 0x0008) != 0 {
                break
            }

            let method = readUInt16LE(bytes, at: cursor + 8)
            let compressedSize = Int(readUInt32LE(bytes, at: cursor + 18))
            let uncompressedSize = Int(readUInt32LE(bytes, at: cursor + 22))
            let nameLen = Int(readUInt16LE(bytes, at: cursor + 26))
            let extraLen = Int(readUInt16LE(bytes, at: cursor + 28))

            let headerEnd = cursor + 30 + nameLen + extraLen
            let dataEnd = headerEnd + compressedSize
            guard cursor + 30 + nameLen <= bytes.count, dataEnd <= bytes.count else {
                throw ExportError.invalidZipArchive
            }

            let nameBytes = Array(bytes[(cursor + 30)..<(cursor + 30 + nameLen)])
            let fileName = String(bytes: nameBytes, encoding: .utf8) ?? ""
            let compressedSlice = Data(bytes[headerEnd..<dataEnd])

            cursor = dataEnd

            if fileName.hasSuffix("/") { continue }

            let decompressed = try decompressEntry(
                data: compressedSlice,
                method: method,
                uncompressedSize: uncompressedSize
            )

            if fileName.lowercased().hasSuffix(".fit") || isFitHeader(decompressed) {
                return decompressed
            }
            if fallbackCandidate == nil && !decompressed.isEmpty {
                fallbackCandidate = decompressed
            }
        }

        if let candidate = fallbackCandidate, isFitHeader(candidate) {
            return candidate
        }
        return nil
    }

    private static func nameBytesCount(_ len: Int) -> Int {
        max(0, len)
    }

    private static func decompressEntry(data: Data, method: UInt16, uncompressedSize: Int) throws -> Data {
        switch method {
        case 0: // Stored
            return data
        case 8: // Deflate (raw deflate stream)
            if uncompressedSize == 0 && data.isEmpty {
                return Data()
            }
            let targetCapacity = max(uncompressedSize, data.count * 4, 4096)
            return try inflateRaw(data, expectedSize: targetCapacity)
        default:
            throw ExportError.invalidZipArchive
        }
    }

    private static func inflateRaw(_ data: Data, expectedSize: Int) throws -> Data {
        var capacity = max(expectedSize, 1024)
        let maxCapacity = max(expectedSize * 16, 64 * 1024 * 1024)

        while capacity <= maxCapacity {
            var destination = Data(count: capacity)
            let decodedBytes = data.withUnsafeBytes { srcPtr -> Int in
                guard let srcBase = srcPtr.baseAddress?.assumingMemoryBound(to: UInt8.self) else {
                    return 0
                }
                return destination.withUnsafeMutableBytes { dstPtr -> Int in
                    guard let dstBase = dstPtr.baseAddress?.assumingMemoryBound(to: UInt8.self) else {
                        return 0
                    }
                    return compression_decode_buffer(
                        dstBase,
                        capacity,
                        srcBase,
                        data.count,
                        nil,
                        COMPRESSION_ZLIB
                    )
                }
            }

            if decodedBytes > 0 {
                if decodedBytes < capacity || capacity >= expectedSize {
                    destination.count = decodedBytes
                    return destination
                }
            }
            capacity *= 2
        }

        throw ExportError.invalidZipArchive
    }

    private static func deflateRaw(_ data: Data) -> Data? {
        guard !data.isEmpty else { return nil }
        let capacity = max(data.count + 512, 1024)
        var destination = Data(count: capacity)

        let encodedBytes = data.withUnsafeBytes { srcPtr -> Int in
            guard let srcBase = srcPtr.baseAddress?.assumingMemoryBound(to: UInt8.self) else {
                return 0
            }
            return destination.withUnsafeMutableBytes { dstPtr -> Int in
                guard let dstBase = dstPtr.baseAddress?.assumingMemoryBound(to: UInt8.self) else {
                    return 0
                }
                return compression_encode_buffer(
                    dstBase,
                    capacity,
                    srcBase,
                    data.count,
                    nil,
                    COMPRESSION_ZLIB
                )
            }
        }

        guard encodedBytes > 0 else { return nil }
        destination.count = encodedBytes
        return destination
    }

    // MARK: - Byte & CRC32 Utilities

    private static func readUInt16LE(_ bytes: [UInt8], at offset: Int) -> UInt16 {
        UInt16(bytes[offset]) | (UInt16(bytes[offset + 1]) << 8)
    }

    private static func readUInt32LE(_ bytes: [UInt8], at offset: Int) -> UInt32 {
        UInt32(bytes[offset])
            | (UInt32(bytes[offset + 1]) << 8)
            | (UInt32(bytes[offset + 2]) << 16)
            | (UInt32(bytes[offset + 3]) << 24)
    }

    private static func appendUInt16LE(_ data: inout Data, _ value: UInt16) {
        var le = value.littleEndian
        withUnsafeBytes(of: &le) { data.append(contentsOf: $0) }
    }

    private static func appendUInt32LE(_ data: inout Data, _ value: UInt32) {
        var le = value.littleEndian
        withUnsafeBytes(of: &le) { data.append(contentsOf: $0) }
    }

    private static func crc32(_ bytes: [UInt8]) -> UInt32 {
        var crc: UInt32 = 0xFFFF_FFFF
        for byte in bytes {
            var index = (crc ^ UInt32(byte)) & 0xFF
            for _ in 0..<8 {
                if (index & 1) != 0 {
                    index = (index >> 1) ^ 0xEDB8_8320
                } else {
                    index >>= 1
                }
            }
            crc = (crc >> 8) ^ index
        }
        return crc ^ 0xFFFF_FFFF
    }
}
