//
//  PreviewViewController.swift
//  ModizerMiniQuickLook
//
//  Quick Look preview: press space on a supported file in Finder and this view
//  appears, playing it immediately.
//

import Cocoa
import Quartz
import SwiftUI

final class PreviewViewController: NSViewController, QLPreviewingController {

    private var playback: QuickLookPlayback?
    private var hostingView: NSView?

    override func loadView() {
        view = NSView(frame: NSRect(x: 0, y: 0, width: 560, height: 340))
    }

    func preparePreviewOfFile(at url: URL) async throws {
        let playback = QuickLookPlayback()
        playback.load(url)
        self.playback = playback

        let host = NSHostingView(rootView: QuickLookPreviewView(playback: playback))
        host.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(host)
        NSLayoutConstraint.activate([
            host.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            host.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            host.topAnchor.constraint(equalTo: view.topAnchor),
            host.bottomAnchor.constraint(equalTo: view.bottomAnchor),
        ])
        hostingView = host
    }

    override func viewDidDisappear() {
        super.viewDidDisappear()
        playback?.stop()
    }
}

/// Small playback controller reused by the preview UI.
@Observable
final class QuickLookPlayback {
    private let audio = ChipAudioEngine()
    private var ticker: Timer?

    private(set) var info: DecodedTrackInfo?
    private(set) var waveform: [Float] = Array(repeating: 0, count: 320)
    private(set) var voices: [VoiceSnapshot] = []
    private(set) var isPaused = false
    private(set) var failed = false

    private(set) var displayName = ""

    func load(_ url: URL) {
        displayName = url.deletingPathExtension().lastPathComponent
        guard let decoded = audio.load(url: url) else {
            failed = true
            return
        }
        info = decoded
        audio.setLooping(true)
        audio.play()
        isPaused = false
        startTicker()
    }

    func toggle() {
        isPaused.toggle()
        isPaused ? audio.pause() : audio.play()
    }

    func stop() {
        ticker?.invalidate()
        ticker = nil
        audio.unload()
    }

    private func startTicker() {
        ticker?.invalidate()
        ticker = Timer.scheduledTimer(withTimeInterval: 1.0 / 30.0, repeats: true) { [weak self] _ in
            guard let self else { return }
            self.audio.copyMixWaveform(into: &self.waveform)
            self.voices = self.audio.voiceSnapshots(waveformPoints: 48)
        }
    }
}

// MARK: - UI

private struct QuickLookPreviewView: View {
    let playback: QuickLookPlayback

    var body: some View {
        VStack(spacing: 14) {
            HStack(spacing: 12) {
                Image(systemName: "waveform.circle.fill")
                    .font(.system(size: 34))
                    .symbolRenderingMode(.hierarchical)
                    .foregroundStyle(.tint)

                VStack(alignment: .leading, spacing: 2) {
                    Text(playback.displayName)
                        .font(.title3.weight(.semibold))
                        .lineLimit(1)
                    if let info = playback.info {
                        Text("\(info.format) · \(info.system)")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }

                Spacer()

                Button {
                    playback.toggle()
                } label: {
                    Image(systemName: playback.isPaused ? "play.circle.fill" : "pause.circle.fill")
                        .font(.system(size: 38))
                        .symbolRenderingMode(.hierarchical)
                }
                .buttonStyle(.plain)
                .disabled(playback.failed)
            }

            if playback.failed {
                ContentUnavailableView("Formato non supportato", systemImage: "exclamationmark.triangle")
                    .frame(maxHeight: .infinity)
            } else {
                Oscilloscope(samples: playback.waveform, accent: ChipTheme.brand)
                    .frame(height: 96)

                CompactVoiceList(voices: playback.voices)
                    .frame(maxHeight: .infinity)
            }
        }
        .padding(20)
        .frame(minWidth: 420, minHeight: 260)
    }
}

private struct CompactVoiceList: View {
    let voices: [VoiceSnapshot]

    var body: some View {
        ScrollView {
            LazyVStack(spacing: 4) {
                ForEach(voices) { voice in
                    HStack(spacing: 10) {
                        Text(voice.name)
                            .font(.caption.monospacedDigit())
                            .foregroundStyle(.secondary)
                            .frame(width: 78, alignment: .leading)
                        Text(voice.instrument.isEmpty ? Formatting.noteName(voice.note) : voice.instrument)
                            .font(.caption)
                            .lineLimit(1)
                        Spacer(minLength: 0)
                        MiniWaveform(samples: voice.waveform, active: voice.active, accent: ChipTheme.brand)
                            .frame(width: 110, height: 22)
                    }
                    .padding(.vertical, 2)
                }
            }
        }
    }
}
