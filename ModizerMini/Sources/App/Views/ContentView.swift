//
//  ContentView.swift
//  ModizerMini
//

import SwiftUI

struct ContentView: View {
    @Bindable var model: PlayerModel

    var body: some View {
        NavigationSplitView {
            SidebarView(model: model)
                .navigationSplitViewColumnWidth(min: 230, ideal: 270)
        } detail: {
            PlayerView(model: model)
        }
        .frame(minWidth: 940, minHeight: 620)
        // Let the sidebar vibrancy reach the desktop.
        .background(WindowTransparencyConfigurator())
        .onOpenURL { model.open($0) }
        .alert("Impossibile riprodurre il file",
               isPresented: Binding(get: { model.errorMessage != nil },
                                    set: { if !$0 { model.dismissError() } })) {
            Button("OK", role: .cancel) { model.dismissError() }
        } message: {
            Text(model.errorMessage ?? "")
        }
    }
}
