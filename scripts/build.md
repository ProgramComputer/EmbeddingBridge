# Build Guide

This guide provides instructions for building all required dependencies for the EmbeddingBridge project.

## Prerequisites

- CMake 3.9+
- C/C++ compiler (GCC 4.9+ or Clang 3.3+)
- Git
- Python 3.6+ (for Arrow Python bindings)
- pkg-config

## Quick Start

Run the build scripts from the project root directory:

```bash
# Build all dependencies
./scripts/build_aws.sh
./scripts/build_arrow.sh

# Build npy_array
cd vendor/npy_array
./configure
make
sudo make install
cd ../..
```

## Apache Arrow

Apache Arrow provides columnar data structures and is required for Parquet file support.

### Build Arrow

```bash
./scripts/build_arrow.sh
```

This script will:
- Download and build Apache Arrow C++ libraries
- Install to `/usr/local` by default
- Build with Parquet, CSV, and dataset support
- Optionally build GLib bindings with `--with-glib` flag

## NPY Array Library

The npy_array library provides C support for reading/writing NumPy arrays.

### Build npy_array

```bash
cd vendor/npy_array
./configure --prefix=/usr/local
make
sudo make install
```

For more configuration options:
```bash
cd vendor/npy_array
./configure --help
```

## AWS C Libraries

AWS C libraries are required for S3 integration.

### Build Order

The libraries must be built in the following order due to dependencies:

1. **aws-lc** - AWS LibCrypto (cryptographic operations)
2. **s2n-tls** - TLS implementation
3. **aws-c-common** - Core utilities
4. **aws-checksums** - Checksum implementations
5. **aws-c-cal** - Cryptographic abstraction layer
6. **aws-c-io** - I/O operations
7. **aws-c-compression** - Compression functionality
8. **aws-c-http** - HTTP protocol implementation
9. **aws-c-sdkutils** - SDK utility functions
10. **aws-c-auth** - Authentication and credentials management
11. **aws-c-s3** - S3 service client

### Quick Build

```bash
./scripts/build_aws.sh
```

### Manual Build Steps

#### 1. Set up the directory structure

```bash
mkdir -p vendor/aws/install
```

#### 2. AWS-LC (LibCrypto)

```bash
cd vendor/aws
git submodule add -f https://github.com/aws/aws-lc.git
cd ~/path/to/project
cmake -S vendor/aws/aws-lc -B vendor/aws/aws-lc/build -DCMAKE_INSTALL_PREFIX=$(pwd)/vendor/aws/install
cmake --build vendor/aws/aws-lc/build --target install
```

#### 3. s2n-tls

```bash
cd vendor/aws
git submodule add -f https://github.com/aws/s2n-tls.git
cd ~/path/to/project
cmake -S vendor/aws/s2n-tls -B vendor/aws/s2n-tls/build -DCMAKE_INSTALL_PREFIX=$(pwd)/vendor/aws/install -DCMAKE_PREFIX_PATH=$(pwd)/vendor/aws/install
cmake --build vendor/aws/s2n-tls/build --target install
```

#### 4. aws-c-common

```bash
cd vendor/aws
git submodule add -f https://github.com/awslabs/aws-c-common.git
cd ~/path/to/project
cmake -S vendor/aws/aws-c-common -B vendor/aws/aws-c-common/build -DCMAKE_INSTALL_PREFIX=$(pwd)/vendor/aws/install -DCMAKE_PREFIX_PATH=$(pwd)/vendor/aws/install
cmake --build vendor/aws/aws-c-common/build --target install
```

#### 5. aws-checksums

```bash
cd vendor/aws
git submodule add -f https://github.com/awslabs/aws-checksums.git
cd ~/path/to/project
cmake -S vendor/aws/aws-checksums -B vendor/aws/aws-checksums/build -DCMAKE_INSTALL_PREFIX=$(pwd)/vendor/aws/install -DCMAKE_PREFIX_PATH=$(pwd)/vendor/aws/install -Daws-c-common_DIR=$(pwd)/vendor/aws/install/lib/cmake/aws-c-common
cmake --build vendor/aws/aws-checksums/build --target install
```

#### 6. aws-c-cal

```bash
cd vendor/aws
git submodule add -f https://github.com/awslabs/aws-c-cal.git
cd ~/path/to/project
cmake -S vendor/aws/aws-c-cal -B vendor/aws/aws-c-cal/build -DCMAKE_INSTALL_PREFIX=$(pwd)/vendor/aws/install -DCMAKE_PREFIX_PATH=$(pwd)/vendor/aws/install -Daws-c-common_DIR=$(pwd)/vendor/aws/install/lib/cmake/aws-c-common
cmake --build vendor/aws/aws-c-cal/build --target install
```

#### 7. aws-c-io

```bash
cd vendor/aws
git submodule add -f https://github.com/awslabs/aws-c-io.git
cd ~/path/to/project
cmake -S vendor/aws/aws-c-io -B vendor/aws/aws-c-io/build -DCMAKE_INSTALL_PREFIX=$(pwd)/vendor/aws/install -DCMAKE_PREFIX_PATH=$(pwd)/vendor/aws/install -Daws-c-common_DIR=$(pwd)/vendor/aws/install/lib/cmake/aws-c-common -Daws-c-cal_DIR=$(pwd)/vendor/aws/install/lib/cmake/aws-c-cal
cmake --build vendor/aws/aws-c-io/build --target install
```

#### 8. aws-c-compression

```bash
cd vendor/aws
git submodule add -f https://github.com/awslabs/aws-c-compression.git
cd ~/path/to/project
cmake -S vendor/aws/aws-c-compression -B vendor/aws/aws-c-compression/build -DCMAKE_INSTALL_PREFIX=$(pwd)/vendor/aws/install -DCMAKE_PREFIX_PATH=$(pwd)/vendor/aws/install -Daws-c-common_DIR=$(pwd)/vendor/aws/install/lib/cmake/aws-c-common
cmake --build vendor/aws/aws-c-compression/build --target install
```

#### 9. aws-c-http

```bash
cd vendor/aws
git submodule add -f https://github.com/awslabs/aws-c-http.git
cd ~/path/to/project
cmake -S vendor/aws/aws-c-http -B vendor/aws/aws-c-http/build -DCMAKE_INSTALL_PREFIX=$(pwd)/vendor/aws/install -DCMAKE_PREFIX_PATH=$(pwd)/vendor/aws/install -Daws-c-common_DIR=$(pwd)/vendor/aws/install/lib/cmake/aws-c-common -Daws-c-compression_DIR=$(pwd)/vendor/aws/install/lib/cmake/aws-c-compression -Daws-c-io_DIR=$(pwd)/vendor/aws/install/lib/cmake/aws-c-io
cmake --build vendor/aws/aws-c-http/build --target install
```

#### 10. aws-c-sdkutils

```bash
cd vendor/aws
git submodule add -f https://github.com/awslabs/aws-c-sdkutils.git
cd ~/path/to/project
cmake -S vendor/aws/aws-c-sdkutils -B vendor/aws/aws-c-sdkutils/build -DCMAKE_INSTALL_PREFIX=$(pwd)/vendor/aws/install -DCMAKE_PREFIX_PATH=$(pwd)/vendor/aws/install -Daws-c-common_DIR=$(pwd)/vendor/aws/install/lib/cmake/aws-c-common
cmake --build vendor/aws/aws-c-sdkutils/build --target install
```

#### 11. aws-c-auth

```bash
cd vendor/aws
git submodule add -f https://github.com/awslabs/aws-c-auth.git
cd ~/path/to/project
cmake -S vendor/aws/aws-c-auth -B vendor/aws/aws-c-auth/build -DCMAKE_INSTALL_PREFIX=$(pwd)/vendor/aws/install -DCMAKE_PREFIX_PATH=$(pwd)/vendor/aws/install -Daws-c-common_DIR=$(pwd)/vendor/aws/install/lib/cmake/aws-c-common -Daws-c-cal_DIR=$(pwd)/vendor/aws/install/lib/cmake/aws-c-cal -Daws-c-io_DIR=$(pwd)/vendor/aws/install/lib/cmake/aws-c-io -Daws-c-http_DIR=$(pwd)/vendor/aws/install/lib/cmake/aws-c-http -Daws-c-sdkutils_DIR=$(pwd)/vendor/aws/install/lib/cmake/aws-c-sdkutils
cmake --build vendor/aws/aws-c-auth/build --target install
```

#### 12. aws-c-s3

```bash
cd vendor/aws
git submodule add -f https://github.com/awslabs/aws-c-s3.git
cd ~/path/to/project
cmake -S vendor/aws/aws-c-s3 -B vendor/aws/aws-c-s3/build -DCMAKE_INSTALL_PREFIX=$(pwd)/vendor/aws/install -DCMAKE_PREFIX_PATH=$(pwd)/vendor/aws/install -Daws-c-common_DIR=$(pwd)/vendor/aws/install/lib/cmake/aws-c-common -Daws-c-cal_DIR=$(pwd)/vendor/aws/install/lib/cmake/aws-c-cal -Daws-c-io_DIR=$(pwd)/vendor/aws/install/lib/cmake/aws-c-io -Daws-c-http_DIR=$(pwd)/vendor/aws/install/lib/cmake/aws-c-http -Daws-c-sdkutils_DIR=$(pwd)/vendor/aws/install/lib/cmake/aws-c-sdkutils -Daws-c-auth_DIR=$(pwd)/vendor/aws/install/lib/cmake/aws-c-auth -Daws-checksums_DIR=$(pwd)/vendor/aws/install/lib/cmake/aws-checksums -Daws-c-compression_DIR=$(pwd)/vendor/aws/install/lib/cmake/aws-c-compression
cmake --build vendor/aws/aws-c-s3/build --target install
```

## Using the Libraries

After building, the libraries and headers will be installed in:
- AWS Libraries: `vendor/aws/install/lib/`
- AWS Headers: `vendor/aws/install/include/`
- Arrow Libraries: `/usr/local/lib/` (or custom prefix)
- npy_array: `/usr/local/lib/` (or custom prefix)

To test if everything is working correctly, run:

```bash
vendor/aws/install/bin/s3 --help
```