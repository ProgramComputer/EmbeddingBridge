#!/bin/bash

# IMPORTANT:Change @types.h to have the correct version
# RUN dist.sh before running this script

# Get version from command line or extract from types.h as default
if [ -z "$1" ]; then
    # Extract version from types.h if no argument is provided
    DEFAULT_VERSION=$(grep -E "^#define EB_VERSION_STR" src/core/types.h | awk '{print $3}' | tr -d '"')
    VERSION=${DEFAULT_VERSION:-$(date +%Y%m%d)}
else
    VERSION=$1
fi
RELEASE_NAME="embedding_bridge-${VERSION}"

# Create release directory
mkdir -p "${RELEASE_NAME}"

# Copy distribution files
cp -r dist/* "${RELEASE_NAME}/"

# Copy documentation and other important files
cp README.md LICENSE CONTRIBUTING.md RELEASE_NOTES.md "${RELEASE_NAME}/"

# Create release archive
tar -czf "${RELEASE_NAME}.tar.gz" "${RELEASE_NAME}"

# Optional: Create ZIP archive for Windows users
zip -r "${RELEASE_NAME}.zip" "${RELEASE_NAME}"

# Cleanup
rm -rf "${RELEASE_NAME}"

echo "Created release packages:"
echo "- ${RELEASE_NAME}.tar.gz"
echo "- ${RELEASE_NAME}.zip"