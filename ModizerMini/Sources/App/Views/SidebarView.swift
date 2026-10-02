//
//  SidebarView.swift
//  ModizerMini
//

import SwiftUI

struct SidebarView: View {
    @Environment(AppSettings.self) private var settings
    @Bindable var model: PlayerModel
    @State private var isImporting = false
    @State private var isDropTargeted = false

    var body: some View {
        List {
            ForEach(model.tracks) { track in
                TrackRow(track: track,
                         isCurrent: track.id == model.currentTrackID,
                         isPlaying: track.id == model.currentTrackID && model.isPlaying && !model.isPaused,
                         accent: settings.accent)
                    .contentShape(Rectangle())
                    .onTapGesture { model.play(track) }
                    .listRowBackground(Color.clear)
                    .listRowSeparator(.hidden)
            }
            .onMove { model.move(from: $0, to: $1) }
            .onDelete { model.remove(at: $0) }
        }
        .listStyle(.sidebar)
        .scrollContentBackground(.hidden)
        .background(.ultraThinMaterial)
        .overlay {
            if model.tracks.isEmpty {
                ContentUnavailableView {
                    Label("Playlist vuota", systemImage: "music.note.list")
                } description: {
                    Text("Trascina qui i file supportati, oppure usa il pulsante +.")
                }
                .padding()
            }
        }
        .dropDestination(for: URL.self) { urls, _ in
            model.addFiles(urls)
            return true
        } isTargeted: { isDropTargeted = $0 }
        .overlay(
            RoundedRectangle(cornerRadius: 8)
                .strokeBorder(settings.accent, lineWidth: 2)
                .opacity(isDropTargeted ? 1 : 0)
                .padding(4)
        )
        .navigationTitle("Playlist")
        .toolbar {
            ToolbarItemGroup {
                Button {
                    isImporting = true
                } label: {
                    Label("Aggiungi file", systemImage: "plus")
                }
                .help("Aggiungi file supportati alla playlist")

                Button(role: .destructive) {
                    model.clear()
                } label: {
                    Label("Svuota playlist", systemImage: "trash")
                }
                .disabled(model.tracks.isEmpty)
                .help("Rimuovi tutti i brani")
            }
        }
        .fileImporter(isPresented: $isImporting,
                      allowedContentTypes: PlayerModel.supportedContentTypes,
                      allowsMultipleSelection: true) { result in
            if case .success(let urls) = result { model.addFiles(urls) }
        }
    }
}

private struct TrackRow: View {
    let track: Track
    let isCurrent: Bool
    let isPlaying: Bool
    let accent: Color

    var body: some View {
        HStack(spacing: 10) {
            Image(systemName: isCurrent ? (isPlaying ? "waveform" : "pause.circle") : "music.note")
                .foregroundStyle(isCurrent ? Color.white : Color.secondary)
                .frame(width: 18)

            VStack(alignment: .leading, spacing: 1) {
                Text(track.displayName)
                    .lineLimit(1)
                    .fontWeight(isCurrent ? .semibold : .regular)
                Text(track.fileExtension)
                    .font(.caption2)
                    .opacity(isCurrent ? 0.85 : 0.6)
            }
            Spacer(minLength: 0)
        }
        .foregroundStyle(isCurrent ? Color.white : Color.primary)
        .padding(.vertical, 5)
        .padding(.horizontal, 8)
        .background(
            RoundedRectangle(cornerRadius: 6)
                .fill(isCurrent ? accent : Color.clear)
        )
    }
}
