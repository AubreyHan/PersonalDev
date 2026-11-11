// swift-tools-version: 5.10
import PackageDescription

let package = Package(
    name: "GarminActivity",
    platforms: [
        .macOS(.v14)
    ],
    products: [
        .executable(
            name: "GarminActivity",
            targets: ["GarminActivity"]
        )
    ],
    targets: [
        .executableTarget(
            name: "GarminActivity",
            path: "Sources/GarminActivity"
        ),
        .testTarget(
            name: "GarminActivityTests",
            dependencies: ["GarminActivity"],
            path: "Tests/GarminActivityTests"
        )
    ]
)
