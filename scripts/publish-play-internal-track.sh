#!/usr/bin/env bash
# Publish an Android App Bundle (or an already uploaded version code) to the
# Google Play internal testing track of com.sufficit.phone.
#
# Required: GOOGLE_OAUTH_ACCESS_TOKEN (androidpublisher scope)
# Optional: PLAY_PACKAGE_NAME (default com.sufficit.phone), PLAY_RELEASE_NAME,
#           PLAY_RELEASE_NOTES, PLAY_EXISTING_VERSION_CODE, GITHUB_OUTPUT
#
# Usage: scripts/publish-play-internal-track.sh [BUNDLE_PATH]
#   BUNDLE_PATH is not needed when PLAY_EXISTING_VERSION_CODE is set.
# Testers stay managed in the Play Console (the list is preserved).
set -euo pipefail

PACKAGE_NAME="${PLAY_PACKAGE_NAME:-com.sufficit.phone}"
BUNDLE_PATH="${1:-}"
EXISTING="${PLAY_EXISTING_VERSION_CODE:-}"
TOKEN="${GOOGLE_OAUTH_ACCESS_TOKEN:-}"
NOTES="${PLAY_RELEASE_NOTES:-Nova versão disponível para testes.}"
TRACK=internal

[ -n "$TOKEN" ] || { echo "GOOGLE_OAUTH_ACCESS_TOKEN is required" >&2; exit 2; }
if [ -n "$EXISTING" ]; then
  [[ "$EXISTING" =~ ^[1-9][0-9]*$ ]] || { echo "PLAY_EXISTING_VERSION_CODE must be a positive integer" >&2; exit 2; }
else
  [ -s "$BUNDLE_PATH" ] || { echo "AAB not found: '$BUNDLE_PATH'" >&2; exit 2; }
fi
[ "${#NOTES}" -le 500 ] || { echo "PLAY_RELEASE_NOTES must have at most 500 characters" >&2; exit 2; }
for c in curl jq; do command -v "$c" >/dev/null || { echo "$c is required" >&2; exit 2; }; done

API="https://androidpublisher.googleapis.com/androidpublisher/v3/applications/$PACKAGE_NAME"
UPLOAD="https://androidpublisher.googleapis.com/upload/androidpublisher/v3/applications/$PACKAGE_NAME"
WORK="$(mktemp -d)"
EDIT_ID=""
COMMITTED=false

call() { # method url output [body] [content-type]
  local args=(--config - --silent --show-error --max-time 600 --output "$3" --write-out '%{http_code}' --request "$1")
  [ -n "${4:-}" ] && args+=(--header "Content-Type: ${5:-application/json}" --data-binary "@$4")
  local code
  code="$(printf 'header = "Authorization: Bearer %s"\n' "$TOKEN" | curl "${args[@]}" "$2")"
  case "$code" in
    2??) ;;
    *) echo "Google Play request failed (HTTP $code): $1 ${2%%\?*}" >&2
       jq -r '.error.message // .' "$3" >&2 || true
       return 1 ;;
  esac
}

cleanup() {
  local s=$?
  trap - EXIT
  if [ "$s" -ne 0 ] && [ -n "$EDIT_ID" ] && [ "$COMMITTED" != true ]; then
    echo "==> Discarding uncommitted edit $EDIT_ID" >&2
    printf 'header = "Authorization: Bearer %s"\n' "$TOKEN" | curl --config - -s --max-time 30 -X DELETE "$API/edits/$EDIT_ID" >/dev/null || true
  fi
  rm -rf "$WORK"
  exit "$s"
}
trap cleanup EXIT

echo '{}' > "$WORK/empty.json"
echo "==> Creating edit for $PACKAGE_NAME"
call POST "$API/edits" "$WORK/edit.json" "$WORK/empty.json"
EDIT_ID="$(jq -r '.id // empty' "$WORK/edit.json")"
[ -n "$EDIT_ID" ] || { echo "No edit id returned" >&2; exit 1; }

if [ -n "$EXISTING" ]; then
  VERSION_CODE="$EXISTING"
  echo "==> Reusing version code $VERSION_CODE"
else
  echo "==> Uploading $(basename "$BUNDLE_PATH")"
  call POST "$UPLOAD/edits/$EDIT_ID/bundles?uploadType=media" "$WORK/upload.json" "$BUNDLE_PATH" application/octet-stream
  VERSION_CODE="$(jq -r '.versionCode | tostring' "$WORK/upload.json")"
fi
[[ "$VERSION_CODE" =~ ^[1-9][0-9]*$ ]] || { echo "Invalid version code" >&2; exit 1; }
NAME="${PLAY_RELEASE_NAME:-$VERSION_CODE}"

echo "==> Publishing version code $VERSION_CODE to the $TRACK track"
jq -n --arg t "$TRACK" --arg n "$NAME" --arg v "$VERSION_CODE" --arg notes "$NOTES" \
  '{track:$t,releases:[{name:$n,versionCodes:[$v],releaseNotes:[{language:"pt-BR",text:$notes}],status:"completed"}]}' > "$WORK/track.json"
call PUT "$API/edits/$EDIT_ID/tracks/$TRACK" "$WORK/track-out.json" "$WORK/track.json"

echo "==> Validating and committing"
call POST "$API/edits/$EDIT_ID:validate" "$WORK/validate.json"
call POST "$API/edits/$EDIT_ID:commit?changesInReviewBehavior=ERROR_IF_IN_REVIEW" "$WORK/commit.json"
COMMITTED=true

[ -z "${GITHUB_OUTPUT:-}" ] || echo "version_code=$VERSION_CODE" >> "$GITHUB_OUTPUT"
echo "Internal testing release committed at version code $VERSION_CODE"
