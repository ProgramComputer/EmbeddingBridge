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
eb init

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

# Create and manage sets
eb set create experimental
eb set switch experimental
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

### Set Management
```bash
# Create a new set of embeddings
eb set create <name> [--desc="Description"] [--base=<base-set>]

# List available sets
eb set list
eb set list --verbose

# Switch between sets
eb set switch <name>

# Show current set status
eb set status

# Compare differences between sets
eb set diff <set1> <set2>

# Merge sets (future functionality)
eb set merge <source-set> [--target=<target-set>] [--strategy=<strategy>]

# Delete a set
eb set delete <name> [--force]
```

### Remote Operations (Future Functionality)
```bash
# Add a remote
eb remote add <name> <url>

# List remote sets
eb remote sets <remote>

# Push a set to remote
eb remote push <set-name> [remote]

# Pull a set from remote
eb remote pull <set-name> [remote]
```

## Contributing

We welcome contributions! Please see our [Contributing Guidelines](CONTRIBUTING.md) for details.

## License

This project is licensed under the GNU General Public License v2.0 - see the [LICENSE](LICENSE) file for details. 
