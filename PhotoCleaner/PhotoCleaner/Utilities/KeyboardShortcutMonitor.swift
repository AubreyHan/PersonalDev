import AppKit
import SwiftUI

/// Represents all global and workspace keyboard shortcuts defined in SPEC.md §2.4.
public enum ShortcutAction: String, Sendable, Equatable, CaseIterable {
    case toggleQuickReview      // Space: Toggle between Grid and Quick Review
    case exitOverlay            // Esc: Exit Quick Review or Similarity Compare Sheet
    case movePrevious           // ←: Move selection cursor to previous asset
    case moveNext               // →: Move selection cursor to next asset
    case toggleTrash            // Delete / ⌫ / X: Add to or remove from Trash Basket
    case toggleFavorite         // F: Toggle native PhotoKit favorite ❤️
    case playLivePhoto          // L: Trigger Live Photo playback in Quick Review
    case openCompare            // C: Open side-by-side Similarity Compare view
    case toggleInspector        // I: Show / hide Right Inspector column
    case undoTrash              // ⌘Z: Undo last Trash Basket add/remove action
    case commitDeletions        // ⌘Return: Commit all items in Trash Basket to system Recently Deleted
    case previousNonEmptyDay    // ⌘[: Navigate to previous non-empty MonthDay
    case nextPendingDay         // ⌘]: Navigate to next pending / hasNew MonthDay
}

/// Local `NSEvent` keyboard shortcut dispatcher that safely ignores events when a text input control
/// (`NSTextView`, `NSTextField`, `NSSearchField`) is the current `firstResponder`.
@MainActor
public final class KeyboardShortcutMonitor {
    public static let shared = KeyboardShortcutMonitor()

    private var monitorToken: Any?
    public var onAction: ((ShortcutAction) -> Bool)?

    public init(onAction: ((ShortcutAction) -> Bool)? = nil) {
        self.onAction = onAction
    }

    /// Starts monitoring local `.keyDown` events.
    public func start(handler: ((ShortcutAction) -> Bool)? = nil) {
        if let handler {
            self.onAction = handler
        }
        guard monitorToken == nil else { return }

        monitorToken = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
            guard let self else { return event }
            return self.handle(event: event)
        }
    }

    /// Stops monitoring local `.keyDown` events.
    public func stop() {
        if let token = monitorToken {
            NSEvent.removeMonitor(token)
            monitorToken = nil
        }
    }

    /// Processes an incoming `NSEvent` and returns `nil` if the event was consumed by `onAction`,
    /// or the original `NSEvent` if it should continue normal responder-chain propagation.
    public func handle(event: NSEvent) -> NSEvent? {
        guard !Self.isTextInputFirstResponder(in: event.window ?? NSApp?.keyWindow) else {
            return event
        }
        guard let action = Self.parseAction(from: event) else {
            return event
        }
        if let onAction, onAction(action) {
            return nil
        }
        return event
    }

    /// Checks whether the active `firstResponder` in `window` is a text editor (e.g., search field or album title input).
    public static func isTextInputFirstResponder(in window: NSWindow?) -> Bool {
        guard let responder = window?.firstResponder else { return false }
        if responder is NSTextView || responder is NSTextField || responder is NSSearchField {
            return true
        }
        // Field editor check: when an NSTextField/NSSearchField is active, the firstResponder is an NSTextView field editor.
        if let view = responder as? NSView {
            var current: NSView? = view
            while let node = current {
                if node is NSSearchField || node is NSTextField {
                    return true
                }
                current = node.superview
            }
        }
        return false
    }

    /// Pure parser mapping an `NSEvent` to a `ShortcutAction`.
    public static func parseAction(from event: NSEvent) -> ShortcutAction? {
        parseAction(
            keyCode: event.keyCode,
            charactersIgnoringModifiers: event.charactersIgnoringModifiers,
            modifierFlags: event.modifierFlags
        )
    }

    /// Testable pure mapping from key code, unmodified characters, and modifiers to `ShortcutAction`.
    public static func parseAction(
        keyCode: UInt16,
        charactersIgnoringModifiers: String?,
        modifierFlags: NSEvent.ModifierFlags
    ) -> ShortcutAction? {
        let relevantModifiers = modifierFlags.intersection([.command, .option, .control, .shift])
        let lowerChar = charactersIgnoringModifiers?.lowercased() ?? ""

        // Command-modified shortcuts (⌘Z, ⌘Return, ⌘[, ⌘])
        if relevantModifiers == .command {
            if lowerChar == "z" || keyCode == 6 {
                return .undoTrash
            }
            if keyCode == 36 || keyCode == 76 { // Return or Keypad Enter
                return .commitDeletions
            }
            if lowerChar == "[" || keyCode == 33 {
                return .previousNonEmptyDay
            }
            if lowerChar == "]" || keyCode == 30 {
                return .nextPendingDay
            }
            return nil
        }

        // Unmodified single-key shortcuts (ignore if Command/Option/Control are held)
        guard relevantModifiers.intersection([.command, .option, .control]).isEmpty else {
            return nil
        }

        switch keyCode {
        case 49: // Space
            return .toggleQuickReview
        case 53: // Escape
            return .exitOverlay
        case 123: // Left Arrow ←
            return .movePrevious
        case 124: // Right Arrow →
            return .moveNext
        case 51, 117: // Delete (Backspace) / Forward Delete
            return .toggleTrash
        default:
            break
        }

        switch lowerChar {
        case "x":
            return .toggleTrash
        case "f":
            return .toggleFavorite
        case "l":
            return .playLivePhoto
        case "c":
            return .openCompare
        case "i":
            return .toggleInspector
        default:
            return nil
        }
    }
}

/// SwiftUI modifier to attach `KeyboardShortcutMonitor` to a root view.
public struct KeyboardShortcutMonitorModifier: ViewModifier {
    let onAction: (ShortcutAction) -> Bool

    public init(onAction: @escaping (ShortcutAction) -> Bool) {
        self.onAction = onAction
    }

    public func body(content: Content) -> some View {
        content
            .onAppear {
                KeyboardShortcutMonitor.shared.start(handler: onAction)
            }
            .onDisappear {
                KeyboardShortcutMonitor.shared.stop()
            }
    }
}

public extension View {
    /// Installs the local `KeyboardShortcutMonitor` while this view is visible.
    func keyboardShortcutMonitor(onAction: @escaping (ShortcutAction) -> Bool) -> some View {
        modifier(KeyboardShortcutMonitorModifier(onAction: onAction))
    }
}
