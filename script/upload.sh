#!/bin/zsh
set -euo pipefail

# Get the short hash of the most recent commit on the current branch
GIT_HASH=$(git rev-parse --short=6 HEAD)
echo "Updating version.h with git hash: ${GIT_HASH}"

VERSION_H="$(dirname "$0")/../include/version.h"

# Replace the git_hash constant in version.h
sed -i '' "s|constexpr const char\* git_hash = \".*\";|constexpr const char* git_hash = \"${GIT_HASH}\";|" "$VERSION_H"

source ~/.platformio/penv/bin/activate
pio run -e seeed_xiao_esp32s3
pio run -e seeed_xiao_esp32s3 -t upload