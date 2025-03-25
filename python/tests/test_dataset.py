#!/usr/bin/env python3
"""
EmbeddingBridge - Dataset Tests
Copyright (C) 2024 ProgramComputer

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
"""

import os
import sys
import unittest
import tempfile
import shutil
import pandas as pd
import numpy as np
from pathlib import Path

# Add parent directory to path
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from embeddingbridge import datasets

class TestDataset(unittest.TestCase):
    """Test suite for Dataset class"""
    
    def setUp(self):
        """Set up test fixtures"""
        # Create a temporary directory
        self.temp_dir = tempfile.mkdtemp()
        
        # Create test data
        self.test_data = pd.DataFrame({
            "id": ["doc1", "doc2", "doc3"],
            "values": [
                np.array([0.1, 0.2, 0.3, 0.4, 0.5], dtype=np.float32),
                np.array([0.2, 0.3, 0.4, 0.5, 0.6], dtype=np.float32),
                np.array([0.3, 0.4, 0.5, 0.6, 0.7], dtype=np.float32),
            ],
            "metadata": [
                {"text": "Document 1", "source": "test"},
                {"text": "Document 2", "source": "test"},
                {"text": "Document 3", "source": "test"},
            ],
        })
        
        # Create a test dataset
        self.dataset = datasets.Dataset(
            documents=self.test_data,
            name="test-dataset",
            metadata={"description": "Test dataset", "dimension": 5}
        )
    
    def tearDown(self):
        """Tear down test fixtures"""
        # Remove temporary directory
        shutil.rmtree(self.temp_dir)
    
    def test_dataset_creation(self):
        """Test dataset creation"""
        self.assertEqual(self.dataset.name, "test-dataset")
        self.assertEqual(len(self.dataset), 3)
        self.assertEqual(self.dataset.dimension, 5)
        self.assertEqual(self.dataset.metadata["description"], "Test dataset")
    
    def test_dataset_save_load(self):
        """Test saving and loading dataset"""
        # Save dataset
        dataset_path = os.path.join(self.temp_dir, "test-dataset")
        self.dataset.save(dataset_path)
        
        # Check that files were created
        self.assertTrue(os.path.exists(dataset_path))
        self.assertTrue(os.path.exists(os.path.join(dataset_path, "test-dataset.parquet")))
        self.assertTrue(os.path.exists(os.path.join(dataset_path, "metadata.json")))
        
        # Load dataset
        loaded_dataset = datasets.Dataset.from_path(dataset_path)
        
        # Check that dataset was loaded correctly
        self.assertEqual(loaded_dataset.name, "test-dataset")
        self.assertEqual(len(loaded_dataset), 3)
        self.assertEqual(loaded_dataset.dimension, 5)
        self.assertEqual(loaded_dataset.metadata["description"], "Test dataset")
    
    def test_dataset_search(self):
        """Test vector search"""
        # Create a query vector
        query_vector = [0.2, 0.3, 0.4, 0.5, 0.6]
        
        # Search for similar vectors
        results = self.dataset.search(query_vector, top_k=2)
        
        # Check results
        self.assertEqual(len(results), 2)
        self.assertEqual(results[0][0], "doc2")  # Most similar should be doc2
        self.assertGreater(results[0][1], 0.95)  # Similarity should be high
    
    def test_iter_documents(self):
        """Test iterating through documents in batches"""
        # Iterate through documents
        batches = list(self.dataset.iter_documents(batch_size=2))
        
        # Check batches
        self.assertEqual(len(batches), 2)
        self.assertEqual(len(batches[0]), 2)  # First batch has 2 documents
        self.assertEqual(len(batches[1]), 1)  # Second batch has 1 document
        
        # Check document content
        self.assertEqual(batches[0][0]["id"], "doc1")
        self.assertEqual(batches[0][1]["id"], "doc2")
        self.assertEqual(batches[1][0]["id"], "doc3")

if __name__ == "__main__":
    unittest.main() 