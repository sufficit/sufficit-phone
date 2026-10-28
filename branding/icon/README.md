# Sufficit Phone icon

Original icon of the Sufficit Phone app, distinct from the generic Sufficit icon:
the official Sufficit symbol with a red phone badge.

- `sufficit-phone-light.svg`: default icon (white background) used on Android, Windows, macOS and Linux.
- `sufficit-phone-dark.svg`: dark variant (graphite background) for dark surfaces, stores and marketing.
- `sufficit-phone-android-foreground.svg` / `-monochrome.svg`: Android adaptive icon layers
  (content inside the 66 dp safe zone; background color `#FFFFFF`).

Generated files: `Linphone/data/icon/hicolor/*/apps/icon.png`, `Linphone/data/icon.ico`,
`cmake/install/macos/linphone.icns`, `android/overlay/app/src/main/res/mipmap-*`.
Render with `rsvg-convert` from these SVGs.
