from typing import List, Optional, Union
import ctypes
import numpy as np
from dataclasses import dataclass
from enum import IntEnum, Enum
import os
from datetime import datetime

# Enum definitions matching C
class DataType(IntEnum):
    FLOAT32 = 0
    FLOAT64 = 1
    INT8 = 2

class Status(IntEnum):
    SUCCESS = 0
    INVALID_INPUT = 1
    DIMENSION_MISMATCH = 2
    MEMORY_ALLOCATION = 3
    COMPUTATION_FAILED = 4

class Metadata(ctypes.Structure):
    pass  # Forward declaration

Metadata._fields_ = [
    ("key", ctypes.c_char_p),
    ("value", ctypes.c_char_p),
    ("total_size", ctypes.c_uint32),
    ("next", ctypes.POINTER(Metadata))
]

class Embedding(ctypes.Structure):
    _fields_ = [
        ("data", ctypes.c_void_p),
        ("dimensions", ctypes.c_size_t),
        ("count", ctypes.c_size_t),
        ("is_normalized", ctypes.c_bool),
        ("dtype", ctypes.c_int)  # eb_dtype_t
    ]

class StoredVector(ctypes.Structure):
    _fields_ = [
        ("id", ctypes.c_uint64),
        ("embedding", ctypes.POINTER(Embedding)),
        ("metadata", ctypes.POINTER(Metadata)),
        ("model_version", ctypes.c_char_p),
        ("timestamp", ctypes.c_uint64),
        ("parent_id", ctypes.c_uint64),
        ("next", ctypes.POINTER("StoredVector"))
    ]

class ComparisonMethod(Enum):
    COSINE = 0
    PROJECTION = 1
    SEMANTIC = 2

class ComparisonResult(ctypes.Structure):
    _fields_ = [
        ("cosine_similarity", ctypes.c_float),
        ("euclidean_distance", ctypes.c_float),
        ("neighborhood_scores", ctypes.POINTER(ctypes.c_float)),
        ("neighborhood_count", ctypes.c_size_t),
        ("semantic_preservation", ctypes.c_float),
        ("method_used", ctypes.c_int)
    ]

class EmbeddingBridge:
    def __init__(self, lib_path: Optional[str] = None):
        if lib_path is None:
            # Try to find the library in common locations
            lib_name = "libembedding_bridge.so"
            workspace_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
            possible_paths = [
                os.path.join(workspace_root, "lib", lib_name),
                os.path.join(workspace_root, "build", lib_name),
                os.path.join("/usr", "local", "lib", lib_name),
                os.path.join("/usr", "lib", lib_name),
            ]
            for path in possible_paths:
                if os.path.exists(path):
                    lib_path = path
                    break
            if lib_path is None:
                raise RuntimeError(f"Could not find {lib_name}")
        
        self.lib = ctypes.CDLL(lib_path)
        
        # Store structure classes as attributes
        self.Metadata = Metadata
        self.Embedding = Embedding
        self.StoredVector = StoredVector
        self.ComparisonResult = ComparisonResult
        
        self._setup_bindings()
    
    def _setup_bindings(self):
        """Configure C function signatures"""
        # eb_create_embedding
        self.lib.eb_create_embedding.argtypes = [
            ctypes.c_void_p,
            ctypes.c_size_t,
            ctypes.c_size_t,
            ctypes.c_int,
            ctypes.c_bool,
            ctypes.POINTER(ctypes.c_void_p)
        ]
        self.lib.eb_create_embedding.restype = ctypes.c_int

        # eb_destroy_embedding
        self.lib.eb_destroy_embedding.argtypes = [ctypes.c_void_p]
        self.lib.eb_destroy_embedding.restype = None

        # eb_compute_cosine_similarity
        self.lib.eb_compute_cosine_similarity.argtypes = [
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_float)
        ]
        self.lib.eb_compute_cosine_similarity.restype = ctypes.c_int

        # eb_compute_euclidean_distance
        self.lib.eb_compute_euclidean_distance.argtypes = [
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_float)
        ]
        self.lib.eb_compute_euclidean_distance.restype = ctypes.c_int

        # Add neighborhood preservation binding
        self.lib.eb_compute_neighborhood_preservation.argtypes = [
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_float)
        ]
        self.lib.eb_compute_neighborhood_preservation.restype = ctypes.c_int

        # Add new function signatures
        self.lib.eb_compare_embeddings_cross_model.argtypes = [
            ctypes.POINTER(Embedding),
            ctypes.POINTER(Embedding),
            ctypes.c_char_p,
            ctypes.c_char_p,
            ctypes.c_int,
            ctypes.POINTER(ctypes.POINTER(ComparisonResult))
        ]
        self.lib.eb_compare_embeddings_cross_model.restype = ctypes.c_int
    
    def _numpy_to_embedding(self, array: np.ndarray, normalize: bool = False) -> ctypes.c_void_p:
        """Convert numpy array to C embedding structure"""
        print(f"DEBUG: Input array shape: {array.shape}")
        print(f"DEBUG: Input array dtype: {array.dtype}")
        print(f"DEBUG: Input array flags: {array.flags}")
        
        # Ensure array is 2D and C-contiguous float32
        if array.ndim == 1:
            array = array.reshape(1, -1)
        elif array.ndim > 2:
            raise ValueError(f"Array must be 1D or 2D, got {array.ndim}D")
        
        array = np.ascontiguousarray(array, dtype=np.float32)
        print("DEBUG: Converted to float32 and C-contiguous")
        
        # Normalize if requested
        if normalize:
            # Normalize each vector to unit length
            norms = np.linalg.norm(array, axis=1, keepdims=True)
            norms[norms == 0] = 1  # Avoid division by zero
            array = array / norms
            print(f"DEBUG: Normalized array. New max norm: {np.max(np.linalg.norm(array, axis=1))}")
        
        print(f"DEBUG: Final array shape: {array.shape}")
        print(f"DEBUG: Final array dtype: {array.dtype}")
        print(f"DEBUG: Final array flags: {array.flags}")
        print(f"DEBUG: Final array min: {np.min(array)}")
        print(f"DEBUG: Final array max: {np.max(array)}")
        print(f"DEBUG: Final array mean: {np.mean(array)}")
        print(f"DEBUG: Final array std: {np.std(array)}")
        
        embedding_ptr = ctypes.c_void_p()
        status = self.lib.eb_create_embedding(
            array.ctypes.data_as(ctypes.c_void_p),  # Ensure correct pointer type
            array.shape[1],  # dimensions
            array.shape[0],  # count
            DataType.FLOAT32,
            normalize,
            ctypes.byref(embedding_ptr)
        )
        
        if status != Status.SUCCESS:
            raise RuntimeError(f"Failed to create embedding: {Status(status).name}")
        
        return embedding_ptr
    
    def compare_embeddings(
        self,
        embeddings_a: np.ndarray,
        embeddings_b: np.ndarray,
        k_neighbors: int = 5
    ) -> ComparisonResult:
        """Compare two sets of embeddings using the C library"""
        if embeddings_a.shape != embeddings_b.shape:
            raise ValueError("Embedding arrays must have the same shape")
        
        # Convert numpy arrays to C embeddings
        embedding_a_ptr = self._numpy_to_embedding(embeddings_a, normalize=True)
        embedding_b_ptr = self._numpy_to_embedding(embeddings_b, normalize=True)
        
        try:
            # Compute cosine similarity
            cosine_sim = ctypes.c_float()
            status = self.lib.eb_compute_cosine_similarity(
                embedding_a_ptr,
                embedding_b_ptr,
                ctypes.byref(cosine_sim)
            )
            if status != Status.SUCCESS:
                raise RuntimeError(f"Cosine similarity computation failed: {Status(status).name}")
            
            # Compute euclidean distance
            euclidean_dist = ctypes.c_float()
            status = self.lib.eb_compute_euclidean_distance(
                embedding_a_ptr,
                embedding_b_ptr,
                ctypes.byref(euclidean_dist)
            )
            if status != Status.SUCCESS:
                raise RuntimeError(f"Euclidean distance computation failed: {Status(status).name}")
            
            # Compute neighborhood preservation
            neighborhood_scores = np.zeros(k_neighbors, dtype=np.float32)
            preservation_score = ctypes.c_float()
            status = self.lib.eb_compute_neighborhood_preservation(
                embedding_a_ptr,
                embedding_b_ptr,
                k_neighbors,
                ctypes.byref(preservation_score)
            )
            if status != Status.SUCCESS:
                raise RuntimeError(f"Neighborhood preservation computation failed: {Status(status).name}")
            
            # For now, use neighborhood preservation as semantic preservation
            semantic_preservation = preservation_score.value
            
            return ComparisonResult(
                cosine_similarity=cosine_sim.value,
                euclidean_distance=euclidean_dist.value,
                neighborhood_scores=neighborhood_scores,
                semantic_preservation=semantic_preservation
            )
        
        finally:
            # Clean up C embeddings
            self.lib.eb_destroy_embedding(embedding_a_ptr)
            self.lib.eb_destroy_embedding(embedding_b_ptr)
    
    def compare_embeddings_cross_model(self, embedding_a, embedding_b, 
                                     model_version_a, model_version_b,
                                     method=ComparisonMethod.PROJECTION):
        """Compare embeddings from different models.
        
        Args:
            embedding_a: First embedding array
            embedding_b: Second embedding array
            model_version_a: Model version for first embedding
            model_version_b: Model version for second embedding
            method: Comparison method to use
            
        Returns:
            dict: Comparison results including similarity scores
        """
        result_ptr = ctypes.POINTER(ComparisonResult)()
        
        status = self.lib.eb_compare_embeddings_cross_model(
            embedding_a,
            embedding_b,
            model_version_a.encode(),
            model_version_b.encode(),
            method.value,
            ctypes.byref(result_ptr)
        )
        
        if status != Status.SUCCESS:
            raise RuntimeError(f"Failed to compare embeddings: {Status(status).name}")
            
        result = {
            "cosine_similarity": result_ptr.contents.cosine_similarity,
            "euclidean_distance": result_ptr.contents.euclidean_distance,
            "semantic_preservation": result_ptr.contents.semantic_preservation,
            "method_used": ComparisonMethod(result_ptr.contents.method_used).name
        }
        
        # Clean up
        self.lib.eb_destroy_comparison_result(result_ptr)
        
        return result
    
    def __del__(self):
        # Ensure we clean up any remaining resources
        if hasattr(self, 'lib'):
            del self.lib 