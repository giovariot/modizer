//
//  PlayerModel.swift
//  ModizerMini
//

import Foundation
import SwiftUI
import UniformTypeIdentifiers

struct Track: Identifiable, Hashable {
    let id = UUID()
    let url: URL

    var displayName: String { url.deletingPathExtension().lastPathComponent }
    var fileExtension: String { url.pathExtension.uppercased() }
}

enum RepeatMode: Int, CaseIterable {
    case off, all, one

    var systemImage: String {
        switch self {
        case .off, .all: return "repeat"
        case .one: return "repeat.1"
        }
    }

    var help: String {
        switch self {
        case .off: return "Ripetizione disattivata"
        case .all: return "Ripeti tutta la playlist"
        case .one: return "Ripeti il brano"
        }
    }

    mutating func advance() { self = RepeatMode(rawValue: (rawValue + 1) % RepeatMode.allCases.count) ?? .off }
}

@Observable
final class PlayerModel {

    // MARK: playlist

    var tracks: [Track] = []
    var selection: Track.ID?

    // MARK: playback state

    private(set) var currentTrackID: Track.ID?
    private(set) var info: DecodedTrackInfo?
    private(set) var voices: [VoiceSnapshot] = []
    private(set) var mixWaveform: [Float] = Array(repeating: 0, count: 512)
    private(set) var currentTime: TimeInterval = 0
    private(set) var isPlaying = false
    private(set) var isPaused = false
    private(set) var errorMessage: String?

    var shuffle = false
    var repeatMode: RepeatMode = .off {
        didSet { audio.setLooping(repeatMode == .one) }
    }
    var volume: Double = 1.0 {
        didSet { audio.setVolume(Float(volume)) }
    }

    // MARK: private

    private let audio = ChipAudioEngine()
    private var currentIndex: Int?
    private var history: [Int] = []
    private var ticker: Timer?

    private let mixWaveformPoints = 512
    private let voiceWaveformPoints = 64

    static let supportedExtensions: [String] = {
        String(cString: ct_supported_extensions())
            .split(separator: ";")
            .map(String.init)
    }()

    static let supportedContentTypes: [UTType] = {
        supportedExtensions.compactMap { UTType(filenameExtension: $0) }
    }()

    static func isPlayable(_ url: URL) -> Bool {
        url.path.withCString { ct_can_play($0) }
    }

    // MARK: derived state

    var currentTrack: Track? {
        guard let currentTrackID else { return nil }
        return tracks.first { $0.id == currentTrackID }
    }

    var duration: TimeInterval { info?.duration ?? 0 }

    var progress: Double {
        guard duration > 0 else { return 0 }
        return min(1, max(0, currentTime.truncatingRemainder(dividingBy: duration) / duration))
    }

    // MARK: playlist editing

    func addFiles(_ urls: [URL]) {
        let playable = urls.filter { Self.isPlayable($0) }
        guard !playable.isEmpty else {
            if !urls.isEmpty { errorMessage = "Nessun file supportato tra quelli selezionati." }
            return
        }
        let existing = Set(tracks.map { $0.url.standardizedFileURL })
        for url in playable where !existing.contains(url.standardizedFileURL) {
            tracks.append(Track(url: url))
        }
    }

    func remove(at offsets: IndexSet) {
        tracks.remove(atOffsets: offsets)
        clampCurrentIndex()
    }

    func move(from source: IndexSet, to destination: Int) {
        tracks.move(fromOffsets: source, toOffset: destination)
    }

    func clear() {
        stop()
        tracks.removeAll()
        currentTrackID = nil
        currentIndex = nil
    }

    func open(_ url: URL) {
        addFiles([url])
        if let track = tracks.first(where: { $0.url.standardizedFileURL == url.standardizedFileURL }) {
            play(track)
        }
    }

    // MARK: transport

    func play(_ track: Track) {
        guard let index = tracks.firstIndex(of: track) else { return }
        startPlayback(at: index, recordHistory: true)
    }

    func togglePlayPause() {
        guard isPlaying else {
            if let index = currentIndex, tracks.indices.contains(index) {
                startPlayback(at: index, recordHistory: false)
            } else if let first = tracks.first {
                play(first)
            }
            return
        }
        if isPaused {
            audio.play()
            isPaused = false
        } else {
            audio.pause()
            isPaused = true
        }
    }

    func stop() {
        audio.unload()
        ticker?.invalidate()
        ticker = nil
        isPlaying = false
        isPaused = false
        info = nil
        voices = []
        currentTime = 0
        mixWaveform = Array(repeating: 0, count: mixWaveformPoints)
    }

    func next() { advance(automatic: false) }

    func previous() {
        guard !tracks.isEmpty else { return }
        if shuffle, let last = history.popLast(), tracks.indices.contains(last) {
            startPlayback(at: last, recordHistory: false)
            return
        }
        let index = currentIndex ?? 0
        startPlayback(at: index > 0 ? index - 1 : tracks.count - 1, recordHistory: false)
    }

    func seek(to time: TimeInterval) {
        guard isPlaying else { return }
        audio.seek(to: time)
        currentTime = time
    }

    func selectSubsong(_ index: Int) {
        guard isPlaying else { return }
        audio.selectSubsong(index)
        currentTime = 0
    }

    func dismissError() { errorMessage = nil }

    // MARK: internals

    private func startPlayback(at index: Int, recordHistory: Bool) {
        guard tracks.indices.contains(index) else { return }
        let track = tracks[index]
        guard let decoded = audio.load(url: track.url) else {
            errorMessage = "Impossibile riprodurre \"\(track.displayName)\"."
            return
        }
        if recordHistory, shuffle, let previous = currentIndex, previous != index {
            history.append(previous)
        }
        currentIndex = index
        currentTrackID = track.id
        selection = track.id
        info = decoded
        currentTime = 0
        isPlaying = true
        isPaused = false
        audio.setLooping(repeatMode == .one)
        audio.setVolume(Float(volume))
        audio.play()
        refreshSnapshots()
        startTicker()
    }

    private func advance(automatic: Bool) {
        guard !tracks.isEmpty else { return }
        let index = currentIndex ?? -1

        if shuffle, tracks.count > 1 {
            var candidate = index
            while candidate == index { candidate = Int.random(in: 0..<tracks.count) }
            startPlayback(at: candidate, recordHistory: true)
            return
        }

        var next = index + 1
        if next >= tracks.count {
            if automatic && repeatMode == .off {
                stop()
                return
            }
            next = 0
        }
        startPlayback(at: next, recordHistory: true)
    }

    private func clampCurrentIndex() {
        guard let currentIndex else { return }
        if tracks.isEmpty {
            stop()
        } else if currentIndex >= tracks.count {
            startPlayback(at: tracks.count - 1, recordHistory: false)
        }
    }

    private func startTicker() {
        ticker?.invalidate()
        ticker = Timer.scheduledTimer(withTimeInterval: 1.0 / 30.0, repeats: true) { [weak self] _ in
            self?.tick()
        }
    }

    private func tick() {
        guard isPlaying else { return }
        currentTime = audio.currentTime
        refreshSnapshots()
        if audio.endReached {
            advance(automatic: true)
        }
    }

    private func refreshSnapshots() {
        audio.copyMixWaveform(into: &mixWaveform)
        voices = audio.voiceSnapshots(waveformPoints: voiceWaveformPoints)
    }
}
