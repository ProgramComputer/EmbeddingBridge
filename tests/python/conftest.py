"""Test configuration and fixtures for the embedding_bridge package."""

import pytest
import numpy as np
from typing import Dict, Any, Optional
from datetime import datetime
from embedding_bridge.store import VectorMemoryStore
import os
from dotenv import load_dotenv

# Load environment variables
load_dotenv()

class MockEmbeddingBridge:
    """Mock implementation of the C bridge for testing."""
    
    def __init__(self):
        self.stored_vectors = {}
        self.next_id = 1
        self.vector_versions = {}  # Track versions of each vector
    
    def store_vector(self, vector: np.ndarray, metadata: Dict[str, Any], model_version: str, vector_id: Optional[int] = None) -> int:
        """Mock storing a vector."""
        if vector_id is None:
            vector_id = self.next_id
            self.next_id += 1
        
        version = {
            "id": vector_id,
            "vector": vector.copy(),
            "metadata": metadata.copy(),
            "model_version": model_version,
            "timestamp": datetime.now().timestamp()
        }
        
        if vector_id not in self.vector_versions:
            self.vector_versions[vector_id] = []
        self.vector_versions[vector_id].append(version)
        
        return vector_id
    
    def get_vector_evolution(self, vector_id: int, from_time: Optional[float] = None, 
                           to_time: Optional[float] = None) -> Dict[str, Any]:
        """Mock getting vector evolution."""
        if vector_id not in self.vector_versions:
            return {"versions": [], "changes": []}
        
        versions = self.vector_versions[vector_id]
        filtered_versions = []
        
        for version in versions:
            timestamp = version["timestamp"]
            if ((from_time is None or timestamp >= from_time) and 
                (to_time is None or timestamp <= to_time)):
                filtered_versions.append({
                    "id": str(version["id"]),
                    "vector": version["vector"],
                    "metadata": version["metadata"],
                    "model_version": version["model_version"],
                    "timestamp": datetime.fromtimestamp(version["timestamp"])
                })
        
        # Sort versions by timestamp, newest first
        filtered_versions.sort(key=lambda x: x["timestamp"], reverse=True)
        
        # Generate changes between consecutive versions
        changes = []
        for i in range(len(filtered_versions) - 1):
            changes.append({
                "from_version": filtered_versions[i + 1]["id"],
                "to_version": filtered_versions[i]["id"],
                "cosine_similarity": 0.95,  # Mock similarity value
                "semantic_preservation": 0.9,  # Mock preservation value
                "from_metadata": filtered_versions[i + 1]["metadata"],
                "to_metadata": filtered_versions[i]["metadata"],
                "from_model": filtered_versions[i + 1]["model_version"],
                "to_model": filtered_versions[i]["model_version"],
                "timestamp": filtered_versions[i]["timestamp"]
            })
        
        return {
            "versions": filtered_versions,
            "changes": changes
        }

@pytest.fixture
def mock_bridge():
    """Fixture providing a mock bridge implementation."""
    return MockEmbeddingBridge()

@pytest.fixture
def test_vector():
    """Fixture providing a test vector."""
    return np.random.rand(1536).astype(np.float32)

@pytest.fixture
def test_metadata():
    """Fixture providing test metadata."""
    return {"test": "data", "source": "test"}

@pytest.fixture
def real_memory_store():
    """Fixture for tests using real embedding models"""
    store = VectorMemoryStore(db_path=":memory:")
    yield store

@pytest.fixture
def sample_texts():
    """Real world text samples for testing"""
    return [
        "Python is great for data science and machine learning",
        "JavaScript makes web pages interactive",
        "Rust ensures memory safety without garbage collection",
        "Go excels at building concurrent applications",
        "Java powers enterprise applications worldwide"
    ]

@pytest.fixture
def api_keys_present():
    """Skip integration tests if API keys aren't available"""
    openai_key = os.getenv("OPENAI_API_KEY")
    cohere_key = os.getenv("COHERE_API_KEY")
    voyage_key = os.getenv("VOYAGE_API_KEY")
    
    if not all([openai_key, cohere_key, voyage_key]):
        missing = []
        if not openai_key: missing.append("OpenAI")
        if not cohere_key: missing.append("Cohere")
        if not voyage_key: missing.append("Voyage")
        pytest.skip(f"Missing API keys: {', '.join(missing)}")
    return True

def pytest_configure(config):
    """Register custom markers."""
    config.addinivalue_line(
        "markers",
        "integration: mark test as an integration test that requires external services"
    ) 