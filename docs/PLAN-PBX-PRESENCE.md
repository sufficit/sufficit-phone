# Status of the app as PBX presence

Context and decisions: `sufficit-pabx-asterisk/docs/PLAN-EXTENSION-PRESENCE.md`.
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
- [ ] Identity: apply the manifest with
  `python3 scripts/identity-provision.py --token-file <file> --apply`. This needs
  an operator token with `identity.provisioning.preview` and
  `identity.provisioning.apply`.
  **Apply it before releasing a build that requests the new scope**, otherwise
  interactive sign-in fails with `invalid_scope`.
- [ ] Android (`android/overlay/.../SufficitAuth.kt`): same mapping and scope.
- [ ] Validation with a real installation on apoint: change the status, then
  confirm the PBX presence, the panel card and that queues skip `dnd`.
