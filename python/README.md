# EmbeddingBridge Python Package

A Python interface to the EmbeddingBridge vector database with dataset management capabilities.

## Features

- Python ctypes bindings to the C core
- Dataset management with S3 integration
- Vector similarity search
- Compatibility with Pinecone datasets
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

# Add vectors
store.add_vector(
    id="doc1",
    vector=[0.1, 0.2, ...],  # Your vector values
    metadata={"text": "Example document", "source": "wiki"}
)

# Search for similar vectors
results = store.search([0.1, 0.2, ...], top_k=5)
for result in results:
    print(f"ID: {result['id']}, Score: {result['score']}")
    print(f"Metadata: {result['metadata']}")
```

### Dataset Management

```python
from embeddingbridge import datasets

# List available datasets
dataset_list = datasets.list_datasets()
print(dataset_list)

# Load a dataset
dataset = datasets.load_dataset("my-dataset")

# Get dataset info
print(f"Dimension: {dataset.dimension}")
print(f"Documents: {len(dataset)}")

# Search for similar vectors
query_vector = [0.1, 0.2, ...]  # Your query vector
results = dataset.search(query_vector, top_k=10)
for id, score in results:
    print(f"ID: {id}, Score: {score}")

# Save a dataset to S3
dataset.save("s3://my-bucket/datasets/my-dataset")

# Load a dataset from S3
s3_dataset = datasets.Dataset.from_path("s3://my-bucket/datasets/my-dataset")
```

## API Reference

### `EmbeddingStore`

- `__init__(path, dimension=None)`: Initialize a new embedding store or open an existing one
- `add_vector(id, vector, metadata=None)`: Add a vector to the store
- `search(query_vector, top_k=10)`: Search for similar vectors
- `get_vector(id)`: Get a vector by ID
- `delete_vector(id)`: Delete a vector by ID
- `get_metadata(id)`: Get metadata for a vector
- `dimension`: Property returning the dimension of vectors
- `count`: Property returning the number of vectors

### `Dataset`

- `from_path(path)`: Load a dataset from a local path or S3 bucket
- `save(path, overwrite=False)`: Save dataset to local path or S3 bucket
- `iter_documents(batch_size=100)`: Iterate through documents in batches
- `search(query_vector, top_k=10)`: Search for similar vectors
- `dimension`: Property returning the dimension of vectors
- `documents`: DataFrame containing the documents

### Helper Functions

- `list_datasets(as_df=False)`: List available datasets
- `load_dataset(name)`: Load a dataset by name

## License

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version. 