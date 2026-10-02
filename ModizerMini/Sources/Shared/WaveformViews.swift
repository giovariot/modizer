//
//  WaveformViews.swift
//  ModizerMini
//
//  Everything is drawn with Canvas, no custom graphics code beyond a polyline.
//  The accent colour is passed in so the views stay usable from both the app
//  and the Quick Look extension.
//

import SwiftUI

/// Brand colour, shared by the app and the extension.
enum ChipTheme {
    /// The colour of the app icon (#E5277E).
    static let brand = Color(red: 0xE5 / 255.0, green: 0x27 / 255.0, blue: 0x7E / 255.0)
}

/// Large oscilloscope of the mixed output.
struct Oscilloscope: View {
    let samples: [Float]
    var accent: Color = ChipTheme.brand
    var lineWidth: CGFloat = 1.6

    var body: some View {
        Canvas { context, size in
            guard samples.count > 1 else { return }
            let mid = size.height / 2
            let step = size.width / CGFloat(samples.count - 1)
            let amplitude = mid * 0.92

            var path = Path()
            for (index, sample) in samples.enumerated() {
                let clamped = CGFloat(max(-1, min(1, sample)))
                let point = CGPoint(x: CGFloat(index) * step, y: mid - clamped * amplitude)
                index == 0 ? path.move(to: point) : path.addLine(to: point)
            }
            context.stroke(path, with: .color(accent.opacity(0.9)), lineWidth: lineWidth)
        }
        .drawingGroup()
    }
}

/// Tiny per-instrument oscilloscope shown in every instrument row.
struct MiniWaveform: View {
    let samples: [Float]
    var active: Bool
    var accent: Color = ChipTheme.brand

    var body: some View {
        Canvas { context, size in
            guard samples.count > 1 else { return }
            let mid = size.height / 2
            let step = size.width / CGFloat(samples.count - 1)
            let amplitude = mid * 0.9

            var path = Path()
            for (index, sample) in samples.enumerated() {
                let clamped = CGFloat(max(-1, min(1, sample)))
                let point = CGPoint(x: CGFloat(index) * step, y: mid - clamped * amplitude)
                index == 0 ? path.move(to: point) : path.addLine(to: point)
            }
            let color: Color = active ? accent : .secondary.opacity(0.35)
            context.stroke(path, with: .color(color), lineWidth: 1)
        }
    }
}

/// Simple level meter used next to the instrument name.
struct LevelBar: View {
    let level: Float
    var active: Bool
    var accent: Color = ChipTheme.brand

    var body: some View {
        GeometryReader { geometry in
            ZStack(alignment: .leading) {
                Capsule().fill(.quaternary)
                Capsule()
                    .fill(active ? accent : Color.secondary.opacity(0.4))
                    .frame(width: max(2, geometry.size.width * CGFloat(max(0, min(1, level)))))
            }
        }
        .frame(height: 4)
    }
}
