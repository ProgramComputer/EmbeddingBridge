#!/usr/bin/env python3
"""
EmbeddingBridge - Setup Script
Copyright (C) 2024 ProgramComputer

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
"""

from setuptools import setup, find_packages
import os
import shutil
import sys
from pathlib import Path

# Copy shared library to package directory if available
def copy_shared_lib():
    """Copy shared library to package directory"""
    # Define source paths
    source_paths = [
        "../lib",  # Project root lib directory
        "../../lib",  # One level up from project root
    ]
    
    # Define lib names based on platform
    if sys.platform.startswith('linux'):
        lib_names = ["libembedding_bridge.so", "libembeddingbridge.so"]
    elif sys.platform.startswith('darwin'):
        lib_names = ["libembedding_bridge.dylib", "libembeddingbridge.dylib"]
    elif sys.platform.startswith('win'):
        lib_names = ["embedding_bridge.dll", "embeddingbridge.dll"]
    else:
        lib_names = [
            "libembedding_bridge.so", "libembeddingbridge.so", 
            "libembedding_bridge.dylib", "libembeddingbridge.dylib", 
            "embedding_bridge.dll", "embeddingbridge.dll"
        ]
    
    # Create lib directory in package
    os.makedirs("embeddingbridge/lib", exist_ok=True)
    
    # Try to find and copy the library
    for source_path in source_paths:
        for lib_name in lib_names:
            source_file = os.path.join(source_path, lib_name)
            if os.path.exists(source_file):
                print(f"Copying {source_file} to embeddingbridge/lib/")
                shutil.copy2(source_file, f"embeddingbridge/lib/{lib_name}")
                return True
    
    print("Warning: Could not find shared library. Please build it first.")
    return False

# Try to copy the shared library
copy_shared_lib()

# Read requirements from file
with open('requirements.txt') as f:
    requirements = f.read().splitlines()

# Read long description from README
long_description = """
# EmbeddingBridge

A high-performance vector embedding storage and management system.

## Features

- Fast vector storage and retrieval
- Integration with S3 for dataset management
- Compatible with Pinecone dataset format
- Python interface with ctypes bindings to C core
"""

setup(
    name="embeddingbridge",
    version="0.1.0",
    author="ProgramComputer",
    author_email="info@programcomputer.example",
    description="Vector embedding storage and management",
    long_description=long_description,
    long_description_content_type="text/markdown",
    url="https://github.com/yourusername/embeddingbridge",
    packages=find_packages(),
    classifiers=[
        "Development Status :: 3 - Alpha",
        "Intended Audience :: Developers",
        "Intended Audience :: Science/Research",
        "License :: OSI Approved :: GNU General Public License v2 (GPLv2)",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.7",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Topic :: Scientific/Engineering :: Artificial Intelligence",
    ],
    python_requires=">=3.7",
    install_requires=requirements,
    include_package_data=True,
    package_data={
        "embeddingbridge": ["lib/*.so", "lib/*.dylib", "lib/*.dll"],
    },
) 