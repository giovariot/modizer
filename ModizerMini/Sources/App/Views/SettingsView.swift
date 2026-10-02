//
//  SettingsView.swift
//  ModizerMini
//

import SwiftUI

struct SettingsView: View {
    @Environment(AppSettings.self) private var settings

    var body: some View {
        @Bindable var settings = settings

        Form {
            Section("Aspetto") {
                Picker("Colore accento", selection: $settings.accentChoice) {
                    ForEach(AppSettings.AccentChoice.allCases) { choice in
                        Text(choice.title).tag(choice)
                    }
                }
                .pickerStyle(.inline)

                if settings.accentChoice == .custom {
                    ColorPicker("Colore personalizzato", selection: $settings.customColor, supportsOpacity: false)
                }

                HStack {
                    Text("Anteprima")
                    Spacer()
                    RoundedRectangle(cornerRadius: 6)
                        .fill(settings.accent)
                        .frame(width: 44, height: 22)
                    Image(systemName: "play.circle.fill")
                        .font(.title2)
                        .foregroundStyle(settings.accent)
                }
            }
        }
        .formStyle(.grouped)
        .frame(width: 380, height: 260)
    }
}
