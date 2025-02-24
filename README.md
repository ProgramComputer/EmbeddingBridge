# Embedding Bridge

A command-line tool for managing and versioning embedding vectors. Think of it as "Git for Embeddings" - helping you track, compare, and manage semantic changes in your ML models.

## Features

- Store and version control your embedding vectors
- Compare semantic similarity between embeddings
- Track embedding history and changes
- Roll back to previous embedding versions
- Support for multiple embedding models

## Installation

```bash
make clean
make DEBUG=0 all
make DEBUG=0 install
```

## Quick Start

```bash
# Register a model
eb model register text-embedding-3-small --dimensions 1536 --normalize

# Store an embedding
eb store --embedding vector.bin --dims 1536 document.txt

# Check status
eb status document.txt

# Compare embeddings
eb diff <hash1> <hash2>

# Roll back to previous version
eb rollback <hash> document.txt
```

## Core Commands

### Model Management
```bash
# Register a new model
eb model register <model-name> --dimensions <dims> [--normalize]

# List registered models
eb model list
```

### Embedding Operations
```bash
# Store embedding from binary file
eb store --embedding vector.bin --dims 1536 file.txt

# Store embedding from numpy file
eb store --embedding vector.npy file.txt

# Check embedding status
eb status file.txt
eb status -v file.txt  # verbose output

# Compare embeddings
eb diff <hash1> <hash2>

# Roll back to previous version
eb rollback <hash> file.txt
```

## Contributing

We welcome contributions! Please see our [Contributing Guidelines](CONTRIBUTING.md) for details.

## License

This project is licensed under the GNU General Public License v2.0 - see the [LICENSE](LICENSE) file for details. 
