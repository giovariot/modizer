//
//  ChipAudioEngine.swift
//  ModizerMini
//
//  Thin Swift wrapper around the ChiptuneKit C engine. All decoding happens in
//  libxmp / Game_Music_Emu; this file only owns the AVAudioEngine graph and
//  forwards the per-voice snapshots the UI draws.
//

import Foundation
import AVFoundation

/// Metadata of the file that is currently loaded.
struct DecodedTrackInfo: Equatable {
    var title: String
    var format: String
    var system: String
    var channels: Int
    var instruments: Int
    var samples: Int
    var subsongs: Int
    var currentSubsong: Int
    var duration: TimeInterval
    var isConsole: Bool
}

/// One row of the instrument list.
struct VoiceSnapshot: Identifiable, Equatable {
    let id: Int
    let name: String
    let instrument: String
    let note: Int
    let level: Float
    let active: Bool
    let waveform: [Float]
}

final class ChipAudioEngine {
    private var engine: OpaquePointer?
    private let av = AVAudioEngine()
    private var sourceNode: AVAudioSourceNode?

    let sampleRate: Double = 44_100

    init() {
        if let created = ct_engine_create(sampleRate) {
            engine = created
        }
        if let resources = Bundle.main.resourcePath {
            resources.withCString { ct_engine_set_resource_path($0) }
        }
        configureAudioGraph()
    }

    deinit { teardown() }

    /// Stops the audio graph and destroys the C engine. Safe to call more than
    /// once; after this the receiver renders silence.
    func teardown() {
        av.stop()
        if let node = sourceNode {
            av.disconnectNodeOutput(node)
            av.detach(node)
            sourceNode = nil
        }
        if let engine {
            ct_engine_destroy(engine)
            self.engine = nil
        }
    }

    private func configureAudioGraph() {
        guard let engine,
              let format = AVAudioFormat(standardFormatWithSampleRate: sampleRate, channels: 2) else { return }

        // The render block captures the raw C pointer (no ARC work on the audio
        // thread) and pulls planar float frames from the decoder.
        let node = AVAudioSourceNode(format: format) { _, _, frameCount, audioBufferList -> OSStatus in
            let buffers = UnsafeMutableAudioBufferListPointer(audioBufferList)
            let frames = Int32(frameCount)
            guard let left = buffers[0].mData?.assumingMemoryBound(to: Float.self) else { return noErr }
            let right = buffers.count > 1
                ? (buffers[1].mData?.assumingMemoryBound(to: Float.self) ?? left)
                : left
            ct_render_planar(engine, left, right, frames)
            return noErr
        }

        av.attach(node)
        av.connect(node, to: av.mainMixerNode, format: format)
        sourceNode = node
        av.mainMixerNode.outputVolume = 1
        av.prepare()
        try? av.start()
    }

    // MARK: loading

    @discardableResult
    func load(url: URL, subsong: Int = 0) -> DecodedTrackInfo? {
        guard let engine else { return nil }
        var info = CtTrackInfo()
        let ok = url.path.withCString { ct_engine_load(engine, $0, Int32(subsong), &info) }
        guard ok else { return nil }
        return DecodedTrackInfo(cStruct: info)
    }

    func unload() {
        guard let engine else { return }
        ct_engine_unload(engine)
    }

    var isLoaded: Bool { engine.map { ct_engine_is_loaded($0) } ?? false }

    // MARK: transport

    func play() { if let engine { ct_engine_pause(engine, false) } }
    func pause() { if let engine { ct_engine_pause(engine, true) } }
    var isPaused: Bool { engine.map { ct_engine_is_paused($0) } ?? true }

    func seek(to time: TimeInterval) { if let engine { ct_engine_seek(engine, max(0, time)) } }
    func selectSubsong(_ index: Int) { if let engine { ct_engine_select_subsong(engine, Int32(index)) } }
    func setLooping(_ looping: Bool) { if let engine { ct_engine_set_looping(engine, looping) } }

    func setVolume(_ value: Float) { if let engine { ct_engine_set_volume(engine, value) } }

    var currentTime: TimeInterval { engine.map { ct_engine_current_time($0) } ?? 0 }
    var endReached: Bool { engine.map { ct_engine_end_reached($0) } ?? true }
    var canPlay: Bool { engine != nil }

    // MARK: visuals

    var voiceCount: Int { engine.map { Int(ct_voice_count($0)) } ?? 0 }

    func copyMixWaveform(into buffer: inout [Float]) {
        guard let engine, !buffer.isEmpty else { return }
        buffer.withUnsafeMutableBufferPointer { pointer in
            guard let base = pointer.baseAddress else { return }
            _ = ct_engine_copy_waveform(engine, base, Int32(pointer.count))
        }
    }

    func voiceSnapshots(waveformPoints: Int = 64) -> [VoiceSnapshot] {
        guard let engine else { return [] }
        let count = Int(ct_voice_count(engine))
        guard count > 0 else { return [] }
        var result: [VoiceSnapshot] = []
        result.reserveCapacity(count)
        for index in 0..<count {
            var note: Int32 = 0
            var instrument: Int32 = 0
            var level: Float = 0
            var active = false
            ct_voice_state(engine, Int32(index), &note, &instrument, &level, &active)

            var waveform = [Float](repeating: 0, count: waveformPoints)
            waveform.withUnsafeMutableBufferPointer { pointer in
                guard let base = pointer.baseAddress else { return }
                _ = ct_voice_waveform(engine, Int32(index), base, Int32(pointer.count))
            }

            let name = String(cString: ct_voice_name(engine, Int32(index)))
            let instrumentName = String(cString: ct_voice_instrument(engine, Int32(index)))
                .trimmingCharacters(in: .whitespaces)

            result.append(VoiceSnapshot(id: index,
                                        name: name,
                                        instrument: instrumentName,
                                        note: Int(note),
                                        level: level,
                                        active: active,
                                        waveform: waveform))
        }
        return result
    }
}

// MARK: - C struct helpers

/// Reads a fixed-size C char array (imported as a tuple) as a Swift string.
func decodeCString<T>(_ value: T) -> String {
    var copy = value
    return withUnsafePointer(to: &copy) { pointer in
        pointer.withMemoryRebound(to: CChar.self, capacity: MemoryLayout<T>.size) {
            String(cString: $0)
        }
    }
}

private extension DecodedTrackInfo {
    init(cStruct info: CtTrackInfo) {
        title = decodeCString(info.title)
        format = decodeCString(info.format)
        system = decodeCString(info.system)
        channels = Int(info.channels)
        instruments = Int(info.instruments)
        samples = Int(info.samples)
        subsongs = max(1, Int(info.subsongs))
        currentSubsong = Int(info.currentSubsong)
        duration = info.durationMs > 0 ? Double(info.durationMs) / 1000.0 : 0
        isConsole = info.backend == 2 // CtBackend::CT_BACKEND_GME
    }
}
