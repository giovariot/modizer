//
//  AppSettings.swift
//  ModizerMini
//

import SwiftUI
import AppKit

/// User preferences. The accent colour defaults to the icon's magenta and can
/// be overridden with a custom colour or left to the system.
@Observable
final class AppSettings {

    enum AccentChoice: String, CaseIterable, Identifiable {
        case brand, system, custom
        var id: String { rawValue }

        var title: String {
            switch self {
            case .brand: return "Modizer (icona)"
            case .system: return "Sistema"
            case .custom: return "Personalizzato"
            }
        }
    }

    /// The icon's colour, #E5277E.
    static let brandColor = Color(red: 0xE5 / 255.0, green: 0x27 / 255.0, blue: 0x7E / 255.0)

    private let defaults = UserDefaults.standard

    var accentChoice: AccentChoice {
        didSet { defaults.set(accentChoice.rawValue, forKey: Keys.choice) }
    }
    var customRed: Double { didSet { saveCustom() } }
    var customGreen: Double { didSet { saveCustom() } }
    var customBlue: Double { didSet { saveCustom() } }

    /// Ask the system accent to be used for the whole app instead of a fixed one.
    var followSystem: Bool {
        get { accentChoice == .system }
    }

    init() {
        let raw = defaults.string(forKey: Keys.choice) ?? AccentChoice.brand.rawValue
        accentChoice = AccentChoice(rawValue: raw) ?? .brand
        customRed = defaults.object(forKey: Keys.customRed) as? Double ?? 0.478
        customGreen = defaults.object(forKey: Keys.customGreen) as? Double ?? 0.239
        customBlue = defaults.object(forKey: Keys.customBlue) as? Double ?? 1.0
    }

    var customColor: Color {
        get { Color(red: customRed, green: customGreen, blue: customBlue) }
        set {
            let ns = NSColor(newValue).usingColorSpace(.sRGB) ?? .magenta
            customRed = Double(ns.redComponent)
            customGreen = Double(ns.greenComponent)
            customBlue = Double(ns.blueComponent)
        }
    }

    /// The colour used across the interface.
    var accent: Color {
        switch accentChoice {
        case .brand: return Self.brandColor
        case .custom: return customColor
        case .system: return Color(nsColor: NSColor.controlAccentColor)
        }
    }

    private func saveCustom() {
        defaults.set(customRed, forKey: Keys.customRed)
        defaults.set(customGreen, forKey: Keys.customGreen)
        defaults.set(customBlue, forKey: Keys.customBlue)
    }

    private enum Keys {
        static let choice = "accentChoice"
        static let customRed = "accentCustomRed"
        static let customGreen = "accentCustomGreen"
        static let customBlue = "accentCustomBlue"
    }
}
