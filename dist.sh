#!/bin/bash

# Create a distribution directory
mkdir -p dist/lib
cp bin/embedding_bridge dist/

# Copy all required shared libraries detected by ldd
ldd bin/embedding_bridge | grep "=> /" | awk '{print $3}' | xargs -I '{}' cp '{}' dist/lib/

# Explicitly copy Arrow libraries
echo "Copying Arrow libraries..."
mkdir -p vendor/dist/lib 2>/dev/null || true
if [ -d "vendor/dist/lib" ]; then
  cp -P vendor/dist/lib/libarrow.so* dist/lib/ 2>/dev/null || echo "Warning: Arrow libraries not found"
  cp -P vendor/dist/lib/libparquet.so* dist/lib/ 2>/dev/null || echo "Warning: Parquet libraries not found"
fi

# Explicitly copy AWS libraries if they exist as shared libraries
echo "Copying AWS libraries..."
if [ -d "vendor/aws/install/lib" ]; then
  find vendor/aws/install/lib -name "*.so*" -type f -exec cp -P {} dist/lib/ \; 2>/dev/null || echo "Warning: No AWS shared libraries found"
fi

# Create a wrapper script
cat > dist/run_embedding_bridge.sh << 'EOF'
#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
export LD_LIBRARY_PATH="$DIR/lib:$LD_LIBRARY_PATH"
"$DIR/embedding_bridge" "$@"
EOF
chmod +x dist/run_embedding_bridge.sh

echo "Distribution package created in dist/"