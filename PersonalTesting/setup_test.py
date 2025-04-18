"""
EmbeddingBridge - Test Setup Script
Copyright (C) 2024 ProgramComputer

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
"""

import os
import numpy as np
import requests
import lorem
import time
import shutil
import struct
from dotenv import load_dotenv

# Load environment variables from .env file
load_dotenv()

# API Configuration
VOYAGE_API_KEY = os.getenv('VOYAGE_API_KEY')
OPENAI_API_KEY = os.getenv('OPENAI_API_KEY')
OPENAI_ORG_ID = os.getenv('OPENAI_ORG_ID')

# Verify API keys are present
if not all([VOYAGE_API_KEY, OPENAI_API_KEY, OPENAI_ORG_ID]):
    raise EnvironmentError(
        "Missing required API keys. Please ensure VOYAGE_API_KEY, OPENAI_API_KEY, "
        "and OPENAI_ORG_ID are set in .env"
    )

# Cache to store embeddings and avoid duplicate API calls
embedding_cache = {}

def get_voyage_embedding(text, cache_key=None):
    """Get embedding from VoyageAI API with caching"""
    if cache_key and cache_key in embedding_cache:
        print(f"Using cached embedding for {cache_key} (Voyage)")
        return embedding_cache[cache_key]
    
    headers = {
        "Authorization": f"Bearer {VOYAGE_API_KEY}",
        "Content-Type": "application/json"
    }
    data = {
        "model": "voyage-2",
        "input": text,
        "input_type": "document"
    }
    
    print(f"Calling Voyage API for text: {text[:50]}...")
    response = requests.post(
        "https://api.voyageai.com/v1/embeddings",
        headers=headers,
        json=data
    )
    response.raise_for_status()
    embedding_data = response.json()['data'][0]['embedding']
    embedding = np.array(embedding_data, dtype=np.float32)
    
    if cache_key:
        embedding_cache[cache_key] = embedding
    
    return embedding

def get_openai_embedding(text, cache_key=None):
    """Get embedding from OpenAI API with caching"""
    if cache_key and cache_key in embedding_cache:
        print(f"Using cached embedding for {cache_key} (OpenAI)")
        return embedding_cache[cache_key]
    
    headers = {
        "Authorization": f"Bearer {OPENAI_API_KEY}",
        "OpenAI-Organization": OPENAI_ORG_ID,
        "Content-Type": "application/json"
    }
    data = {
        "input": text,
        "model": "text-embedding-3-small",
        "encoding_format": "float"
    }
    
    print(f"Calling OpenAI API for text: {text[:50]}...")
    response = requests.post(
        "https://api.openai.com/v1/embeddings",
        headers=headers,
        json=data
    )
    response.raise_for_status()
    embedding = np.array(response.json()['data'][0]['embedding'], dtype=np.float32)
    
    if cache_key:
        embedding_cache[cache_key] = embedding
        
    return embedding

def file_exists_and_not_empty(filepath):
    """Check if a file exists and is not empty"""
    return os.path.exists(filepath) and os.path.getsize(filepath) > 0

def clean_test_files():
    """Clean the test_files directory by removing it if it exists and recreating it"""
    test_dir = "test_files"
    if os.path.exists(test_dir):
        print(f"Cleaning {test_dir} directory...")
        shutil.rmtree(test_dir)
    os.makedirs(test_dir, exist_ok=True)
    print(f"Created clean {test_dir} directory")

def save_as_bin(embedding, filepath):
    """Save embedding as a binary file with a simple format: float32 values"""
    # Make sure embedding is float32
    embedding = embedding.astype(np.float32)
    
    # Convert embedding to binary and save
    with open(filepath, 'wb') as f:
        # Write the number of dimensions as a 32-bit integer
        f.write(struct.pack('i', embedding.shape[0]))
        # Write the embedding values as 32-bit floats
        for value in embedding:
            f.write(struct.pack('f', value))
    
    print(f"Created binary file: {filepath}")

def create_test_files():
    """Create test files and their embeddings using real APIs"""
    # Create test directory if it doesn't exist
    os.makedirs("test_files", exist_ok=True)
    
    # Create technical documentation with lorem text
    tech_doc_path = "test_files/technical_doc.txt"
    if not file_exists_and_not_empty(tech_doc_path):
        tech_doc = "\n\n".join([
            "# Technical Documentation\n",
            "## Overview\n" + lorem.paragraph() + "\n" + lorem.paragraph(),  # Two separate paragraphs
            "## Technical Details\n" + "\n\n".join([lorem.paragraph() for _ in range(3)]),
            "## Implementation\n" + lorem.paragraph() + "\n\n" + lorem.paragraph(),
            "## Future Work\n" + lorem.paragraph()
        ])
        
        with open(tech_doc_path, "w") as f:
            f.write(tech_doc)
    else:
        with open(tech_doc_path, "r") as f:
            tech_doc = f.read()
    
    # Create API documentation with lorem text
    api_doc_path = "test_files/api_design.md"
    if not file_exists_and_not_empty(api_doc_path):
        api_doc = "\n\n".join([
            "# API Design Documentation\n",
            "## Overview\n" + lorem.paragraph(),
            "## Authentication\n" + lorem.paragraph() + "\n\n" + lorem.sentence(),
            "## Endpoints\n" + "\n".join([
                f"- {endpoint}: {lorem.sentence()}" for endpoint in [
                    "POST /embeddings",
                    "GET /models",
                    "PUT /configurations",
                    "DELETE /models/{id}"
                ]
            ]),
            "## Error Handling\n" + lorem.paragraph(),
            "## Rate Limiting\n" + lorem.paragraph(),
            "## Best Practices\n" + "\n".join([
                f"- {lorem.sentence()}" for _ in range(4)
            ])
        ])
        
        with open(api_doc_path, "w") as f:
            f.write(api_doc)
    else:
        with open(api_doc_path, "r") as f:
            api_doc = f.read()

    # Create embeddings for each file
    files = {
        "technical_doc.txt": tech_doc,
        "api_design.md": api_doc
    }

    for filename, content in files.items():
        print(f"\nProcessing {filename}...")
        
        # Generate OpenAI embedding for the full document (only if not exists)
        openai_path = f"test_files/{filename}.openai.npy"
        if not file_exists_and_not_empty(openai_path):
            print("Getting OpenAI embedding for full document...")
            openai_emb = get_openai_embedding(content, cache_key=f"openai_{filename}")
            np.save(openai_path, openai_emb)
            print(f"Created {openai_path}")
            
            # Also save as binary
            openai_bin_path = f"test_files/{filename}.openai.bin"
            save_as_bin(openai_emb, openai_bin_path)
        else:
            print(f"Using existing OpenAI embedding: {openai_path}")
            
            # Create bin file if it doesn't exist
            openai_bin_path = f"test_files/{filename}.openai.bin"
            if not file_exists_and_not_empty(openai_bin_path):
                openai_emb = np.load(openai_path)
                save_as_bin(openai_emb, openai_bin_path)
        
        # Generate real Voyage embedding (only if not exists)
        voyage_path = f"test_files/{filename}.voyage.npy"
        if not file_exists_and_not_empty(voyage_path):
            print("Getting Voyage embedding for full document...")
            voyage_emb = get_voyage_embedding(content, cache_key=f"voyage_{filename}")
            np.save(voyage_path, voyage_emb)
            print(f"Created {voyage_path}")
            
            # Also save as binary
            voyage_bin_path = f"test_files/{filename}.voyage.bin"
            save_as_bin(voyage_emb, voyage_bin_path)
        else:
            print(f"Using existing Voyage embedding: {voyage_path}")
            
            # Create bin file if it doesn't exist
            voyage_bin_path = f"test_files/{filename}.voyage.bin"
            if not file_exists_and_not_empty(voyage_bin_path):
                voyage_emb = np.load(voyage_path)
                save_as_bin(voyage_emb, voyage_bin_path)

def create_modified_files():
    """Create modified versions of test files to test rollback"""
    print("\nCreating modified versions of files...")
    
    # Modify technical documentation
    tech_doc_path = "test_files/technical_doc.txt"
    if os.path.exists(tech_doc_path):
        with open(tech_doc_path, "r") as f:
            tech_doc = f.read()
        
        # Add a new section to simulate changes
        modified_tech_doc = tech_doc + "\n\n## New Section\n" + lorem.paragraph()
        
        with open(tech_doc_path, "w") as f:
            f.write(modified_tech_doc)
        print("Modified technical_doc.txt")
        
        # Save modified version for rollback testing
        modified_openai_path = "test_files/technical_doc.txt.modified.openai.npy"
        if not file_exists_and_not_empty(modified_openai_path):
            print("Getting OpenAI embedding for modified technical doc...")
            try:
                openai_emb = get_openai_embedding(modified_tech_doc, cache_key="openai_tech_modified")
                np.save(modified_openai_path, openai_emb)
                print(f"Created {modified_openai_path}")
                
                # Also save as binary
                modified_openai_bin_path = "test_files/technical_doc.txt.modified.openai.bin"
                save_as_bin(openai_emb, modified_openai_bin_path)
                
                # Create real Voyage embedding
                modified_voyage_path = "test_files/technical_doc.txt.modified.voyage.npy"
                voyage_emb = get_voyage_embedding(modified_tech_doc, cache_key="voyage_tech_modified")
                np.save(modified_voyage_path, voyage_emb)
                print(f"Created {modified_voyage_path}")
                
                # Also save as binary
                modified_voyage_bin_path = "test_files/technical_doc.txt.modified.voyage.bin"
                save_as_bin(voyage_emb, modified_voyage_bin_path)
            except Exception as e:
                print(f"Error generating modified embeddings: {e}")
        else:
            print(f"Using existing modified embeddings for technical doc")
            
            # Create bin files if they don't exist
            openai_emb = np.load(modified_openai_path)
            modified_openai_bin_path = "test_files/technical_doc.txt.modified.openai.bin"
            if not file_exists_and_not_empty(modified_openai_bin_path):
                save_as_bin(openai_emb, modified_openai_bin_path)
            
            modified_voyage_path = "test_files/technical_doc.txt.modified.voyage.npy"
            if file_exists_and_not_empty(modified_voyage_path):
                voyage_emb = np.load(modified_voyage_path)
                modified_voyage_bin_path = "test_files/technical_doc.txt.modified.voyage.bin"
                if not file_exists_and_not_empty(modified_voyage_bin_path):
                    save_as_bin(voyage_emb, modified_voyage_bin_path)
    
    # Modify API documentation
    api_doc_path = "test_files/api_design.md"
    if os.path.exists(api_doc_path):
        with open(api_doc_path, "r") as f:
            api_doc = f.read()
        
        # Add new endpoints to simulate changes
        if "## Best Practices" in api_doc:
            modified_api_doc = api_doc.replace(
                "## Best Practices",
                "## New Endpoints\n" +
                "- PATCH /models/{id}: " + lorem.sentence() + "\n" +
                "- POST /validate: " + lorem.sentence() + "\n\n" +
                "## Best Practices"
            )
            
            with open(api_doc_path, "w") as f:
                f.write(modified_api_doc)
            print("Modified api_design.md")
            
            # Save modified version for rollback testing
            modified_openai_path = "test_files/api_design.md.modified.openai.npy"
            if not file_exists_and_not_empty(modified_openai_path):
                print("Getting OpenAI embedding for modified API doc...")
                try:
                    openai_emb = get_openai_embedding(modified_api_doc, cache_key="openai_api_modified")
                    np.save(modified_openai_path, openai_emb)
                    print(f"Created {modified_openai_path}")
                    
                    # Also save as binary
                    modified_openai_bin_path = "test_files/api_design.md.modified.openai.bin"
                    save_as_bin(openai_emb, modified_openai_bin_path)
                    
                    # Create real Voyage embedding
                    modified_voyage_path = "test_files/api_design.md.modified.voyage.npy"
                    voyage_emb = get_voyage_embedding(modified_api_doc, cache_key="voyage_api_modified")
                    np.save(modified_voyage_path, voyage_emb)
                    print(f"Created {modified_voyage_path}")
                    
                    # Also save as binary
                    modified_voyage_bin_path = "test_files/api_design.md.modified.voyage.bin"
                    save_as_bin(voyage_emb, modified_voyage_bin_path)
                except Exception as e:
                    print(f"Error generating modified embeddings: {e}")
            else:
                print(f"Using existing modified embeddings for API doc")
                
                # Create bin files if they don't exist
                openai_emb = np.load(modified_openai_path)
                modified_openai_bin_path = "test_files/api_design.md.modified.openai.bin"
                if not file_exists_and_not_empty(modified_openai_bin_path):
                    save_as_bin(openai_emb, modified_openai_bin_path)
                
                modified_voyage_path = "test_files/api_design.md.modified.voyage.npy"
                if file_exists_and_not_empty(modified_voyage_path):
                    voyage_emb = np.load(modified_voyage_path)
                    modified_voyage_bin_path = "test_files/api_design.md.modified.voyage.bin"
                    if not file_exists_and_not_empty(modified_voyage_bin_path):
                        save_as_bin(voyage_emb, modified_voyage_bin_path)

def print_test_commands():
    """Print instructions for testing the EmbeddingBridge commands"""
    print("\nTest commands:")
    
    print("\n1. Initialize EmbeddingBridge in your project:")
    print("   embr init")
    print("   embr model register openai-3-small --dimensions 1536 --description \"OpenAI text-embedding-3-small model\"")
    print("   embr model register voyage-2 --dimensions 1024 --description \"Voyage-2 embedding model\"")
    
    print("\n2. Create and set up the test files (this runs your setup script):")
    print("   python PersonalTesting/setup_test.py")
    
    print("\n3. Store the original embeddings for both files:")
    print("   # Store OpenAI embeddings")
    print("   embr store test_files/technical_doc.txt.openai.npy test_files/technical_doc.txt")
    print("   embr store test_files/api_design.md.openai.npy test_files/api_design.md")
    print("   # Store VoyageAI embeddings")
    print("   embr store test_files/technical_doc.txt.voyage.npy test_files/technical_doc.txt")
    print("   embr store test_files/api_design.md.voyage.npy test_files/api_design.md")
    print("   # Store binary format embeddings")
    print("   embr store test_files/technical_doc.txt.openai.bin test_files/technical_doc.txt")
    print("   embr store test_files/api_design.md.openai.bin test_files/api_design.md")
    print("   embr store test_files/technical_doc.txt.voyage.bin test_files/technical_doc.txt")
    print("   embr store test_files/api_design.md.voyage.bin test_files/api_design.md")
    
    print("\n4. Check the status of your files:")
    print("   embr status test_files/technical_doc.txt")
    print("   embr status test_files/api_design.md")
    
    print("\n5. Store the modified embeddings (which were created by setup script):")
    print("   # Store modified OpenAI embeddings")
    print("   embr store test_files/technical_doc.txt.modified.openai.npy test_files/technical_doc.txt")
    print("   embr store test_files/api_design.md.modified.openai.npy test_files/api_design.md")
    print("   # Store modified VoyageAI embeddings")
    print("   embr store test_files/technical_doc.txt.modified.voyage.npy test_files/technical_doc.txt")
    print("   embr store test_files/api_design.md.modified.voyage.npy test_files/api_design.md")
    print("   # Store modified binary format embeddings")
    print("   embr store test_files/technical_doc.txt.modified.openai.bin test_files/technical_doc.txt")
    print("   embr store test_files/api_design.md.modified.openai.bin test_files/api_design.md")
    print("   embr store test_files/technical_doc.txt.modified.voyage.bin test_files/technical_doc.txt")
    print("   embr store test_files/api_design.md.modified.voyage.bin test_files/api_design.md")
    
    print("\n6. View the history of changes:")
    print("   embr log test_files/technical_doc.txt")
    print("   embr log test_files/api_design.md")
    
    print("\n7. Compare different versions using diff:")
    print("   # Compare files directly")
    print("   embr diff <hash> <hash>")
    print("   # Compare using specific model")
    print("   embr diff --model openai-3-small <short hash> <short hash>")
    print("   # Compare using different models for each file")
    print("   embr diff --models openai-3-small,voyage-2 <short hash> <short hash>")
    
    print("\n8. Rollback to previous versions:")
    print("   # Rollback by file path (goes to previous version)")
    print("   embr rollback test_files/technical_doc.txt")
    print("   embr rollback test_files/api_design.md")
    print("   # Check the status after rollback")
    print("   embr status test_files/technical_doc.txt")
    print("   embr status test_files/api_design.md")
    
    print("\n9. Test the remote functionality:")
    print("   # Add a remote repository")
    print("   embr remote add s3-test s3://embeddingbridge-test")
    print("   embr remote list")
    print("   # Push your embeddings to the remote")
    print("   embr remote push s3-test")
    print("   # Pull embeddings from the remote")
    print("   embr remote pull s3-test")
    
    print("\n10. Test the set functionality:")
    print("   # Create a new set")
    print("   embr set create test-set \"Test set for embeddings\"")
    print("   # Check available sets")
    print("   embr set list")
    print("   # Switch to the test set")
    print("   embr set switch test-set")
    print("   # Check status of the set")
    print("   embr set status")

if __name__ == "__main__":
    print("Starting test file and embedding generation...")
    clean_test_files()  # Clean test_files directory
    create_test_files()  # Create original files
    print("\nSetup initial files complete!")
    
    # Wait a moment to ensure timestamp differences
    time.sleep(1)
    
    create_modified_files()  # Create modified versions
    print("\nSetup modified files complete!")
    
    # Print test commands
    print_test_commands()
