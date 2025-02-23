import pytest
import numpy as np
from embedding_bridge.store import VectorMemoryStore
import openai
import voyageai
import os
from dotenv import load_dotenv

# Load environment variables
load_dotenv()

def get_openai_embedding(text):
    """Get real OpenAI embedding - this is what users actually do"""
    client = openai.Client(api_key=os.getenv("OPENAI_API_KEY"))
    response = client.embeddings.create(
        model="text-embedding-3-small",
        input=text
    )
    return np.array(response.data[0].embedding)

def get_voyage_embedding(text):
    """Get real Voyage embedding - another real world use case"""
    voyage = voyageai.Client(api_key=os.getenv("VOYAGE_API_KEY"))
    response = voyage.embed(
        text=text,
        model="voyage-2"
    )
    return np.array(response)

def test_store_and_retrieve():
    """Can we store a vector and get it back? That's what matters first."""
    memory = VectorMemoryStore(db_path=":memory:")
    vector = np.random.rand(1536).astype(np.float32)  # Real OpenAI dimension
    
    # Store with minimum required metadata
    vector_id = memory.store_memory(
        vector,
        {"text": "test document"},
        "test-model"
    )
    
    # Can we get it back?
    result = memory.get_memory_evolution(vector_id)
    stored = result["versions"][0]
    
    # Basic sanity checks
    assert np.allclose(stored["vector"], vector)
    assert stored["metadata"]["text"] == "test document"
    assert stored["model_version"] == "test-model"

@pytest.mark.integration
def test_real_evolution():
    """Test what users actually do: Update content and change models"""
    memory = VectorMemoryStore(db_path=":memory:")
    
    # Real use case: Store initial version with OpenAI
    text = "Python is a programming language"
    v1 = get_openai_embedding(text)
    id = memory.store_memory(
        v1,
        {"text": text, "version": 1},
        "text-embedding-3-small"
    )
    
    # User updates content with different model (now using Voyage instead of Cohere)
    updated_text = "Python is a high-level programming language"
    v2 = get_voyage_embedding(updated_text)
    memory.store_memory(
        v2,
        {"text": updated_text, "version": 2},
        "voyage-2",
        vector_id=id
    )
    
    # Can we track the evolution?
    evolution = memory.get_memory_evolution(id)
    assert len(evolution["versions"]) == 2
    assert evolution["versions"][0]["metadata"]["version"] == 2
    assert evolution["versions"][1]["metadata"]["version"] == 1

def test_handles_failures():
    """Does it fail gracefully when things go wrong?"""
    memory = VectorMemoryStore(db_path=":memory:")
    
    # Wrong dimension (OpenAI is 1536)
    with pytest.raises(ValueError, match="dimension"):
        memory.store_memory(
            np.random.rand(10),
            {"text": "bad dimension"},
            "test"
        )
    
    # Missing required metadata
    with pytest.raises(ValueError, match="text"):
        memory.store_memory(
            np.random.rand(1536),
            {"optional": "data"},
            "test"
        )
    
    # Invalid vector_id for update
    with pytest.raises(ValueError, match="id"):
        memory.store_memory(
            np.random.rand(1536),
            {"text": "test"},
            "test",
            vector_id="nonexistent"
        )

@pytest.mark.integration
def test_migration_safety():
    """Can we migrate without losing data?"""
    memory = VectorMemoryStore(db_path=":memory:")
    
    # Store 5 real documents (keeping it small for API costs)
    texts = [
        "Python is great for data science",
        "JavaScript runs in the browser",
        "Rust is memory safe",
        "Go is good for concurrency",
        "Java is widely used in enterprise"
    ]
    
    # Store with OpenAI
    ids = []
    for text in texts:
        v = get_openai_embedding(text)
        id = memory.store_memory(
            v,
            {"text": text, "model": "openai"},
            "text-embedding-3-small"
        )
        ids.append(id)
    
    # Migrate to Voyage (instead of Cohere)
    for id, text in zip(ids, texts):
        v = get_voyage_embedding(text)
        memory.store_memory(
            v,
            {"text": text, "model": "voyage"},
            "voyage-2",
            vector_id=id
        )
    
    # Verify all documents are tracked
    for id, text in zip(ids, texts):
        evolution = memory.get_memory_evolution(id)
        assert len(evolution["versions"]) == 2
        assert evolution["versions"][0]["metadata"]["model"] == "voyage"
        assert evolution["versions"][1]["metadata"]["model"] == "openai"

@pytest.mark.integration
def test_real_world_search():
    """Can we actually find similar content?"""
    memory = VectorMemoryStore(db_path=":memory:")
    
    # Store some real programming language descriptions
    descriptions = {
        "python": "Python is a high-level programming language known for readability",
        "javascript": "JavaScript is a scripting language for web browsers",
        "rust": "Rust is a systems programming language focused on safety and performance",
        "go": "Go is a concurrent programming language developed by Google",
        "java": "Java is an object-oriented programming language used in enterprise"
    }
    
    # Store all with OpenAI embeddings
    for lang, desc in descriptions.items():
        v = get_openai_embedding(desc)
        memory.store_memory(
            v,
            {"text": desc, "language": lang},
            "text-embedding-3-small"
        )
    
    # Real world search scenarios
    search_queries = [
        ("web development language", "javascript"),  # Should find JavaScript
        ("safe systems programming", "rust"),        # Should find Rust
        ("enterprise software", "java"),             # Should find Java
        ("easy to read code", "python"),            # Should find Python
        ("concurrent programming", "go")             # Should find Go
    ]
    
    # Test each search
    for query, expected_lang in search_queries:
        query_vector = get_openai_embedding(query)
        results = memory.search_by_vector(query_vector, limit=1)
        assert len(results) > 0
        assert results[0]["metadata"]["language"] == expected_lang 

@pytest.mark.integration
def test_two_way_evolution():
    """Test evolution across two different embedding providers"""
    memory = VectorMemoryStore(db_path=":memory:")
    
    # Start with a simple text
    text = "Machine learning is transforming technology"
    
    # 1. Initial version with OpenAI
    v1 = get_openai_embedding(text)
    id = memory.store_memory(
        v1,
        {"text": text, "version": 1, "provider": "openai"},
        "text-embedding-3-small"
    )
    
    # 2. Update with Voyage
    v2 = get_voyage_embedding(text)
    memory.store_memory(
        v2,
        {"text": text, "version": 2, "provider": "voyage"},
        "voyage-2",
        vector_id=id
    )
    
    # Verify evolution tracking
    evolution = memory.get_memory_evolution(id)
    
    # Check we have both versions
    assert len(evolution["versions"]) == 2
    
    # Verify version order (newest first)
    assert evolution["versions"][0]["metadata"]["provider"] == "voyage"
    assert evolution["versions"][1]["metadata"]["provider"] == "openai"
    
    # Verify changes are tracked
    assert len(evolution["changes"]) == 1  # Should have 1 transition 