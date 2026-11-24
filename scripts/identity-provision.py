#!/usr/bin/env python3
# Purpose: Preview or apply identity-manifest.v1.json (OAuth client sufficit-phone-desktop) on the Sufficit Identity.
# Usage: python3 scripts/identity-provision.py --token-file <short-lived operator token file> [--apply]
#        The token needs identity.provisioning.preview (and identity.provisioning.apply with --apply).
# Last modified: 2026-11-23
import argparse
import json
import pathlib
import urllib.request

parser = argparse.ArgumentParser()
parser.add_argument('--token-file', required=True)
parser.add_argument('--apply', action='store_true')
args = parser.parse_args()
token = pathlib.Path(args.token_file).read_text().strip()
body = (pathlib.Path(__file__).resolve().parent.parent / 'identity-manifest.v1.json').read_bytes()
for action in (['inventory', 'preview', 'apply'] if args.apply else ['inventory', 'preview']):
    request = urllib.request.Request('https://identity.sufficit.com.br/api/provisioning/manifest/' + action,
        data=body, headers={'Content-Type': 'application/json', 'Authorization': 'Bearer ' + token})
    with urllib.request.urlopen(request, timeout=15) as response:
        result = json.load(response)
    print(action, json.dumps(result))
