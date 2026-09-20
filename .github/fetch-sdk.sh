#!/usr/bin/env bash
# Fetch the X-Plane SDK into ./SDK for a CI build.
#
# The SDK is Laminar Research's and is not redistributed in this repository
# (see LICENSE), so every build downloads it. SDK_URL comes from the workflow
# environment.
set -euo pipefail

: "${SDK_URL:?SDK_URL is not set}"

echo "Downloading $SDK_URL"
curl -fsSL --retry 3 --retry-delay 5 -o sdk.zip "$SDK_URL"

unzip -q sdk.zip -d sdk-tmp

# The zip's top-level folder name changes between SDK releases, so locate the
# directory that actually contains CHeaders rather than assuming a path.
CHEADERS="$(find sdk-tmp -maxdepth 3 -type d -name CHeaders | head -1)"
if [ -z "$CHEADERS" ]; then
  echo "error: no CHeaders directory inside $SDK_URL" >&2
  find sdk-tmp -maxdepth 2 >&2
  exit 1
fi

mv "$(dirname "$CHEADERS")" ./SDK
rm -rf sdk.zip sdk-tmp

# XPLM440 is what the panel-graphics drawing layer needs; fail loudly and early
# rather than partway through a compile if the URL ever points at an older SDK.
if ! grep -q 'kXPLM_Version *(4[4-9][0-9])' SDK/CHeaders/XPLM/XPLMDefs.h; then
  echo "error: SDK is older than 4.4.0 - the source requires XPLM440" >&2
  grep -n 'kXPLM_Version' SDK/CHeaders/XPLM/XPLMDefs.h >&2 || true
  exit 1
fi

echo "SDK ready:"
grep -m1 'Release 4' SDK/README.txt || true
ls SDK
