# Create a distribution directory
mkdir -p dist/lib
cp bin/embedding_bridge dist/
# Copy all required shared libraries
ldd bin/embedding_bridge | grep "=> /" | awk '{print $3}' | xargs -I '{}' cp '{}' dist/lib/

# Create a wrapper script
cat > dist/run_embedding_bridge.sh << 'EOF'
#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
export LD_LIBRARY_PATH="$DIR/lib:$LD_LIBRARY_PATH"
"$DIR/embedding_bridge" "$@"
EOF
chmod +x dist/run_embedding_bridge.sh