#!/usr/bin/env python3
"""
EmbeddingBridge - Dataset Embedding Test
Copyright (C) 2024 ProgramComputer

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This script loads the langchain-docs dataset, generates embeddings for each document
using both OpenAI and Voyage models, and stores them using the embedding bridge.
"""

import os
import sys
import time
import tempfile
import numpy as np
import argparse
from pathlib import Path
from dotenv import load_dotenv
from datasets import load_dataset
import openai
from typing import List, Dict, Any, Optional, Tuple, Union
import requests
from embeddingbridge import EmbeddingBridge

# Load environment variables from .env file
load_dotenv()

class DatasetEmbeddingProcessor:
    """Process the langchain-docs dataset and generate embeddings"""
    
    def __init__(self, 
                 dataset_name: str = "jamescalam/langchain-docs-23-06-27", 
                 output_dir: str = "test_files",
                 batch_size: int = 50,
                 max_samples: Optional[int] = None,
                 test_sample_count: int = 5):
        """Initialize the processor
        
        Args:
            dataset_name: Name of the dataset to load
            output_dir: Directory to store output files
            batch_size: Number of documents to process in each batch
            max_samples: Maximum number of samples to process (None for all)
            test_sample_count: Number of samples to use for testing sequence
        """
        self.dataset_name = dataset_name
        self.output_dir = Path(output_dir)
        self.batch_size = batch_size
        self.max_samples = max_samples
        self.test_sample_count = test_sample_count
        
        # Create output directory if it doesn't exist
        os.makedirs(self.output_dir, exist_ok=True)
        
        # Create the EmbeddingBridge instance
        self.eb = EmbeddingBridge()
        
        # Set up OpenAI API client
        self.client = openai.OpenAI(
            api_key=os.getenv("OPENAI_API_KEY"),
            organization=os.getenv("OPENAI_ORG_ID")
        )
        
        # Check Voyage API key
        self.voyage_api_key = os.getenv("VOYAGE_API_KEY")
        if not self.voyage_api_key:
            raise ValueError("VOYAGE_API_KEY environment variable is not set")
        
        # Initialize the embedding bridge repository if it doesn't exist
        self._init_repository()
        self._register_models()
    
    def _init_repository(self):
        """Initialize the embedding bridge repository"""
        # Check if .eb directory exists
        if not os.path.exists(".eb"):
            print("Initializing embedding bridge repository...")
            init_result = self.eb.init()
            print(f"Init result: {init_result.stdout}")
    
    def _register_models(self):
        """Register embedding models"""
        # Check if models are already registered
        model_list_result = self.eb.model_list()
        model_list = model_list_result.stdout
        
        # Register OpenAI model if not already registered
        if 'openai-3-small' not in model_list:
            print("Registering OpenAI model...")
            openai_register = self.eb.model_register(
                name='openai-3-small',
                dimensions=1536,
                normalize=True,
                description='OpenAI text-embedding-3-small model'
            )
            print(f"OpenAI model register result: {openai_register.stdout}")
        
        # Register Voyage model if not already registered
        if 'voyage-2' not in model_list:
            print("Registering Voyage model...")
            voyage_register = self.eb.model_register(
                name='voyage-2',
                dimensions=1024,
                normalize=True,
                description='Voyage AI embedding model'
            )
            print(f"Voyage model register result: {voyage_register.stdout}")
    
    def get_openai_embedding(self, text: str) -> np.ndarray:
        """Get embeddings from OpenAI API
        
        Args:
            text: Text to embed
            
        Returns:
            Embedding as numpy array
        """
        try:
            response = self.client.embeddings.create(
                model="text-embedding-3-small",
                input=text,
                encoding_format="float"
            )
            embedding = response.data[0].embedding
            return np.array(embedding, dtype=np.float32)
        except Exception as e:
            print(f"Error getting OpenAI embedding: {e}")
            # Return a zero vector as fallback
            return np.zeros(1536, dtype=np.float32)
    
    def get_voyage_embedding(self, text: str) -> np.ndarray:
        """Get embeddings from Voyage AI API
        
        Args:
            text: Text to embed
            
        Returns:
            Embedding as numpy array
        """
        try:
            headers = {
                "Authorization": f"Bearer {self.voyage_api_key}",
                "Content-Type": "application/json"
            }
            data = {
                "model": "voyage-2",
                "input": text,
                "normalize": True
            }
            response = requests.post(
                "https://api.voyageai.com/v1/embeddings",
                headers=headers,
                json=data
            )
            response_json = response.json()
            embedding = response_json["embeddings"][0]
            return np.array(embedding, dtype=np.float32)
        except Exception as e:
            print(f"Error getting Voyage embedding: {e}")
            # Return a zero vector as fallback
            return np.zeros(1024, dtype=np.float32)
    
    def process_dataset(self):
        """Process the dataset and generate embeddings"""
        print(f"Loading dataset {self.dataset_name}...")
        dataset = load_dataset(self.dataset_name)
        print(f"Dataset loaded: {len(dataset['train'])} documents")
        
        # Limit samples if specified
        samples = dataset['train']
        if self.max_samples:
            samples = samples.select(range(min(self.max_samples, len(samples))))
        
        print(f"Processing {len(samples)} documents in batches of {self.batch_size}...")
        
        # Keep track of processed documents for testing
        processed_docs = []
        
        # Process in batches
        for i in range(0, len(samples), self.batch_size):
            batch = samples.select(range(i, min(i + self.batch_size, len(samples))))
            print(f"Processing batch {i // self.batch_size + 1}/{(len(samples) + self.batch_size - 1) // self.batch_size} ({len(batch)} documents)")
            
            for idx, doc in enumerate(batch):
                doc_id = doc['id']
                doc_text = doc['text']
                
                # Truncate very long documents to avoid API limits
                if len(doc_text) > 8000:
                    doc_text = doc_text[:8000]
                
                # Generate a file path for the document
                doc_path = self.output_dir / f"doc_{doc_id}.txt"
                
                # Write the document to a file
                with open(doc_path, 'w', encoding='utf-8') as f:
                    f.write(doc_text)
                
                # Generate embeddings
                print(f"Generating embeddings for document {doc_id} ({len(doc_text)} chars)...")
                
                # Get OpenAI embedding
                openai_embedding = self.get_openai_embedding(doc_text)
                openai_path = self.output_dir / f"doc_{doc_id}.openai.npy"
                np.save(openai_path, openai_embedding)
                
                # Store OpenAI embedding
                openai_store = self.eb.store(str(openai_path), str(doc_path))
                print(f"OpenAI store result: {openai_store.stdout}")
                
                # Get Voyage embedding (with rate limit considerations)
                voyage_embedding = self.get_voyage_embedding(doc_text)
                voyage_path = self.output_dir / f"doc_{doc_id}.voyage.npy"
                np.save(voyage_path, voyage_embedding)
                
                # Store Voyage embedding
                voyage_store = self.eb.store(str(voyage_path), str(doc_path))
                print(f"Voyage store result: {voyage_store.stdout}")
                
                # Add to processed documents list
                processed_docs.append({
                    'id': doc_id,
                    'path': doc_path,
                    'openai_path': openai_path,
                    'voyage_path': voyage_path
                })
                
                print(f"Successfully processed document {doc_id}")
                
                # Small delay to avoid hitting rate limits for Voyage API
                time.sleep(0.05)
            
            # Larger delay between batches for Voyage API rate limiting
            time.sleep(1)
        
        print(f"Successfully processed {len(processed_docs)} documents")
        return processed_docs
    
    def run_test_sequence(self, processed_docs):
        """Run the testing sequence on a subset of processed documents
        
        Args:
            processed_docs: List of processed documents
        """
        if not processed_docs:
            print("No documents were processed, skipping test sequence")
            return
        
        # Select documents for testing
        test_docs = processed_docs[:min(self.test_sample_count, len(processed_docs))]
        print(f"Running test sequence on {len(test_docs)} documents...")
        
        for doc in test_docs:
            print(f"\nTesting document {doc['id']}:")
            
            # Check status
            print("\nChecking status...")
            status_result = self.eb.status(str(doc['path']), verbose=True)
            print(f"Status: {status_result.stdout}")
            
            # Check log
            print("\nChecking log...")
            log_result = self.eb.log(str(doc['path']))
            print(f"Log: {log_result.stdout}")
            
            # Extract hashes for diff
            hashes = []
            for line in log_result.stdout.strip().split('\n'):
                if 'commit' in line:
                    hash_value = line.split()[1]
                    hashes.append(hash_value)
            
            # Run diff if we have multiple hashes
            if len(hashes) >= 2:
                print("\nRunning diff...")
                diff_result = self.eb.diff(hashes[0][:8], hashes[1][:8])
                print(f"Diff: {diff_result.stdout}")
            
            # Test rm with --model
            print("\nTesting rm with --model...")
            rm_result = self.eb.rm(str(doc['path']), model='openai-3-small')
            print(f"Remove model result: {rm_result.stdout}")
            if rm_result.stderr:
                print(f"Remove stderr: {rm_result.stderr}")
            
            # Check status after removal
            print("\nChecking status after removal...")
            status_after_remove = self.eb.status(str(doc['path']), verbose=True)
            print(f"Status after removal: {status_after_remove.stdout}")
            
            # Add back
            print("\nAdding back openai embedding...")
            add_back_result = self.eb.store(str(doc['openai_path']), str(doc['path']))
            print(f"Add back result: {add_back_result.stdout}")
            
            # Test rm with --cached
            print("\nTesting rm with --cached...")
            cached_rm_result = self.eb.rm(str(doc['path']), cached=True)
            print(f"Remove cached result: {cached_rm_result.stdout}")
            if cached_rm_result.stderr:
                print(f"Remove cached stderr: {cached_rm_result.stderr}")
            
            # Re-add
            print("\nRe-adding embeddings...")
            readd_openai = self.eb.store(str(doc['openai_path']), str(doc['path']))
            print(f"Re-add OpenAI result: {readd_openai.stdout}")
            
            readd_voyage = self.eb.store(str(doc['voyage_path']), str(doc['path']))
            print(f"Re-add Voyage result: {readd_voyage.stdout}")
            
            # Test rollback if we have multiple hashes
            new_log_result = self.eb.log(str(doc['path']))
            new_hashes = []
            for line in new_log_result.stdout.strip().split('\n'):
                if 'commit' in line:
                    hash_value = line.split()[1]
                    new_hashes.append(hash_value)
            
            if len(new_hashes) >= 2:
                print("\nTesting rollback...")
                rollback_result = self.eb.rollback(new_hashes[1], str(doc['path']))
                print(f"Rollback result: {rollback_result.stdout}")
                if rollback_result.stderr:
                    print(f"Rollback stderr: {rollback_result.stderr}")
                
                # Check status after rollback
                print("\nChecking status after rollback...")
                rollback_status = self.eb.status(str(doc['path']), verbose=True)
                print(f"Status after rollback: {rollback_status.stdout}")
        
        # Test eb set
        print("\nTesting eb set...")
        set_create_result = self.eb.set_create('test-set')
        print(f"Set create result: {set_create_result.stdout}")
        if set_create_result.stderr:
            print(f"Set create stderr: {set_create_result.stderr}")
        
        set_list_result = self.eb.set_list()
        print(f"Set list result: {set_list_result.stdout}")
        
        set_switch_result = self.eb.set_switch('test-set')
        print(f"Set switch result: {set_switch_result.stdout}")
        if set_switch_result.stderr:
            print(f"Set switch stderr: {set_switch_result.stderr}")
        
        set_status_result = self.eb.set_status()
        print(f"Set status result: {set_status_result.stdout}")
        
        print("\nTest sequence completed successfully!")

def main():
    """Main function"""
    parser = argparse.ArgumentParser(description='Process the langchain-docs dataset and generate embeddings')
    parser.add_argument('--max-samples', type=int, default=20, help='Maximum number of samples to process')
    parser.add_argument('--batch-size', type=int, default=5, help='Number of documents to process in each batch')
    parser.add_argument('--test-samples', type=int, default=3, help='Number of samples to use for testing sequence')
    parser.add_argument('--output-dir', type=str, default='test_files', help='Directory to store output files')
    
    args = parser.parse_args()
    
    processor = DatasetEmbeddingProcessor(
        output_dir=args.output_dir,
        batch_size=args.batch_size,
        max_samples=args.max_samples,
        test_sample_count=args.test_samples
    )
    
    processed_docs = processor.process_dataset()
    processor.run_test_sequence(processed_docs)

if __name__ == "__main__":
    main() 