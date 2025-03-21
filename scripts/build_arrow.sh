#!/bin/bash
set -e

ARROW_VERSION="12.0.0"
ARROW_BUILD_TYPE="Release"
ARROW_DIR="$(pwd)/vendor/arrow"
ARROW_BUILD_DIR="${ARROW_DIR}/cpp/build"
ARROW_INSTALL_DIR="/usr/local"
VENV_DIR="$(pwd)/vendor/venv"
BUILD_GLIB=false

# Check for sudo privileges if installing to system location
if [[ "$ARROW_INSTALL_DIR" == "/usr/local" && $EUID -ne 0 ]]; then
    echo "Warning: Installing to $ARROW_INSTALL_DIR may require sudo privileges."
    echo "If installation fails with permission errors, re-run with sudo."
    echo "Continuing in 3 seconds..."
    sleep 3
fi

# Parse command line arguments
for arg in "$@"; do
    case $arg in
        --with-glib)
        BUILD_GLIB=true
        shift
        ;;
        *)
        # Unknown option
        ;;
    esac
done

# Create directories
mkdir -p "${ARROW_DIR}"
mkdir -p "${ARROW_INSTALL_DIR}"

# Download Arrow source if not exists
if [ ! -d "${ARROW_DIR}/cpp" ]; then
    echo "Downloading Apache Arrow ${ARROW_VERSION}..."
    wget "https://dlcdn.apache.org/arrow/arrow-${ARROW_VERSION}/apache-arrow-${ARROW_VERSION}.tar.gz" \
        -O "apache-arrow-${ARROW_VERSION}.tar.gz"
    tar xf "apache-arrow-${ARROW_VERSION}.tar.gz" --strip-components=1 -C "${ARROW_DIR}"
    rm "apache-arrow-${ARROW_VERSION}.tar.gz"
fi

# Build Arrow C++
if [ ! -f "${ARROW_INSTALL_DIR}/lib/libarrow.so" ]; then
    echo "Building Arrow C++..."
    # Minimal Arrow build with just what we need for Parquet
    cd "${ARROW_DIR}/cpp"
    mkdir -p build
    cd build

    echo "Configuring Arrow C++ build..."
    cmake .. \
        -DCMAKE_BUILD_TYPE=${ARROW_BUILD_TYPE} \
        -DCMAKE_INSTALL_PREFIX=${ARROW_INSTALL_DIR} \
        -DARROW_DEPENDENCY_SOURCE=BUNDLED \
        -DARROW_PARQUET=ON \
        -DARROW_WITH_ZSTD=ON \
        -DARROW_WITH_LZ4=ON \
        -DARROW_WITH_SNAPPY=ON \
        -DARROW_BUILD_SHARED=ON \
        -DARROW_BUILD_STATIC=OFF \
        -DARROW_COMPUTE=ON \
        -DARROW_CSV=ON \
        -DARROW_DATASET=ON \
        -DARROW_FILESYSTEM=ON \
        -DARROW_JSON=ON \
        -DARROW_INSTALL_NAME_RPATH=OFF

    echo "Building Arrow C++ (this may take a while)..."
    cmake --build . --config ${ARROW_BUILD_TYPE} --parallel $(nproc)

    echo "Installing Arrow C++..."
    cmake --install .
else
    echo "Arrow C++ already built, skipping."
fi

# Write a pkg-config file for our locally built Arrow if not exists
if [ ! -f "${ARROW_INSTALL_DIR}/lib/pkgconfig/arrow.pc" ]; then
    mkdir -p "${ARROW_INSTALL_DIR}/lib/pkgconfig"
    cat > "${ARROW_INSTALL_DIR}/lib/pkgconfig/arrow.pc" << EOF
prefix=${ARROW_INSTALL_DIR}
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: Apache Arrow
Description: Arrow is a columnar in-memory analytics layer for big data.
Version: ${ARROW_VERSION}
Libs: -L\${libdir} -larrow
Cflags: -I\${includedir}
EOF

    cat > "${ARROW_INSTALL_DIR}/lib/pkgconfig/parquet.pc" << EOF
prefix=${ARROW_INSTALL_DIR}
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: Apache Parquet
Description: Apache Parquet is a columnar storage format.
Version: ${ARROW_VERSION}
Requires: arrow
Libs: -L\${libdir} -lparquet
Cflags: -I\${includedir}
EOF

    # Add pkgconfig for arrow-csv
    cat > "${ARROW_INSTALL_DIR}/lib/pkgconfig/arrow-csv.pc" << EOF
prefix=${ARROW_INSTALL_DIR}
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: Apache Arrow CSV
Description: Apache Arrow CSV adapter.
Version: ${ARROW_VERSION}
Requires: arrow
Libs: -L\${libdir} -larrow
Cflags: -I\${includedir}
EOF
fi

# Build Arrow GLib if requested
if [ "$BUILD_GLIB" = true ]; then
    echo "Checking GLib build dependencies..."
    
    # Create a Python virtual environment for meson and ninja
    if [ ! -d "$VENV_DIR" ]; then
        echo "Creating Python virtual environment for meson and ninja..."
        python3 -m venv "$VENV_DIR"
    fi
    
    # Activate the virtual environment
    echo "Activating Python virtual environment..."
    source "$VENV_DIR/bin/activate"
    
    # Install meson and ninja in the virtual environment
    echo "Installing meson and ninja in virtual environment..."
    pip install --upgrade pip
    pip install meson ninja
    
    # Install other dependencies
    if command -v apt-get &> /dev/null; then
        echo "Installing GTK-Doc and GObject introspection dependencies..."
        if [[ $EUID -eq 0 ]]; then
            apt-get update
            apt-get install -y -V gtk-doc-tools libgirepository1.0-dev
        else
            sudo apt-get update
            sudo apt-get install -y -V gtk-doc-tools libgirepository1.0-dev
        fi
    elif command -v yum &> /dev/null; then
        if [[ $EUID -eq 0 ]]; then
            yum install -y gtk-doc gobject-introspection-devel
        else
            sudo yum install -y gtk-doc gobject-introspection-devel
        fi
    fi
    
    # Setup directory permissions if needed
    if [[ "$ARROW_INSTALL_DIR" == "/usr/local" && ! -w "$ARROW_INSTALL_DIR" ]]; then
        echo "Setting up write permissions for $ARROW_INSTALL_DIR..."
        sudo mkdir -p "$ARROW_INSTALL_DIR"
        sudo chown $(whoami) "$ARROW_INSTALL_DIR"
    fi
    
    # Build Arrow GLib
    cd "${ARROW_DIR}/c_glib" || exit 1
    
    if [ -d "build" ]; then
        rm -rf build
    fi
    
    echo "Configuring Arrow GLib build..."
    export PKG_CONFIG_PATH="${ARROW_INSTALL_DIR}/lib/pkgconfig:$PKG_CONFIG_PATH"
    meson setup build --prefix=${ARROW_INSTALL_DIR} --buildtype=release
    
    echo "Building Arrow GLib..."
    meson compile -C build
    
    echo "Installing Arrow GLib..."
    meson install -C build
    
    echo "Arrow GLib build complete."
    
    # Deactivate the virtual environment
    deactivate
fi

# Run ldconfig if we're installing to a system directory
if [[ "$ARROW_INSTALL_DIR" == "/usr/local" ]]; then
    echo "Updating system library cache..."
    sudo ldconfig || echo "Warning: Failed to run ldconfig. You may need to run 'sudo ldconfig' manually."
fi

echo "Arrow C++ ${ARROW_VERSION} build complete."
echo "Installed to: ${ARROW_INSTALL_DIR}"
echo "Libraries should be automatically found by the system."
echo "For development, you may need: export PKG_CONFIG_PATH=\"${ARROW_INSTALL_DIR}/lib/pkgconfig:\$PKG_CONFIG_PATH\"" 