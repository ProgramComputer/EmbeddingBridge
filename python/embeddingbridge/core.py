"""
EmbeddingBridge - Core C Interface
Copyright (C) 2024 ProgramComputer

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
"""

import os
import ctypes
import json
import numpy as np
from pathlib import Path
import platform
from typing import List, Dict, Any, Optional, Tuple, Union
import time
import tempfile

# Locate the shared library
def _find_lib():
    """Find the appropriate shared library for the current platform"""
    # Get the directory where this file is located
    current_dir = os.path.dirname(os.path.abspath(__file__))
    
    # Search paths for the library
    search_paths = [
        os.path.join(current_dir, "lib"),  # Bundled with package
        os.path.join(os.path.dirname(current_dir), "lib"),  # Development layout
        os.path.join(os.path.dirname(os.path.dirname(current_dir)), "lib"),  # Project root
        "/usr/local/lib",  # System install
        "/usr/lib",  # System install
    ]
    
    # Determine library name based on platform
    system = platform.system()
    if system == "Linux":
        lib_names = ["libembedding_bridge.so", "libembeddingbridge.so"]
    elif system == "Darwin":
        lib_names = ["libembedding_bridge.dylib", "libembeddingbridge.dylib"]
    elif system == "Windows":
        lib_names = ["embedding_bridge.dll", "embeddingbridge.dll"]
    else:
        raise ImportError(f"Unsupported platform: {system}")
    
    # Search for the library
    for path in search_paths:
        for name in lib_names:
            lib_path = os.path.join(path, name)
            if os.path.exists(lib_path):
                return lib_path
    
    raise ImportError("Could not find EmbeddingBridge library")

# Load the shared library
try:
    _lib = ctypes.CDLL(_find_lib())
except ImportError as e:
    raise ImportError(f"Failed to load EmbeddingBridge library: {e}")

# Define C function signatures based on the actual library
_lib.eb_store_init.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
_lib.eb_store_init.restype = ctypes.c_int

_lib.eb_store_destroy.argtypes = [ctypes.c_void_p]
_lib.eb_store_destroy.restype = ctypes.c_int

_lib.eb_create_embedding.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_int, ctypes.POINTER(ctypes.c_void_p)]
_lib.eb_create_embedding.restype = ctypes.c_int

_lib.eb_create_embedding_from_file.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
_lib.eb_create_embedding_from_file.restype = ctypes.c_int

_lib.eb_get_vector.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_void_p)]
_lib.eb_get_vector.restype = ctypes.c_int

_lib.eb_search_embeddings.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_void_p)]
_lib.eb_search_embeddings.restype = ctypes.c_int

# Define cmd_ function signatures correctly
_lib.cmd_rollback.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
_lib.cmd_rollback.restype = ctypes.c_int

_lib.cmd_set.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
_lib.cmd_set.restype = ctypes.c_int

_lib.cmd_init.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
_lib.cmd_init.restype = ctypes.c_int

_lib.cmd_model.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
_lib.cmd_model.restype = ctypes.c_int

_lib.cmd_store.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
_lib.cmd_store.restype = ctypes.c_int

_lib.cmd_status.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
_lib.cmd_status.restype = ctypes.c_int

_lib.cmd_log.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
_lib.cmd_log.restype = ctypes.c_int

_lib.cmd_diff.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
_lib.cmd_diff.restype = ctypes.c_int

_lib.cmd_rm.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
_lib.cmd_rm.restype = ctypes.c_int

_lib.cmd_switch.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
_lib.cmd_switch.restype = ctypes.c_int

class EmbeddingStore:
    """Python wrapper for EmbeddingBridge vector store"""
    
    def __init__(self, path: str, dimension: Optional[int] = None):
        """Initialize a new embedding store.
        
        Args:
            path: Path to the store
            dimension: Vector dimension (for documentation only, actual dimension is determined by the C library)
        """
        self.path = path
        self._dimension = dimension
        
        # Initialize store
        store_ptr = ctypes.c_void_p()
        config_ptr = ctypes.c_void_p()  # This should be properly initialized in a full implementation
        
        result = _lib.eb_store_init(config_ptr, ctypes.byref(store_ptr))
        if result != 0:
            raise RuntimeError(f"Failed to initialize embedding store at {path}, error code: {result}")
        
        self._store = store_ptr
    
    def __del__(self):
        """Close the store when the object is garbage collected"""
        self.close()
    
    def close(self):
        """Explicitly close the store"""
        if hasattr(self, '_store') and self._store:
            result = _lib.eb_store_destroy(self._store)
            self._store = None
            return result == 0
        return False
    
    def add_embedding_from_file(self, file_path: str) -> str:
        """Add an embedding from a file
        
        Args:
            file_path: Path to the file containing the embedding
            
        Returns:
            ID of the added embedding
        """
        file_path_bytes = file_path.encode('utf-8')
        embedding_ptr = ctypes.c_void_p()
        
        result = _lib.eb_create_embedding_from_file(file_path_bytes, ctypes.byref(embedding_ptr))
        if result != 0:
            raise RuntimeError(f"Failed to create embedding from file: {file_path}, error code: {result}")
        
        # TODO: Actually store the embedding in the store
        # This would involve calling whatever function actually adds to the store
        
        # For now, we'll just return a placeholder ID
        return "embedding_1"
    
    def add_vector(self, vector: List[float]) -> str:
        """Add a vector to the store
        
        Args:
            vector: List or array of float values
            
        Returns:
            ID of the added vector
        """
        # Convert vector to ctypes array
        vector_array = np.array(vector, dtype=np.float32)
        arr_type = ctypes.c_float * len(vector_array)
        vector_arr = arr_type(*vector_array)
        
        # Create embedding
        embedding_ptr = ctypes.c_void_p()
        result = _lib.eb_create_embedding(vector_arr, len(vector_array), ctypes.byref(embedding_ptr))
        if result != 0:
            raise RuntimeError(f"Failed to create embedding, error code: {result}")
        
        # TODO: Actually store the embedding in the store
        # This would involve calling whatever function actually adds to the store
        
        # For now, we'll just return a placeholder ID
        return "vector_1"
    
    def get_vector(self, vector_id: Union[str, int]) -> Optional[Dict]:
        """Get a vector by ID
        
        Args:
            vector_id: Vector ID
            
        Returns:
            Dictionary with vector data or None if not found
        """
        # Convert ID to integer if it's a string
        if isinstance(vector_id, str):
            try:
                id_num = int(vector_id)
            except ValueError:
                # If ID is not convertible to int, it's not a valid ID
                return None
        else:
            id_num = vector_id
        
        # Create output pointers
        embedding_ptr = ctypes.c_void_p()
        metadata_ptr = ctypes.c_void_p()
        
        # Get vector
        result = _lib.eb_get_vector(self._store, id_num, ctypes.byref(embedding_ptr), ctypes.byref(metadata_ptr))
        
        if result != 0:
            return None
        
        # TODO: Properly extract embedding and metadata from the pointers
        # This would involve handling the actual C structs
        
        # For now, we'll just return a placeholder
        return {
            "id": str(id_num),
            "vector": [0.1, 0.2, 0.3],  # Placeholder
            "metadata": {"source": "placeholder"}  # Placeholder
        }
    
    def search(self, query_vector: List[float], top_k: int = 10) -> List[Dict]:
        """Search for similar vectors
        
        Args:
            query_vector: Query vector
            top_k: Number of results to return
            
        Returns:
            List of dictionaries with id, score, and metadata
        """
        # Convert vector to ctypes array
        vector_array = np.array(query_vector, dtype=np.float32)
        arr_type = ctypes.c_float * len(vector_array)
        vector_arr = arr_type(*vector_array)
        
        # Create embedding from query vector
        query_embedding_ptr = ctypes.c_void_p()
        result = _lib.eb_create_embedding(vector_arr, len(vector_array), ctypes.byref(query_embedding_ptr))
        if result != 0:
            raise RuntimeError(f"Failed to create query embedding, error code: {result}")
        
        # Search for similar embeddings
        results_ptr = ctypes.c_void_p()
        result = _lib.eb_search_embeddings(self._store, query_embedding_ptr, top_k, ctypes.byref(results_ptr))
        if result != 0:
            raise RuntimeError(f"Search failed, error code: {result}")
        
        # TODO: Properly extract results from the results_ptr
        # This would involve handling the actual C structs
        
        # For now, we'll just return placeholder results
        return [
            {"id": "result1", "score": 0.95, "metadata": {"source": "placeholder"}},
            {"id": "result2", "score": 0.85, "metadata": {"source": "placeholder"}},
        ]
    
    @property
    def dimension(self) -> int:
        """Get the dimension of vectors in the store"""
        return self._dimension or 384  # Return provided dimension or default
    
    def __enter__(self):
        """Context manager support"""
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Close the store when exiting a context manager block"""
        self.close()
        return False  # Don't suppress exceptions

class CommandResult:
    """Result of a command executed by EmbeddingBridge"""
    
    def __init__(self, returncode, stdout, stderr=""):
        """Initialize CommandResult.
            returncode: Return code of the command (0 for success)
            stdout: Standard output from the command
            stderr: Standard error from the command
        """
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr
    
    def __bool__(self):
        """Return True if the command succeeded (returncode == 0)"""
        return self.returncode == 0
    
    def __str__(self):
        """Return stdout as string representation"""
        return self.stdout

class EmbeddingBridge:
    """Python wrapper for the EmbeddingBridge CLI tool using ctypes"""
    
    def __init__(self, working_dir=None):
        """Initialize EmbeddingBridge.
        
        Args:
            working_dir: Working directory for commands
        """
        self.working_dir = working_dir
        if working_dir:
            os.chdir(working_dir)
    
    def _capture_stdout(self, func, argc, argv):
        """Helper method to capture stdout from C functions and return it as CommandResult"""
        # Use temporary files for stdout and stderr redirection
        with tempfile.NamedTemporaryFile(mode='w+b', delete=False) as stdout_file:
            stdout_path = stdout_file.name
        
        with tempfile.NamedTemporaryFile(mode='w+b', delete=False) as stderr_file:
            stderr_path = stderr_file.name
            
        # Save original file descriptors
        original_stdout = os.dup(1)
        original_stderr = os.dup(2)
        
        # Open the temporary files
        stdout_fd = os.open(stdout_path, os.O_WRONLY)
        stderr_fd = os.open(stderr_path, os.O_WRONLY)
        
        # Redirect stdout and stderr
        os.dup2(stdout_fd, 1)
        os.dup2(stderr_fd, 2)
        
        try:
            # Call the C function
            returncode = func(argc, argv)
            
            # Close the redirected files
            os.close(stdout_fd)
            os.close(stderr_fd)
            
            # Restore original stdout and stderr
            os.dup2(original_stdout, 1)
            os.dup2(original_stderr, 2)
            
            # Read the captured output
            with open(stdout_path, 'rb') as f:
                stdout_data = f.read().decode('utf-8', errors='replace')
            
            with open(stderr_path, 'rb') as f:
                stderr_data = f.read().decode('utf-8', errors='replace')
            
            return CommandResult(returncode, stdout_data, stderr_data)
            
        finally:
            # Ensure original descriptors are restored
            os.dup2(original_stdout, 1)
            os.dup2(original_stderr, 2)
            os.close(original_stdout)
            os.close(original_stderr)
            
            # Clean up temporary files
            try:
                os.unlink(stdout_path)
                os.unlink(stderr_path)
            except:
                pass
    
    def init(self):
        """Initialize a new embedding repository."""
        args = [b"init"]
        
        # Convert to C array
        argc = len(args)
        argv_type = ctypes.c_char_p * argc
        argv = argv_type(*args)
        
        return self._capture_stdout(_lib.cmd_init, argc, argv)
    
    def model_register(self, name, dimensions, normalize=False, description=None):
        """Register a model configuration."""
        args = [b"model", b"register", name.encode('utf-8')]
        
        # Add dimensions
        args.append(str(dimensions).encode('utf-8'))
        
        # Add normalize if true
        if normalize:
            args.append(b"--normalize")
            
        # Add description if provided
        if description:
            args.append(b"--description")
            args.append(description.encode('utf-8'))
        
        # Convert to C array
        argc = len(args)
        argv_type = ctypes.c_char_p * argc
        argv = argv_type(*args)
        
        return self._capture_stdout(_lib.cmd_model, argc, argv)
    
    def model_list(self):
        """List all registered models."""
        args = [b"model", b"list"]
        
        # Convert to C array
        argc = len(args)
        argv_type = ctypes.c_char_p * argc
        argv = argv_type(*args)
        
        return self._capture_stdout(_lib.cmd_model, argc, argv)
    
    def store(self, embedding_file, source_file):
        """Store an embedding for a source file."""
        args = [b"store"]
        
        # Add embedding file
        args.append(embedding_file.encode('utf-8') if isinstance(embedding_file, str) else embedding_file)
        
        # Add source file
        args.append(source_file.encode('utf-8') if isinstance(source_file, str) else source_file)
        
        # Convert to C array
        argc = len(args)
        argv_type = ctypes.c_char_p * argc
        argv = argv_type(*args)
        
        return self._capture_stdout(_lib.cmd_store, argc, argv)
    
    def status(self, source_file=None, verbose=False):
        """Check status of tracked embeddings."""
        args = [b"status"]
        
        # Add source file if provided
        if source_file:
            args.append(source_file.encode('utf-8') if isinstance(source_file, str) else source_file)
        
        # Add verbose flag if true
        if verbose:
            args.append(b"--verbose")
        
        # Convert to C array
        argc = len(args)
        argv_type = ctypes.c_char_p * argc
        argv = argv_type(*args)
        
        return self._capture_stdout(_lib.cmd_status, argc, argv)
    
    def log(self, source_file):
        """Show history of embeddings for a source file."""
        args = [b"log"]
        
        # Add source file
        args.append(source_file.encode('utf-8') if isinstance(source_file, str) else source_file)
        
        # Convert to C array
        argc = len(args)
        argv_type = ctypes.c_char_p * argc
        argv = argv_type(*args)
        
        return self._capture_stdout(_lib.cmd_log, argc, argv)
    
    def diff(self, hash1, hash2):
        """Compare two embedding versions."""
        args = [b"diff"]
        
        # Add hashes
        args.append(hash1.encode('utf-8') if isinstance(hash1, str) else hash1)
        args.append(hash2.encode('utf-8') if isinstance(hash2, str) else hash2)
        
        # Convert to C array
        argc = len(args)
        argv_type = ctypes.c_char_p * argc
        argv = argv_type(*args)
        
        return self._capture_stdout(_lib.cmd_diff, argc, argv)
    
    def rm(self, source_file, model=None, cached=False):
        """Remove an embedding or untrack a file."""
        args = [b"rm"]
        
        # Add source file
        args.append(source_file.encode('utf-8') if isinstance(source_file, str) else source_file)
        
        # Add model if provided
        if model:
            args.append(b"--model")
            args.append(model.encode('utf-8') if isinstance(model, str) else model)
        
        # Add cached flag if true
        if cached:
            args.append(b"--cached")
        
        # Convert to C array
        argc = len(args)
        argv_type = ctypes.c_char_p * argc
        argv = argv_type(*args)
        
        return self._capture_stdout(_lib.cmd_rm, argc, argv)
    
    def rollback(self, hash_value, source_file, model=None):
        """Revert to a previous embedding version."""
        # Format arguments based on the expected format: eb rollback [options] <hash> <source>
        if model:
            args = [b"rollback", b"--model", model.encode('utf-8') if isinstance(model, str) else model, 
                   hash_value.encode('utf-8') if isinstance(hash_value, str) else hash_value,
                   source_file.encode('utf-8') if isinstance(source_file, str) else source_file]
        else:
            args = [b"rollback", 
                   hash_value.encode('utf-8') if isinstance(hash_value, str) else hash_value,
                   source_file.encode('utf-8') if isinstance(source_file, str) else source_file]
        
        # Convert to C array
        argc = len(args)
        argv_type = ctypes.c_char_p * argc
        argv = argv_type(*args)
        
        return self._capture_stdout(_lib.cmd_rollback, argc, argv)
    
    def set_create(self, name, description=None, base_set=None):
        """Create a new embedding set."""
        # Format arguments based on the expected format: eb set [options] <set-name>
        args = [b"set"]
        
        # Add options
        if description:
            args.append(b"--description")
            args.append(description.encode('utf-8') if isinstance(description, str) else description)
        
        if base_set:
            args.append(b"--base")
            args.append(base_set.encode('utf-8') if isinstance(base_set, str) else base_set)
        
        # Add set name
        args.append(name.encode('utf-8') if isinstance(name, str) else name)
        
        # Convert to C array
        argc = len(args)
        argv_type = ctypes.c_char_p * argc
        argv = argv_type(*args)
        
        return self._capture_stdout(_lib.cmd_set, argc, argv)
    
    def set_list(self, verbose=False):
        """List all embedding sets."""
        # Format arguments based on the expected format: eb set [options]
        args = [b"set"]
        
        # Add verbose flag if true
        if verbose:
            args.append(b"--verbose")
        
        # Convert to C array
        argc = len(args)
        argv_type = ctypes.c_char_p * argc
        argv = argv_type(*args)
        
        return self._capture_stdout(_lib.cmd_set, argc, argv)
    
    def set_switch(self, name):
        """Switch to a different embedding set."""
        # Format arguments based on the expected format: eb switch <set-name>
        args = [b"switch", name.encode('utf-8') if isinstance(name, str) else name]
        
        # Convert to C array
        argc = len(args)
        argv_type = ctypes.c_char_p * argc
        argv = argv_type(*args)
        
        return self._capture_stdout(_lib.cmd_switch, argc, argv)
    
    def set_status(self):
        """Show status of the current embedding set."""
        # Format arguments based on set command with specific status flag
        args = [b"set", b"--status"]
        
        # Convert to C array
        argc = len(args)
        argv_type = ctypes.c_char_p * argc
        argv = argv_type(*args)
        
        return self._capture_stdout(_lib.cmd_set, argc, argv) 