# Status of the app as PBX presence

Context and decisions: `sufficit-pabx-asterisk/docs/activities/202611241300-extension-presence.md`.
When the user picks a status in the app, the app stores it as the native Asterisk
presence of the assigned ramal, keyed by its GUID, through the EndPoints presence
API. SIMPLE `PUBLISH` stays off, because Asterisk rejects client presence with
489.

| App status | PBX presence |
|---|---|
| Online | `available` |
| Away | `away` |
| Busy | `away`, subtype `busy` |
| Do not disturb | `dnd` |
| Offline | `unavailable` |
| Undefined | `not_set` |

The presence note (up to 140 characters) becomes the presence message.

The request names the device (`device: "pjsip"`, 2026-11-24). Extensions with the default
unified presence apply the status to every device; extensions set to presence per device
apply it only to this softphone (the Chrome extension sends `webrtc`).

## Changes

- `Linphone/tool/SufficitOAuth.*`:
  - `publishPresence()` sends `PUT https://endpoints.sufficit.com.br/telephony/endpoint/presence`
    with the session token;
  - the `assignedRamalId` from provisioning is kept in settings;
  - a status picked before the token or the ramal is available is sent once
    they are;
  - interactive sign-in also requests `endpoints.phone`; the silent refresh
    sends no scope and keeps what the session was granted.
- `Linphone/model/account/AccountModel.cpp`: the status works with publish
  disabled; `PUBLISH` is still sent only when the account enables it.
- `Linphone/core/account/AccountCore.cpp`: a user-initiated status triggers
  `publishPresence`.
- `identity-manifest.v1.json`: client `sufficit-phone-desktop` gains
  `endpoints.phone`.

## Checkpoints

- [x] Code (desktop/Qt). The three changed sources pass a syntax check with the
  recorded build flags. A full local build is blocked by a missing
  `libsecret-1` on this workstation; CI builds the app.
- [x] Identity (2026-11-23): manifest applied with an operator token. `endpoints.phone` is a scope owned by
  `sufficit-endpoints.v1`, so the manifest lists it in `referencedScopes` instead of declaring it.
  Preview: `provisioning.installation` unchanged, client `sufficit-phone-desktop` updated. The client now
  has `scp:endpoints.phone`. `/connect/authorize` with the new scope answers 302 to the login page.
- [x] Android 0.2.8-pilot (2026-11-24): the upstream app had no status the user picks (its automatic Online/Offline
  must never reach the PBX), so the side menu gained "Disponibilidade" with Disponível (`available`) and Não perturbe
  (`dnd`), stored as the PBX presence of the assigned extension with `device: "pjsip"`
  (`android/overlay/.../SufficitPresence.kt`, anchors in `scripts/prepare-android.py`). While Não perturbe is on, the
  app also declines incoming calls (486) that still arrive, such as internal calls. Sign-in now requests
  `endpoints.phone`; sessions from older versions get 403 until the user signs out and in again (the app says so).
  Built and signed with the pilot key, installed on the owner's Galaxy A51 (versionCode 10); no crash at startup.
- [ ] Validation with a real installation on apoint: change the status, then
  confirm the PBX presence, the panel card and that queues skip `dnd`.
