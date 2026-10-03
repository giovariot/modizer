//
//  PreviewViewController.swift
//  ModizerMiniQuickLook
//
//  Quick Look preview: pressing space on a supported file in Finder shows this
//  view and plays it. Quick Look reuses the controller when browsing files, so
//  one hosting view and one audio engine are created once and simply reloaded;
//  that avoids tearing the render/view hierarchy down on every file.
//

import Cocoa
import Quartz
import SwiftUI

/// Root view that tells its owner when it is attached to / detached from a window.
final class PreviewRootView: NSView {
    var onWindowChange: ((NSWindow?) -> Void)?
    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        onWindowChange?(window)
    }
}

final class PreviewViewController: NSViewController, QLPreviewingController {

    private let playback = QuickLookPlayback()
    private var hostingView: NSView?
    private var watchdog: Timer?

    override func loadView() {
        let root = PreviewRootView(frame: NSRect(x: 0, y: 0, width: 560, height: 340))
        root.onWindowChange = { [weak self] window in
            // Detached from the window: Quick Look moved on (possibly to a file
            // handled by another preview), so stop playing right away.
            if window == nil { self?.stopEverything() }
        }
        view = root
    }

    func preparePreviewOfFile(at url: URL) async throws {
        if hostingView == nil {
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
        playback.load(url)
    }

    override func viewDidAppear() {
        super.viewDidAppear()
        startWatchdog()
    }

    override func viewWillDisappear() {
        super.viewWillDisappear()
        stopEverything()
    }

    override func viewDidDisappear() {
        super.viewDidDisappear()
        stopEverything()
    }

    deinit {
        watchdog?.invalidate()
    }

    private func stopEverything() {
        playback.stop()
        watchdog?.invalidate()
        watchdog = nil
    }

    /// Quick Look does not always tell the controller when another preview takes
    /// over (e.g. moving to an MP3, which the system previews itself), so poll
    /// whether our view is still the one on screen. A few consecutive failures
    /// are required to avoid stopping during a normal transition.
    private func startWatchdog() {
        guard watchdog == nil else { return }
        var missed = 0
        watchdog = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { [weak self] _ in
            guard let self else { return }
            if self.isPreviewVisible() {
                missed = 0
            } else {
                missed += 1
                if missed >= 2 { self.stopEverything() }
            }
        }
    }

    /// True when our view is attached to a visible window and actually on top
    /// (hit-testing catches the case where another preview covers it).
    private func isPreviewVisible() -> Bool {
        guard let window = view.window, window.isVisible, !view.isHidden, view.superview != nil else {
            return false
        }
        guard let content = window.contentView else { return false }
        let point = view.convert(NSPoint(x: view.bounds.midX, y: view.bounds.midY), to: nil)
        guard let hit = content.hitTest(point) else { return false }
        return hit === view || hit.isDescendant(of: view) || view.isDescendant(of: hit)
    }
}

/// Playback controller shared by the preview UI. One audio engine is reused for
/// every file; loading a new one unloads the previous track.
@Observable
final class QuickLookPlayback {
    /// Every live playback in this process. Quick Look may keep more than one
    /// preview controller around, so starting a file stops the others.
    private static let live = NSHashTable<AnyObject>.weakObjects()

    private let audio = ChipAudioEngine()
    private var ticker: Timer?

    init() {
        QuickLookPlayback.live.add(self)
    }

    private func stopOthers() {
        for case let other as QuickLookPlayback in QuickLookPlayback.live.allObjects where other !== self {
            other.stop()
        }
    }

    private(set) var info: DecodedTrackInfo?
    private(set) var waveform: [Float] = Array(repeating: 0, count: 320)
    private(set) var voices: [VoiceSnapshot] = []
    private(set) var isPaused = false
    private(set) var failed = false

    private(set) var displayName = ""

    func load(_ url: URL) {
        // Stop anything else that may still be playing (other preview
        // controllers the system kept around).
        stopOthers()

        // Stop the current track before switching to the new file.
        ticker?.invalidate()
        ticker = nil
        audio.hardStop()

        displayName = url.deletingPathExtension().lastPathComponent
        failed = false
        info = nil
        voices = []
        waveform = Array(repeating: 0, count: 320)

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
        audio.hardStop()
        isPaused = true
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
                    .foregroundStyle(ChipTheme.brand)

                VStack(alignment: .leading, spacing: 2) {
                    Text(playback.displayName)
                        .font(.title3.weight(.semibold))
                        .lineLimit(1)
                    if let info = playback.info {
                        Text("\(info.format) · \(info.system)")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                        if !info.comment.isEmpty {
                            Text(info.comment.replacingOccurrences(of: "\n", with: " "))
                                .font(.caption2)
                                .foregroundStyle(.tertiary)
                                .lineLimit(1)
                        }
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
        .tint(ChipTheme.brand)
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
                        Text(voice.instrument.isEmpty ? "—" : voice.instrument)
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
