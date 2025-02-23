from typing import Dict, Any, Optional
import numpy as np
from datetime import datetime
import ctypes
import json
from pathlib import Path
from .bridge import EmbeddingBridge, DataType, Status

class VectorMemoryStore:
    def __init__(self, db_path: str = ":memory:", bridge: Optional[Any] = None):
        """Initialize the vector memory store using C backend or mock bridge"""
        if bridge is not None:
            self.bridge = bridge
            self._is_mock = True
        else:
            self.bridge = EmbeddingBridge()
            self._lib = self.bridge.lib  # Access the C library
            self._setup_bindings()
            self._is_mock = False
            
            # Initialize store
            self.store = ctypes.c_void_p()
            status = self._lib.eb_store_init(
                db_path.encode(),
                ctypes.byref(self.store)
            )
            if status != Status.SUCCESS:
                raise RuntimeError(f"Failed to initialize store: {Status(status).name}")
    
    def _setup_bindings(self):
        """Configure C function signatures for storage operations"""
        # Core operations
        self._lib.eb_store_memory.argtypes = [
            ctypes.c_void_p,     # store
            ctypes.c_void_p,     # embedding
            ctypes.c_void_p,     # metadata
            ctypes.c_char_p,     # model_version
            ctypes.POINTER(ctypes.c_uint64)  # out_id
        ]
        self._lib.eb_store_memory.restype = ctypes.c_int
        
        # Metadata operations
        self._lib.eb_metadata_append.argtypes = [
            ctypes.c_void_p,  # metadata
            ctypes.c_void_p   # next
        ]
        self._lib.eb_metadata_append.restype = None
        
        self._lib.eb_get_memory_evolution_with_changes.argtypes = [
            ctypes.c_void_p,  # store
            ctypes.c_uint64,  # vector_id
            ctypes.c_uint64,  # from_time
            ctypes.c_uint64,  # to_time
            ctypes.POINTER(ctypes.POINTER(self.bridge.StoredVector)),  # out_versions
            ctypes.POINTER(ctypes.c_size_t),  # out_version_count
            ctypes.POINTER(ctypes.POINTER(self.bridge.ComparisonResult)),  # out_changes
            ctypes.POINTER(ctypes.c_size_t)  # out_change_count
        ]
        self._lib.eb_get_memory_evolution_with_changes.restype = ctypes.c_int
        
        # Helper functions
        self._lib.eb_metadata_create.argtypes = [
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.POINTER(ctypes.c_void_p)
        ]
        self._lib.eb_metadata_create.restype = ctypes.c_int
        
        self._lib.eb_format_evolution.argtypes = [
            ctypes.POINTER(self.bridge.StoredVector),
            ctypes.c_size_t,
            ctypes.POINTER(self.bridge.ComparisonResult),
            ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_char_p),
            ctypes.POINTER(ctypes.c_size_t)
        ]
        self._lib.eb_format_evolution.restype = ctypes.c_int
    
    def _create_metadata(self, metadata: Dict[str, Any]) -> ctypes.c_void_p:
        """Convert Python dictionary to C metadata linked list"""
        if not metadata:
            return None
            
        meta_head = None
        last_meta = None
        
        for key, value in metadata.items():
            meta_ptr = ctypes.c_void_p()
            status = self._lib.eb_metadata_create(
                str(key).encode(),
                str(value).encode(),
                ctypes.byref(meta_ptr)
            )
            if status != Status.SUCCESS:
                if meta_head:
                    self._lib.eb_metadata_destroy(meta_head)
                raise RuntimeError(f"Failed to create metadata: {Status(status).name}")
            
            if not meta_head:
                meta_head = meta_ptr
            else:
                # Link the new node to the previous one
                self._lib.eb_metadata_append(last_meta, meta_ptr)
            
            last_meta = meta_ptr
        
        return meta_head
    
    def store_memory(self, 
                    embedding: np.ndarray, 
                    metadata: Dict[str, Any],
                    model_version: str,
                    vector_id: Optional[str] = None,
                    normalize: bool = False) -> str:
        """Store a memory vector with metadata and version tracking
        
        Args:
            embedding: The vector embedding to store
            metadata: Dictionary of metadata about the embedding
            model_version: Version/name of the model used
            vector_id: Optional ID of existing vector to update. If None, a new ID is generated.
            normalize: Whether to normalize the embedding to unit length. Default False to preserve model's scale.
        
        Returns:
            str: The ID of the stored vector
        """
        if embedding.ndim != 1:
            raise ValueError("Embedding must be a 1D array")
        
        if self._is_mock:
            # Use mock bridge
            vector_id_int = int(vector_id) if vector_id is not None else None
            return str(self.bridge.store_vector(embedding, metadata, model_version, vector_id_int))
        
        # Use C bridge
        # Ensure embedding is float32 and C-contiguous
        embedding = np.ascontiguousarray(embedding, dtype=np.float32)
        
        # Create embedding structure
        embedding_ptr = self.bridge._numpy_to_embedding(
            embedding.reshape(1, -1),  # Add batch dimension
            normalize=normalize  # Pass through normalize parameter
        )
        
        # Convert metadata to C format
        meta_ptr = self._create_metadata(metadata)
        
        # Prepare output ID
        out_id = ctypes.c_uint64(int(vector_id) if vector_id is not None else 0)
        
        print(f"DEBUG: Calling eb_store_memory with:")
        print(f"  - store: {self.store}")
        print(f"  - embedding_ptr: {embedding_ptr}")
        print(f"  - meta_ptr: {meta_ptr}")
        print(f"  - model_version: {model_version}")
        print(f"  - out_id (initial): {out_id.value}")
        
        status = self._lib.eb_store_memory(
            self.store,
            embedding_ptr,
            meta_ptr,
            model_version.encode(),
            ctypes.byref(out_id)  # Pass as output parameter
        )
        
        # Clean up
        self._lib.eb_destroy_embedding(embedding_ptr)
        if meta_ptr:
            self._lib.eb_metadata_destroy(meta_ptr)
        
        if status != Status.SUCCESS:
            raise RuntimeError(f"Failed to store memory: {Status(status).name}")
        
        print(f"DEBUG: Successfully stored memory with ID: {out_id.value}")
        return str(out_id.value)
    
    def get_memory_evolution(self,
                           memory_id: str,
                           from_time: Optional[datetime] = None,
                           to_time: Optional[datetime] = None) -> Dict[str, Any]:
        """Get the evolution history of a memory vector"""
        if self._is_mock:
            return self.bridge.get_vector_evolution(
                int(memory_id),
                from_time.timestamp() if from_time else None,
                to_time.timestamp() if to_time else None
            )
        
        # Convert times to Unix timestamps
        from_ts = int(from_time.timestamp()) if from_time else 0
        to_ts = int(to_time.timestamp()) if to_time else int(datetime.now().timestamp())
        
        # Get versions and changes from C
        versions_ptr = ctypes.POINTER(self.bridge.StoredVector)()
        version_count = ctypes.c_size_t()
        changes_ptr = ctypes.POINTER(self.bridge.ComparisonResult)()
        change_count = ctypes.c_size_t()
        
        status = self._lib.eb_get_memory_evolution_with_changes(
            self.store,
            int(memory_id),
            from_ts,
            to_ts,
            ctypes.byref(versions_ptr),
            ctypes.byref(version_count),
            ctypes.byref(changes_ptr),
            ctypes.byref(change_count)
        )
        
        if status != Status.SUCCESS:
            raise RuntimeError(f"Failed to get memory evolution: {Status(status).name}")
        
        # Convert versions to Python
        versions = []
        for i in range(version_count.value):
            # Get embedding data
            embedding_data = versions_ptr[i].embedding.contents
            vector_array = np.frombuffer(
                (ctypes.c_float * embedding_data.dimensions).from_address(
                    embedding_data.data
                ),
                dtype=np.float32
            )

            version = {
                "id": str(versions_ptr[i].id),
                "metadata": self._convert_metadata(versions_ptr[i].metadata),
                "model_version": versions_ptr[i].model_version.decode(),
                "timestamp": datetime.fromtimestamp(versions_ptr[i].timestamp),
                "parent_id": str(versions_ptr[i].parent_id) if versions_ptr[i].parent_id else None,
                "vector": vector_array  # Add the vector data
            }
            versions.append(version)
        
        # Convert changes to Python
        changes = []
        for i in range(change_count.value):
            change = {
                "cosine_similarity": changes_ptr[i].cosine_similarity,
                "euclidean_distance": changes_ptr[i].euclidean_distance,
                "semantic_preservation": changes_ptr[i].semantic_preservation,
                "method_used": self._get_comparison_method_name(changes_ptr[i].method_used),
                "from_version": str(versions[i]["id"]),
                "to_version": str(versions[i + 1]["id"]),
                "from_model": versions[i]["model_version"],
                "to_model": versions[i + 1]["model_version"],
                "from_metadata": versions[i]["metadata"],
                "to_metadata": versions[i + 1]["metadata"]
            }
            changes.append(change)
        
        return {
            "memory_id": str(memory_id),
            "versions": versions,
            "changes": changes
        }
    
    def _get_comparison_method_name(self, method_value: int) -> str:
        """Convert comparison method enum value to string"""
        method_map = {
            0: "COSINE",
            1: "PROJECTION",
            2: "SEMANTIC"
        }
        return method_map.get(method_value, "UNKNOWN")
    
    def _convert_stored_vector(self, vector_ptr) -> Dict[str, Any]:
        """Convert C stored vector to Python dictionary"""
        print(f"DEBUG: Converting vector at {vector_ptr}")
        print(f"DEBUG: Vector type: {type(vector_ptr)}")
        
        try:
            # If vector_ptr is already a StoredVector, use it directly
            if isinstance(vector_ptr, self.bridge.StoredVector):
                vector = vector_ptr
                print("DEBUG: Using StoredVector directly")
            else:
                # Otherwise cast from pointer
                print("DEBUG: Casting pointer to StoredVector")
                vector = ctypes.cast(vector_ptr, 
                                   ctypes.POINTER(self.bridge.StoredVector)).contents
                                   
            print(f"DEBUG: Successfully accessed StoredVector")
            print(f"DEBUG: Vector fields:")
            print(f"  - id: {vector.id}")
            print(f"  - model_version: {vector.model_version}")
            print(f"  - embedding: {vector.embedding}")
            print(f"  - metadata: {vector.metadata}")
            
            print("DEBUG: Accessing embedding data")
            embedding_data = vector.embedding.contents
            print(f"DEBUG: Embedding details:")
            print(f"  - dimensions: {embedding_data.dimensions}")
            print(f"  - count: {embedding_data.count}")
            print(f"  - data pointer: {embedding_data.data}")
            
            print("DEBUG: Creating numpy array from embedding data")
            vector_array = np.frombuffer(
                (ctypes.c_float * embedding_data.dimensions).from_address(
                    embedding_data.data
                ),
                dtype=np.float32
            )
            print(f"DEBUG: Successfully created numpy array with shape {vector_array.shape}")
            
            print("DEBUG: Converting metadata")
            metadata = self._convert_metadata(vector.metadata)
            print(f"DEBUG: Successfully converted metadata")
            
            result = {
                "id": str(vector.id),
                "vector": vector_array,
                "metadata": metadata,
                "model_version": vector.model_version.decode(),
                "timestamp": datetime.fromtimestamp(vector.timestamp)
            }
            print(f"DEBUG: Successfully created result dictionary")
            return result
        except Exception as e:
            print(f"DEBUG: Failed to convert vector: {str(e)}")
            print(f"DEBUG: Exception type: {type(e)}")
            raise
    
    def _convert_metadata(self, meta_ptr) -> Dict[str, Any]:
        """Convert C metadata linked list to Python dictionary"""
        print(f"DEBUG: Converting metadata from pointer {meta_ptr}")
        result = {}
        
        if not meta_ptr:
            print("DEBUG: No metadata to convert (null pointer)")
            return result
            
        try:
            current = ctypes.cast(meta_ptr, ctypes.POINTER(self.bridge.Metadata))
            while current:
                print(f"DEBUG: Processing metadata node at {current}")
                meta = current.contents
                print(f"DEBUG: Metadata contents - key: {meta.key}, value: {meta.value}, size: {meta.total_size}")
                
                if meta.key and meta.value:  # Check for valid pointers
                    result[meta.key.decode()] = meta.value.decode()
                
                current = meta.next
                print(f"DEBUG: Next metadata node: {current}")
            
            print(f"DEBUG: Successfully converted metadata: {result}")
            return result
        except Exception as e:
            print(f"DEBUG: Error converting metadata: {str(e)}")
            print(f"DEBUG: Exception type: {type(e)}")
            raise
    
    def format_evolution(self, evolution_data: Dict[str, Any]) -> str:
        """Format vector evolution in a Git-like display format"""
        if not evolution_data["versions"]:
            return "No versions found."
        
        # Convert Python data to C format
        versions = evolution_data["versions"]
        changes = evolution_data["changes"]
        
        # Prepare C arrays
        versions_array = (self.bridge.StoredVector * len(versions))()
        for i, v in enumerate(versions):
            versions_array[i].id = int(v["id"])
            versions_array[i].model_version = v["model_version"].encode()
            versions_array[i].timestamp = int(v["timestamp"].timestamp())
            
            # Convert metadata
            meta_head = None
            for key, value in v["metadata"].items():
                meta_ptr = ctypes.c_void_p()
                status = self._lib.eb_metadata_create(
                    str(key).encode(),
                    str(value).encode(),
                    ctypes.byref(meta_ptr)
                )
                if status != Status.SUCCESS:
                    raise RuntimeError(f"Failed to create metadata: {Status(status).name}")
                if not meta_head:
                    meta_head = meta_ptr
            versions_array[i].metadata = meta_head
        
        # Prepare changes array if there are any
        changes_array = None
        if changes:
            changes_array = (self.bridge.ComparisonResult * len(changes))()
            for i, c in enumerate(changes):
                changes_array[i].cosine_similarity = c["cosine_similarity"]
                changes_array[i].semantic_preservation = c["semantic_preservation"]
        
        # Call C formatting function
        formatted_ptr = ctypes.c_char_p()
        length = ctypes.c_size_t()
        
        status = self._lib.eb_format_evolution(
            versions_array,
            len(versions),
            changes_array,
            len(changes) if changes else 0,
            ctypes.byref(formatted_ptr),
            ctypes.byref(length)
        )
        
        if status != Status.SUCCESS:
            raise RuntimeError(f"Failed to format evolution: {Status(status).name}")
        
        # Convert result to Python string and clean up
        result = formatted_ptr.value.decode()
        self._lib.free(formatted_ptr)
        
        # Clean up metadata
        for version in versions_array:
            if version.metadata:
                self._lib.eb_metadata_destroy(version.metadata)
        
        return result
    
    def __str__(self) -> str:
        """String representation of the store"""
        return f"VectorMemoryStore(path='{self.store.contents.storage_path.decode()}', vectors={self.store.contents.vector_count})"
    
    def __del__(self):
        """Clean up C resources"""
        if hasattr(self, '_lib') and hasattr(self, 'store'):
            self._lib.eb_store_destroy(self.store) 