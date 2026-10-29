// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "GitHubSync",
    platforms: [
        .macOS(.v13)
    ],
    products: [
        .executable(
            name: "GitHubSync",
            targets: ["GitHubSync"]
        )
    ],
    dependencies: [],
    targets: [
        .executableTarget(
            name: "GitHubSync",
            path: "Sources/GitHubSync"
        )
    ]
)
