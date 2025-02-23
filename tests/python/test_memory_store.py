import pytest
import numpy as np
from datetime import datetime
from embedding_bridge.store import VectorMemoryStore
import voyageai
import os
import openai
from dotenv import load_dotenv

# Load environment variables from .env file
load_dotenv()

def test_basic_storage(mock_bridge, test_vector, test_metadata):
    """Test basic vector storage without OpenAI dependency"""
    memory = VectorMemoryStore(bridge=mock_bridge)
    
    # Store the vector without normalization
    vector_id = memory.store_memory(
        test_vector,
        test_metadata,
        "test-model",
        normalize=False  # Explicitly preserve original scale
    )
    
    # Retrieve the evolution
    evolution = memory.get_memory_evolution(vector_id)
    
    # Verify storage
    assert len(evolution["versions"]) == 1
    stored_version = evolution["versions"][0]
    assert stored_version["id"] == vector_id
    assert stored_version["metadata"] == test_metadata
    assert stored_version["model_version"] == "test-model"
    assert np.allclose(stored_version["vector"], test_vector)  # Should match exactly since we didn't normalize

def test_multiple_versions(mock_bridge):
    """Test storing and retrieving multiple versions of the same memory"""
    memory = VectorMemoryStore(bridge=mock_bridge)
    
    # Create test vectors
    vector1 = np.random.rand(1536).astype(np.float32)
    vector2 = np.random.rand(1536).astype(np.float32)
    
    # Store first version without normalization
    vector_id = memory.store_memory(
        vector1,
        {"version": 1, "text": "test"},  # Add required text field
        "model-v1",
        normalize=False  # Preserve original scale
    )
    
    # Store second version with same ID
    memory.store_memory(
        vector2,
        {"version": 2, "text": "test"},  # Add required text field
        "model-v2",
        vector_id=vector_id,  # Pass the same ID to update the existing memory
        normalize=False  # Preserve original scale
    )
    
    # Retrieve evolution
    evolution = memory.get_memory_evolution(vector_id)
    
    # Verify versions
    assert len(evolution["versions"]) == 2
    assert len(evolution["changes"]) == 1
    
    # Check version order
    assert evolution["versions"][0]["metadata"]["version"] == 2
    assert evolution["versions"][1]["metadata"]["version"] == 1
    
    # Verify change tracking
    change = evolution["changes"][0]
    assert change["from_model"] == "model-v1"
    assert change["to_model"] == "model-v2"
    
    # Verify vectors are preserved exactly
    assert np.allclose(evolution["versions"][0]["vector"], vector2)
    assert np.allclose(evolution["versions"][1]["vector"], vector1)

@pytest.mark.integration
def test_memory_evolution():
    """Integration test for memory evolution with OpenAI embeddings"""
    client = openai.Client(api_key=os.getenv("OPENAI_API_KEY"))

    # Initialize memory store with temporary database
    memory = VectorMemoryStore(db_path=":memory:")

    # Create test timestamps
    start_time = datetime.now()

    try:
        # Agent A's memory update
        print(f"DEBUG: Using OpenAI API key: {os.getenv('OPENAI_API_KEY')[:10]}...")
        try:
            response_a = client.embeddings.create(
                model="text-embedding-3-small",
                input="New information learned"
            )
            print("DEBUG: Successfully got embedding from OpenAI")
        except Exception as e:
            print(f"DEBUG: Failed to get small model embedding: {str(e)}")
            raise  # Fail instead of skip

        vector_a = response_a.data[0].embedding

        # Store with version
        memory_id = memory.store_memory(
            np.array(vector_a),
            {
                "source": "agent_a",
                "text": "New information learned"
            },
            "text-embedding-3-small"
        )

        # Wait a moment to ensure different timestamps
        middle_time = datetime.now()

        # Agent B's update to same memory
        try:
            response_b = client.embeddings.create(
                model="text-embedding-3-large",
                input="Updated information"
            )
            print("DEBUG: Successfully got large model embedding")
        except Exception as e:
            print(f"DEBUG: Failed to get large model embedding: {str(e)}")
            raise  # Fail instead of skip

        vector_b = response_b.data[0].embedding

        # Track evolution with parent_id to ensure version tracking
        memory.store_memory(
            np.array(vector_b),
            {
                "source": "agent_b",
                "text": "Updated information",
                "parent_id": memory_id  # Explicitly set parent relationship
            },
            "text-embedding-3-large"
        )

        end_time = datetime.now()

        # Analyze changes
        evolution = memory.get_memory_evolution(
            memory_id=memory_id,
            from_time=start_time,
            to_time=end_time
        )

        # Verify the evolution data
        assert len(evolution["versions"]) == 2, "Should track both versions despite different dimensions"
        assert evolution["versions"][0]["model_version"] == "text-embedding-3-small"
        assert evolution["versions"][1]["model_version"] == "text-embedding-3-large"
        
        # Verify cross-model comparison was used
        assert len(evolution["changes"]) > 0, "Should have comparison results"
        change = evolution["changes"][0]
        assert "method_used" in change, "Comparison method should be included"
        assert change["method_used"] in ["PROJECTION", "SEMANTIC"], f"Expected cross-model comparison method, got {change.get('method_used', 'MISSING')}"
        assert "cosine_similarity" in change, "Should have similarity score"
        assert "semantic_preservation" in change, "Should have preservation score"
        assert change["from_model"] == "text-embedding-3-small"
        assert change["to_model"] == "text-embedding-3-large"

    except Exception as e:
        print(f"DEBUG: Test failed: {str(e)}")
        raise

@pytest.mark.integration
def test_openai_model_comparison():
    """Integration test for comparing different OpenAI embedding models"""
    client = openai.Client(api_key=os.getenv("OPENAI_API_KEY"))
    print(f"DEBUG: Using OpenAI API key: {os.getenv('OPENAI_API_KEY')[:10]}...")
    
    memory = VectorMemoryStore(db_path=":memory:")
    test_text = "The quick brown fox jumps over the lazy dog"
    
    # Get embeddings from text-embedding-3-small
    try:
        response_small = client.embeddings.create(
            model="text-embedding-3-small",
            input=test_text
        )
        print("DEBUG: Successfully got small model embedding")
        print(f"DEBUG: Small model embedding length: {len(response_small.data[0].embedding)}")
    except Exception as e:
        print(f"DEBUG: Failed to get small model embedding: {str(e)}")
        raise  # Fail instead of skip
        
    vector_small = response_small.data[0].embedding
    
    # Store small model embedding
    memory_id = memory.store_memory(
        np.array(vector_small),
        {
            "model": "text-embedding-3-small",
            "text": test_text,
            "source": "openai-test"
        },
        "text-embedding-3-small",
        normalize=False  # OpenAI embeddings come pre-normalized
    )
    print("DEBUG: Successfully stored small model embedding")
    
    # Get embeddings from text-embedding-3-large
    try:
        response_large = client.embeddings.create(
            model="text-embedding-3-large",
            input=test_text
        )
        print("DEBUG: Successfully got large model embedding")
        print(f"DEBUG: Large model embedding length: {len(response_large.data[0].embedding)}")
    except Exception as e:
        print(f"DEBUG: Failed to get large model embedding: {str(e)}")
        raise  # Fail instead of skip
        
    vector_large = response_large.data[0].embedding
    
    # Store large model embedding
    memory.store_memory(
        np.array(vector_large),
        {
            "model": "text-embedding-3-large",
            "text": test_text,
            "source": "openai-test"
        },
        "text-embedding-3-large",
        normalize=False  # OpenAI embeddings come pre-normalized
    )
    print("DEBUG: Successfully stored large model embedding")
    
    # Get evolution and analyze changes
    evolution = memory.get_memory_evolution(memory_id)
    
    # Basic assertions
    assert len(evolution["versions"]) == 2, f"Expected 2 versions, got {len(evolution['versions'])}"
    assert len(evolution["changes"]) == 1, f"Expected 1 change, got {len(evolution['changes'])}"
    
    # Check model progression - versions are ordered by timestamp
    assert evolution["versions"][0]["model_version"] == "text-embedding-3-small"
    assert evolution["versions"][1]["model_version"] == "text-embedding-3-large"
    
    # Check metadata consistency
    for version in evolution["versions"]:
        assert version["metadata"]["text"] == test_text
        assert version["metadata"]["source"] == "openai-test"

@pytest.mark.integration
def test_voyage_embeddings():
    """Test integration with Voyage AI embeddings"""
    # Initialize Voyage client
    voyage = voyageai.Client(api_key=os.getenv("VOYAGE_API_KEY"))
    
    # Create test data
    test_text = "This is a test of Voyage AI embeddings"
    
    # Get embeddings from Voyage AI
    result = voyage.embed(
        texts=[test_text],
        model="voyage-3"
    )
    embedding = np.array(result.embeddings[0], dtype=np.float32)  # Convert to numpy array with correct dtype
    
    print(f"DEBUG: Voyage embedding shape: {embedding.shape}")
    print(f"DEBUG: Voyage embedding dtype: {embedding.dtype}")
    print(f"DEBUG: Voyage embedding min: {np.min(embedding)}")
    print(f"DEBUG: Voyage embedding max: {np.max(embedding)}")
    print(f"DEBUG: Voyage embedding mean: {np.mean(embedding)}")
    print(f"DEBUG: Voyage embedding std: {np.std(embedding)}")
    
    # Initialize memory store
    memory = VectorMemoryStore()
    
    # Store the embedding
    metadata = {
        "text": test_text,
        "timestamp": datetime.now().isoformat(),
        "source": "voyage-test"
    }
    
    vector_id = memory.store_memory(
        embedding,
        metadata,
        "voyage-3",
        normalize=False  # Preserve Voyage's original embedding scale
    )
    
    # Retrieve and verify
    evolution = memory.get_memory_evolution(vector_id)
    assert len(evolution["versions"]) == 1
    
    stored_version = evolution["versions"][0]
    assert stored_version["id"] == vector_id
    assert stored_version["metadata"] == metadata
    assert stored_version["model_version"] == "voyage-3"
    assert np.allclose(stored_version["vector"], embedding) 