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
        .background(.background)
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
            Text(track?.displayName ?? "—")
                .font(.largeTitle.weight(.semibold))
                .lineLimit(1)
                .truncationMode(.middle)

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
                LazyVStack(spacing: 6) {
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

    var body: some View {
        HStack(spacing: 12) {
            Text(voice.name)
                .font(.callout.monospacedDigit())
                .foregroundStyle(.secondary)
                .frame(width: 90, alignment: .leading)

            VStack(alignment: .leading, spacing: 3) {
                HStack(spacing: 8) {
                    Text(voice.instrument.isEmpty ? (voice.active ? Formatting.noteName(voice.note) : "—") : voice.instrument)
                        .font(.callout)
                        .lineLimit(1)
                    Spacer(minLength: 0)
                    Text(Formatting.noteName(voice.note))
                        .font(.caption.monospacedDigit())
                        .foregroundStyle(.secondary)
                }
                LevelBar(level: voice.level, active: voice.active, accent: settings.accent)
            }
            .frame(maxWidth: .infinity)

            MiniWaveform(samples: voice.waveform, active: voice.active, accent: settings.accent)
                .frame(width: 150, height: 30)
        }
        .padding(.vertical, 4)
        .padding(.horizontal, 10)
        .background(voice.active ? settings.accent.opacity(0.10) : Color.clear,
                    in: RoundedRectangle(cornerRadius: 8))
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
