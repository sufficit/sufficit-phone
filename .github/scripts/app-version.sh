#!/usr/bin/env bash
# Compute a full version for CI builds, bypassing bc_compute_full_version().
#
# Why: bctoolbox's bc_compute_full_version() parses `git describe` with a
# regex that only accepts prerelease tags like -alpha / -beta (no dot-number
# suffix). Our release tags (6.2.0-alpha.2) make any post-tag describe output
# (e.g. 6.2.0-alpha.2-3-g4918b1aa8) FATAL_ERROR at configure time.
# Passing -DLINPHONEAPP_VERSION/-DLINPHONESDK_VERSION skips that code path;
# this script produces a valid full version (bc_parse_full_version-compatible)
# from any describe output:
#   6.2.0-alpha.2-3-g4918b1aa8 -> 6.2.0-alpha.5+4918b1aa8
#   6.2.0-alpha.2-0-g<h>       -> 6.2.0-alpha.2          (exact tag)
#   6.1.1-5-g<h>               -> 6.1.1+<h>
#   6.1.1-0-g<h>               -> 6.1.1
#
# Usage: app-version.sh [git-dir]
#   Without arguments: version of the main repository.
#   With a directory (e.g. external/linphone-sdk): version of that git
#   context. On CI the SDK directory has no own .git, so git resolves to
#   the parent repository - which is exactly what the old code did too.
set -euo pipefail

dir="${1:-.}"
cd "$dir"

raw=$(git describe --tags --long --match '[0-9]*' --match 'v[0-9]*' 2>/dev/null || true)
if [ -z "$raw" ]; then
	echo "0.0.0+$(git rev-parse --short HEAD)"
	exit 0
fi

raw=${raw#v}
if [[ "$raw" =~ ^([0-9]+[.][0-9]+[.][0-9]+)(-([A-Za-z]+)([.]([0-9]+))?)?-([0-9]+)-g([0-9a-f]+)$ ]]; then
	core=${BASH_REMATCH[1]}
	pre=${BASH_REMATCH[3]}
	suffix=${BASH_REMATCH[5]}
	n=${BASH_REMATCH[6]}
	hash=${BASH_REMATCH[7]}
	if [ -n "$pre" ]; then
		base=${suffix:-0}
		if [ "$n" -gt 0 ]; then
			echo "$core-$pre.$((base + n))+$hash"
		elif [ "$base" -gt 0 ]; then
			echo "$core-$pre.$base"
		else
			echo "$core-$pre"
		fi
	else
		if [ "$n" -gt 0 ]; then
			echo "$core+$hash"
		else
			echo "$core"
		fi
	fi
else
	echo "app-version.sh: unparsable git describe output: '$raw'" >&2
	exit 1
fi
