//
//  PlayerView.swift
//  ModizerMini
//

import SwiftUI

struct PlayerView: View {
    @Environment(AppSettings.self) private var settings
    @Bindable var model: PlayerModel

    var body: some View {
        Group {
            if model.currentTrack == nil {
                ContentUnavailableView {
                    Label("Nessun brano in riproduzione", systemImage: "waveform.circle")
                } description: {
                    Text("Scegli un file dalla playlist o trascinalo nella barra laterale.")
                }
            } else {
                loadedContent
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color(nsColor: .windowBackgroundColor))
    }

    private var loadedContent: some View {
        VStack(spacing: 0) {
            TrackInfoHeader(info: model.info, track: model.currentTrack)
                .padding(.horizontal, 24)
                .padding(.top, 20)

            Oscilloscope(samples: model.mixWaveform, accent: settings.accent)
                .frame(height: 130)
                .padding(.horizontal, 24)
                .padding(.vertical, 16)

            VoiceListView(voices: model.voices, isConsole: model.info?.isConsole ?? false)
                .frame(maxHeight: .infinity)

            TransportBar(model: model)
        }
    }
}

// MARK: - Header

private struct TrackInfoHeader: View {
    let info: DecodedTrackInfo?
    let track: Track?

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(alignment: .firstTextBaseline, spacing: 10) {
                Text(track?.displayName ?? "—")
                    .font(.largeTitle.weight(.semibold))
                    .lineLimit(1)
                    .truncationMode(.middle)
                if let info, !info.comment.isEmpty {
                    ModuleInfoButton(info: info)
                }
            }

            if let info {
                HStack(spacing: 6) {
                    Text(info.format)
                    Text("·")
                    Text(info.system)
                    if info.subsongs > 1 {
                        Text("·")
                        Text("Subsong \(info.currentSubsong + 1)/\(info.subsongs)")
                    }
                }
                .font(.subheadline)
                .foregroundStyle(.secondary)

                HStack(spacing: 8) {
                    if info.channels > 0 {
                        StatChip(symbol: "slider.horizontal.3", text: "\(info.channels) canali")
                    }
                    if info.instruments > 0 {
                        StatChip(symbol: "pianokeys", text: "\(info.instruments) strumenti")
                    }
                    if info.samples > 0 {
                        StatChip(symbol: "waveform", text: "\(info.samples) campioni")
                    }
                    if info.title != track?.displayName, !info.title.isEmpty {
                        StatChip(symbol: "textformat", text: info.title)
                    }
                }
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }
}

private struct StatChip: View {
    let symbol: String
    let text: String

    var body: some View {
        Label(text, systemImage: symbol)
            .font(.caption.weight(.medium))
            .labelStyle(.titleAndIcon)
            .padding(.horizontal, 10)
            .padding(.vertical, 4)
            .background(.quaternary, in: Capsule())
    }
}

// MARK: - Module info

private struct ModuleInfoButton: View {
    let info: DecodedTrackInfo
    @State private var showing = false

    /// Some modules store a whole text (credits, greetings) split across the
    /// instrument names; joining the non-empty ones in order rebuilds it.
    private var instrumentText: String {
        let names = info.instrumentNames.filter { !$0.isEmpty }
        guard names.count >= 2 else { return "" }
        return names.joined(separator: " ")
    }

    var body: some View {
        Button {
            showing.toggle()
        } label: {
            Image(systemName: "info.circle")
                .font(.title3)
                .foregroundStyle(.secondary)
        }
        .buttonStyle(.plain)
        .help("Info e commenti del modulo")
        .popover(isPresented: $showing, arrowEdge: .bottom) {
            ScrollView {
                VStack(alignment: .leading, spacing: 10) {
                    Text(info.title.isEmpty ? "Modulo" : info.title)
                        .font(.headline)
                    Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 4) {
                        DetailRow("Formato", info.format)
                        DetailRow("Sistema", info.system)
                        if info.channels > 0 { DetailRow("Canali", "\(info.channels)") }
                        if info.instruments > 0 { DetailRow("Strumenti", "\(info.instruments)") }
                        if info.samples > 0 { DetailRow("Campioni", "\(info.samples)") }
                        if info.subsongs > 1 { DetailRow("Subsong", "\(info.currentSubsong + 1)/\(info.subsongs)") }
                        if info.duration > 0 { DetailRow("Durata", Formatting.time(info.duration)) }
                    }
                    if !info.comment.isEmpty {
                        Divider()
                        Text("Info")
                            .font(.subheadline.weight(.semibold))
                            .foregroundStyle(.secondary)
                        Text(info.comment)
                            .font(.callout)
                            .textSelection(.enabled)
                            .frame(maxWidth: .infinity, alignment: .leading)
                    }
                    if !instrumentText.isEmpty {
                        Divider()
                        Text("Testo dagli strumenti")
                            .font(.subheadline.weight(.semibold))
                            .foregroundStyle(.secondary)
                        Text(instrumentText)
                            .font(.callout)
                            .textSelection(.enabled)
                            .frame(maxWidth: .infinity, alignment: .leading)
                    }
                }
                .padding(14)
            }
            .frame(width: 380, height: 300)
        }
    }
}

private struct DetailRow: View {
    let label: String
    let value: String
    init(_ label: String, _ value: String) { self.label = label; self.value = value }

    var body: some View {
        GridRow {
            Text(label).foregroundStyle(.secondary)
            Text(value).textSelection(.enabled)
        }
    }
}

// MARK: - Instrument list

private struct VoiceListView: View {
    let voices: [VoiceSnapshot]
    let isConsole: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Label(isConsole ? "Voci del chip" : "Strumenti", systemImage: "list.bullet.indent")
                    .font(.headline)
                Spacer()
                Text("\(voices.filter(\.active).count)/\(voices.count) attivi")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            .padding(.horizontal, 24)
            .padding(.bottom, 6)

            ScrollView {
                LazyVStack(spacing: 2) {
                    ForEach(voices) { voice in
                        VoiceRow(voice: voice)
                    }
                }
                .padding(.horizontal, 24)
                .padding(.vertical, 4)
            }
            .defaultScrollAnchor(.top)
        }
    }
}

private struct VoiceRow: View {
    @Environment(AppSettings.self) private var settings
    let voice: VoiceSnapshot

    private var instrumentTitle: String {
        if !voice.instrument.isEmpty { return voice.instrument }
        if voice.instrumentIndex > 0 { return "Strumento \(voice.instrumentIndex + 1)" }
        return "—"
    }

    var body: some View {
        HStack(spacing: 8) {
            // channel / voice number
            Text("\(voice.id + 1)")
                .font(.caption.monospacedDigit())
                .foregroundStyle(.secondary)
                .frame(width: 22, alignment: .trailing)

            // instrument (declared name, or a placeholder)
            Text(instrumentTitle)
                .font(.callout)
                .lineLimit(1)
                .truncationMode(.middle)
                .frame(maxWidth: .infinity, alignment: .leading)

            // volume
            LevelBar(level: voice.level, active: voice.active, accent: settings.accent)
                .frame(width: 64)

            // note
            Text(Formatting.noteName(voice.note))
                .font(.caption.monospacedDigit())
                .foregroundStyle(.secondary)
                .frame(width: 34, alignment: .trailing)

            // per-voice oscilloscope
            MiniWaveform(samples: voice.waveform, active: voice.active, accent: settings.accent)
                .frame(width: 120, height: 20)
        }
        .padding(.vertical, 1)
        .padding(.horizontal, 8)
        .background(voice.active ? settings.accent.opacity(0.10) : Color.clear,
                    in: RoundedRectangle(cornerRadius: 6))
        .help(voice.sample.isEmpty ? instrumentTitle : "\(instrumentTitle) — campione: \(voice.sample)")
    }
}

// MARK: - Transport

private struct TransportBar: View {
    @Environment(AppSettings.self) private var settings
    @Bindable var model: PlayerModel

    var body: some View {
        VStack(spacing: 10) {
            HStack(spacing: 14) {
                Text(Formatting.time(model.currentTime.truncatingRemainder(dividingBy: max(model.duration, 1))))
                    .font(.caption.monospacedDigit())
                    .foregroundStyle(.secondary)
                    .frame(width: 48, alignment: .trailing)

                Slider(value: Binding(get: { model.progress },
                                      set: { model.seek(to: $0 * model.duration) }),
                       in: 0...1)
                .disabled(model.duration <= 0)

                Text(Formatting.time(model.duration))
                    .font(.caption.monospacedDigit())
                    .foregroundStyle(.secondary)
                    .frame(width: 48, alignment: .leading)
            }

            HStack(spacing: 22) {
                Button {
                    model.shuffle.toggle()
                } label: {
                    Image(systemName: "shuffle")
                        .foregroundStyle(model.shuffle ? settings.accent : Color.secondary)
                }
                .buttonStyle(.plain)
                .help("Riproduzione casuale")

                Spacer()

                Button { model.previous() } label: { Image(systemName: "backward.end.fill") }
                    .buttonStyle(.plain)
                    .help("Brano precedente")

                Button { model.togglePlayPause() } label: {
                    Image(systemName: model.isPlaying && !model.isPaused ? "pause.circle.fill" : "play.circle.fill")
                        .font(.system(size: 46))
                        .symbolRenderingMode(.hierarchical)
                }
                .buttonStyle(.plain)
                .help(model.isPlaying && !model.isPaused ? "Pausa" : "Riproduci")

                Button { model.next() } label: { Image(systemName: "forward.end.fill") }
                    .buttonStyle(.plain)
                    .help("Brano successivo")

                Spacer()

                Button {
                    model.repeatMode.advance()
                } label: {
                    Image(systemName: model.repeatMode.systemImage)
                        .foregroundStyle(model.repeatMode == .off ? Color.secondary : settings.accent)
                }
                .buttonStyle(.plain)
                .help(model.repeatMode.help)
            }
            .font(.title3)

            HStack(spacing: 8) {
                Image(systemName: "speaker.wave.2.fill")
                    .foregroundStyle(.secondary)
                Slider(value: $model.volume, in: 0...1)
                    .frame(maxWidth: 200)
            }
            .font(.caption)
        }
        .padding(.horizontal, 24)
        .padding(.vertical, 16)
        .background(.bar)
    }
}
