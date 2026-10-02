//
//  Formatting.swift
//  ModizerMini
//

import Foundation

enum Formatting {
    static func time(_ seconds: TimeInterval) -> String {
        guard seconds.isFinite, seconds >= 0 else { return "--:--" }
        let total = Int(seconds.rounded())
        return String(format: "%d:%02d", total / 60, total % 60)
    }

    private static let noteNames = ["C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"]

    static func noteName(_ note: Int) -> String {
        guard note > 0, note < 128 else { return "–" }
        return noteNames[note % 12] + String(note / 12 - 1)
    }
}
