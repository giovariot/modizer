//
//  ModizerMiniApp.swift
//  ModizerMini
//
//  A tiny player for the chiptune formats Modizer supports, built entirely on
//  the portable decoders already bundled in this repository.
//

import SwiftUI

@main
struct ModizerMiniApp: App {
    @State private var model = PlayerModel()
    @State private var settings = AppSettings()

    var body: some Scene {
        Window("Modizer Mini", id: "main") {
            ContentView(model: model)
                .environment(settings)
                .tint(settings.accent)
        }
        .defaultSize(width: 1040, height: 720)
        .windowResizability(.contentMinSize)
        .windowToolbarStyle(.unified)

        Settings {
            SettingsView()
                .environment(settings)
                .tint(settings.accent)
        }
        .commands {
            CommandGroup(replacing: .newItem) {}

            CommandMenu("Riproduzione") {
                Button("Riproduci / Pausa") { model.togglePlayPause() }
                    .keyboardShortcut(.space, modifiers: [])
                Button("Brano successivo") { model.next() }
                    .keyboardShortcut(.rightArrow, modifiers: .command)
                Button("Brano precedente") { model.previous() }
                    .keyboardShortcut(.leftArrow, modifiers: .command)
                Divider()
                Button(model.shuffle ? "Disattiva ordine casuale" : "Attiva ordine casuale") {
                    model.shuffle.toggle()
                }
                .keyboardShortcut("s", modifiers: [.command, .shift])
                Button("Cambia modalità di ripetizione") { model.repeatMode.advance() }
                    .keyboardShortcut("r", modifiers: [.command, .shift])
            }
        }
    }
}
