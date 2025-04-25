# EmbeddingBridge Python Package

A Python interface to EmbeddingBridge for embedding storage and command execution.

## Features

- Python ctypes bindings to the C core
- Embedding store and retrieval
- CLI wrapper to programmatically run EmbeddingBridge commands
- Vector similarity search
- Simple and intuitive API

## Installation

### From Source

```bash
pip install -e .
```

### Requirements

- Python 3.7+
- NumPy
- pandas
- pyarrow
- boto3 (for S3 support)
- zstandard

## Usage

### Embedding Store

```python
from embeddingbridge import EmbeddingStore

# Create a new store
store = EmbeddingStore("path/to/store", dimension=384)

# Add a vector
vector_id = store.add_vector([0.1, 0.2, ...])  # Your vector values

# Or add from a file
file_id = store.add_embedding_from_file("path/to/embedding.npy")

# Retrieve a vector and metadata by ID
vector_data = store.get_vector(vector_id)
print(vector_data["vector"], vector_data["metadata"])

```

### CLI Wrapper

```python
from embeddingbridge import EmbeddingBridge

# Initialize repository
embr = EmbeddingBridge()
init_result = embr.init()

# Register a model
embr.model_register("text-embedding-3-small", dimensions=1536, normalize=True)

# Store an embedding file
store_result = embr.store("vector.npy", "document.txt")
print(store_result.stdout)
```

## API Reference

### `EmbeddingStore`

- `__init__(path, dimension=None)`: Initialize a new embedding store or open an existing one
- `add_vector(vector)`: Add a vector to the store and return its ID
- `add_embedding_from_file(file_path)`: Add an embedding from a file and return its ID
- `get_vector(id)`: Retrieve a stored vector and metadata by ID
- `close()`: Close the store
- `dimension`: Property returning the dimension of vectors

### `EmbeddingBridge`

- `__init__(working_dir=None)`: Initialize an EmbeddingBridge CLI wrapper (optional working directory)
- `init()`: Initialize an EmbeddingBridge repository
- `model_register(name, dimensions, normalize=False, description=None)`: Register a new model
- `model_list()`: List registered models
- `store(embedding_file, source_file)`: Store an embedding file
- `status(source_file=None, verbose=False)`: Get status of a file
- `log(source_file)`: Display log of versions
- `diff(hash1, hash2)`: Compare versions of embeddings
- `rm(source_file, model=None, cached=False)`: Remove an embedding
- `rollback(hash_value, source_file, model=None)`: Roll back to a previous version
- `config(*cmd_args)`: Run a config command with provided arguments
- `gc(*options)`: Run garbage collection (e.g., `-n` for dry-run)
- `get(remote, path)`: Download a file or directory from a remote repository
- `merge(source_set, target_set=None, strategy=None)`: Merge embeddings from one set to another
- `push(remote, set_name=None)`: Push a set to a remote
- `pull(remote, set_name=None)`: Pull a set from a remote
- `remote(*subargs)`: Manage remotes (add, list, remove, etc.)