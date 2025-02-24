from setuptools import setup, find_namespace_packages
import os
import re

# Get the absolute path of the current directory
here = os.path.abspath(os.path.dirname(__file__))

# Extract version from types.h
def get_version():
    types_h_path = os.path.join(here, "src", "core", "types.h")
    with open(types_h_path, 'r') as f:
        content = f.read()
    
    # Extract version string from types.h
    match = re.search(r'#define\s+EB_VERSION_STR\s+"([^"]+)"', content)
    if match:
        return match.group(1)
    return "0.1.0"  # Default if not found

setup(
    name="embedding_bridge",
    version=get_version(),
    packages=find_namespace_packages(where="src/python", include=["embedding_bridge*"]),
    package_dir={"": "src/python"},
    install_requires=[
        line.strip()
        for line in open(os.path.join(here, "requirements.txt"))
        if line.strip() and not line.startswith("#")
    ],
    python_requires=">=3.8",
) 