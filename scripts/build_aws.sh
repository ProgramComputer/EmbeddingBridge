#!/bin/bash
set -e

git submodule update --init --recursive --remote

# Create directory structure
mkdir -p vendor/aws/install

# Get absolute path to the install directory
INSTALL_DIR=$(pwd)/vendor/aws/install

echo "Building AWS libraries in vendor/aws with install directory: $INSTALL_DIR"

# 1. aws-lc (AWS LibCrypto)
if [ ! -d "vendor/aws/aws-lc" ]; then
  git submodule add -f https://github.com/aws/aws-lc.git vendor/aws/aws-lc
else
  echo "aws-lc directory already exists, skipping git clone"
fi
cmake -S vendor/aws/aws-lc -B vendor/aws/aws-lc/build -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR
cmake --build vendor/aws/aws-lc/build --target install

# 2. s2n-tls
if [ ! -d "vendor/aws/s2n-tls" ]; then
  git submodule add -f https://github.com/aws/s2n-tls.git vendor/aws/s2n-tls
else
  echo "s2n-tls directory already exists, skipping git clone"
fi
cmake -S vendor/aws/s2n-tls -B vendor/aws/s2n-tls/build -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR -DCMAKE_PREFIX_PATH=$INSTALL_DIR
cmake --build vendor/aws/s2n-tls/build --target install

# 3. aws-c-common
if [ ! -d "vendor/aws/aws-c-common" ]; then
  git submodule add -f https://github.com/awslabs/aws-c-common.git vendor/aws/aws-c-common
else
  echo "aws-c-common directory already exists, skipping git clone"
fi
cmake -S vendor/aws/aws-c-common -B vendor/aws/aws-c-common/build -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR -DCMAKE_PREFIX_PATH=$INSTALL_DIR
cmake --build vendor/aws/aws-c-common/build --target install

# 4. aws-checksums
if [ ! -d "vendor/aws/aws-checksums" ]; then
  git submodule add -f https://github.com/awslabs/aws-checksums.git vendor/aws/aws-checksums
else
  echo "aws-checksums directory already exists, skipping git clone"
fi
cmake -S vendor/aws/aws-checksums -B vendor/aws/aws-checksums/build -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR -DCMAKE_PREFIX_PATH=$INSTALL_DIR -Daws-c-common_DIR=$INSTALL_DIR/lib/cmake/aws-c-common
cmake --build vendor/aws/aws-checksums/build --target install

# 5. aws-c-cal
if [ ! -d "vendor/aws/aws-c-cal" ]; then
  git submodule add -f https://github.com/awslabs/aws-c-cal.git vendor/aws/aws-c-cal
else
  echo "aws-c-cal directory already exists, skipping git clone"
fi
cmake -S vendor/aws/aws-c-cal -B vendor/aws/aws-c-cal/build -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR -DCMAKE_PREFIX_PATH=$INSTALL_DIR -Daws-c-common_DIR=$INSTALL_DIR/lib/cmake/aws-c-common
cmake --build vendor/aws/aws-c-cal/build --target install

# 6. aws-c-io
if [ ! -d "vendor/aws/aws-c-io" ]; then
  git submodule add -f https://github.com/awslabs/aws-c-io.git vendor/aws/aws-c-io
else
  echo "aws-c-io directory already exists, skipping git clone"
fi
cmake -S vendor/aws/aws-c-io -B vendor/aws/aws-c-io/build -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR -DCMAKE_PREFIX_PATH=$INSTALL_DIR -Daws-c-common_DIR=$INSTALL_DIR/lib/cmake/aws-c-common -Daws-c-cal_DIR=$INSTALL_DIR/lib/cmake/aws-c-cal
cmake --build vendor/aws/aws-c-io/build --target install

# 7. aws-c-compression
if [ ! -d "vendor/aws/aws-c-compression" ]; then
  git submodule add -f https://github.com/awslabs/aws-c-compression.git vendor/aws/aws-c-compression
else
  echo "aws-c-compression directory already exists, skipping git clone"
fi
cmake -S vendor/aws/aws-c-compression -B vendor/aws/aws-c-compression/build -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR -DCMAKE_PREFIX_PATH=$INSTALL_DIR -Daws-c-common_DIR=$INSTALL_DIR/lib/cmake/aws-c-common
cmake --build vendor/aws/aws-c-compression/build --target install

# 8. aws-c-http
if [ ! -d "vendor/aws/aws-c-http" ]; then
  git submodule add -f https://github.com/awslabs/aws-c-http.git vendor/aws/aws-c-http
else
  echo "aws-c-http directory already exists, skipping git clone"
fi
cmake -S vendor/aws/aws-c-http -B vendor/aws/aws-c-http/build -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR -DCMAKE_PREFIX_PATH=$INSTALL_DIR -Daws-c-common_DIR=$INSTALL_DIR/lib/cmake/aws-c-common -Daws-c-compression_DIR=$INSTALL_DIR/lib/cmake/aws-c-compression -Daws-c-io_DIR=$INSTALL_DIR/lib/cmake/aws-c-io
cmake --build vendor/aws/aws-c-http/build --target install

# 9. aws-c-sdkutils
if [ ! -d "vendor/aws/aws-c-sdkutils" ]; then
  git submodule add -f https://github.com/awslabs/aws-c-sdkutils.git vendor/aws/aws-c-sdkutils
else
  echo "aws-c-sdkutils directory already exists, skipping git clone"
fi
cmake -S vendor/aws/aws-c-sdkutils -B vendor/aws/aws-c-sdkutils/build -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR -DCMAKE_PREFIX_PATH=$INSTALL_DIR -Daws-c-common_DIR=$INSTALL_DIR/lib/cmake/aws-c-common
cmake --build vendor/aws/aws-c-sdkutils/build --target install

# 10. aws-c-auth
if [ ! -d "vendor/aws/aws-c-auth" ]; then
  git submodule add -f https://github.com/awslabs/aws-c-auth.git vendor/aws/aws-c-auth
else
  echo "aws-c-auth directory already exists, skipping git clone"
fi
cmake -S vendor/aws/aws-c-auth -B vendor/aws/aws-c-auth/build -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR -DCMAKE_PREFIX_PATH=$INSTALL_DIR -Daws-c-common_DIR=$INSTALL_DIR/lib/cmake/aws-c-common -Daws-c-cal_DIR=$INSTALL_DIR/lib/cmake/aws-c-cal -Daws-c-io_DIR=$INSTALL_DIR/lib/cmake/aws-c-io -Daws-c-http_DIR=$INSTALL_DIR/lib/cmake/aws-c-http -Daws-c-sdkutils_DIR=$INSTALL_DIR/lib/cmake/aws-c-sdkutils
cmake --build vendor/aws/aws-c-auth/build --target install

# 11. aws-c-s3
if [ ! -d "vendor/aws/aws-c-s3" ]; then
  git submodule add -f https://github.com/awslabs/aws-c-s3.git vendor/aws/aws-c-s3
else
  echo "aws-c-s3 directory already exists, skipping git clone"
fi
cmake -S vendor/aws/aws-c-s3 -B vendor/aws/aws-c-s3/build -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR -DCMAKE_PREFIX_PATH=$INSTALL_DIR
cmake --build vendor/aws/aws-c-s3/build --target install

echo "All AWS libraries built successfully!"
echo "Libraries installed in: $INSTALL_DIR/lib"
echo "Headers installed in: $INSTALL_DIR/include"
echo "Testing S3 executable:"
$INSTALL_DIR/bin/s3 --help 