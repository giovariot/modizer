# Icon Composer layers

These SVGs are the layers for the Liquid Glass icon, ready to drop into
**Icon Composer** (Xcode 27 → Open Developer Tool → Icon Composer):

| file | use |
| --- | --- |
| `app-background.svg` | app icon background (flat magenta) |
| `app-mark.svg` | app icon mark (white ribbons) |
| `doc-background.svg` | document icon background (white) |
| `doc-mark.svg` | document icon mark (magenta ribbons) |

Steps:

1. Open Icon Composer and create a new icon.
2. Add `app-background.svg` as a **Background** layer, `app-mark.svg` as a
   layer with the **Glass** material (translucency + specular + refraction).
   Add `doc-background.svg` / `doc-mark.svg` for the document icon.
3. Save as `AppIcon.icon` inside `ModizerMini/Sources/App/`.
4. In Xcode the `.icon` is used automatically as the app icon; remove the
   `ASSETCATALOG_COMPILER_APPICON_NAME = AppIcon` fallback if you want the
   `.icon` to be the only source.

Why not committed as a `.icon`: the package format is not documented and the
bundled `ictool` rejects a hand-written `icon.json`; it has to be produced by
the Icon Composer app.
