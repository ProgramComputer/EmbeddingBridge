from setuptools import setup, find_namespace_packages
import os

# Get the absolute path of the current directory
here = os.path.abspath(os.path.dirname(__file__))

setup(
    name="embedding_bridge",
    version="0.1.0",
    packages=find_namespace_packages(where="src/python", include=["embedding_bridge*"]),
    package_dir={"": "src/python"},
    install_requires=[
        line.strip()
        for line in open(os.path.join(here, "requirements.txt"))
        if line.strip() and not line.startswith("#")
    ],
    python_requires=">=3.8",
) 