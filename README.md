# Embedding Bridge

A command-line tool for managing and versioning embedding vectors. Think of it as "Git for Embeddings" - helping you track, compare, and manage semantic changes in your ML models.

## Features

- Store and version control your embedding vectors
- Compare semantic similarity between embeddings
- Track embedding history and changes
- Roll back to previous embedding versions
- Support for multiple embedding models
- Organize embeddings into sets for better management
- Prepare for remote collaboration (coming soon)

## Installation

```bash
make clean
make all
make install
```

## Quick Start

```bash
# Initialize
embr init

# Register a model
embr model register text-embedding-3-small --dimensions 1536 --normalize

# Store an embedding
embr store --embedding vector.bin --dims 1536 document.txt

# Check status
embr status document.txt

# Compare embeddings
embr diff <hash1> <hash2>

# Roll back to previous version
embr rollback <hash> document.txt

# Create and manage sets
embr set create experimental
embr set switch experimental
```

## Core Commands

### Model Management
```bash
# Register a new model
embr model register <model-name> --dimensions <dims> [--normalize]

# List registered models
embr model list
```

### Embedding Operations
```bash
# Store embedding from binary file
embr store --embedding vector.bin --dims 1536 file.txt

# Store embedding from numpy file
embr store --embedding vector.npy file.txt

# Check embedding status
embr status file.txt
embr status -v file.txt  # verbose output

# Compare embeddings
embr diff <hash1> <hash2>

# Roll back to previous version
embr rollback <hash> file.txt
```

### Set Management
```bash
# Create a new set of embeddings
embr set create <name> [--desc="Description"] [--base=<base-set>]

# List available sets
embr set list
embr set list --verbose

# Switch between sets
embr set switch <name>

# Show current set status
embr set status

# Compare differences between sets
embr set diff <set1> <set2>

# Merge sets (future functionality)
embr set merge <source-set> [--target=<target-set>] [--strategy=<strategy>]

# Delete a set
embr set delete <name> [--force]
```

### Remote Operations (Future Functionality)
```bash
# Add a remote
embr remote add <name> <url>

# List remote sets
embr remote sets <remote>

# Push a set to remote
embr remote push <set-name> [remote]

# Pull a set from remote
embr remote pull <set-name> [remote]
```

## Contributing

We welcome contributions! Please see our [Contributing Guidelines](CONTRIBUTING.md) for details.

## License

This project is licensed under the GNU General Public License v2.0 - see the [LICENSE](LICENSE) file for details. 
