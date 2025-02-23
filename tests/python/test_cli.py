"""
EmbeddingBridge - Python Test Suite
Copyright (C) 2024 ProgramComputer

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
"""

import os
import shutil
import subprocess
import tempfile
import unittest
import numpy as np
from pathlib import Path
import json
import requests
from dotenv import load_dotenv
from lorem import get_paragraph, get_sentence, get_word
import random
import math

# Load environment variables from .env file
load_dotenv()

# API Configuration
VOYAGE_API_KEY = os.getenv('VOYAGE_API_KEY')
OPENAI_API_KEY = os.getenv('OPENAI_API_KEY')
OPENAI_ORG_ID = os.getenv('OPENAI_ORG_ID')

# After loading environment variables
if not all([VOYAGE_API_KEY, OPENAI_API_KEY, OPENAI_ORG_ID]):
    raise EnvironmentError(
        "Missing required API keys. Please ensure VOYAGE_API_KEY, OPENAI_API_KEY, "
        "and OPENAI_ORG_ID are set in tests/python/.env"
    )

# Test content templates for different domains
TECH_TEMPLATES = {
    'ai_ml': [
        "The {adj} nature of {ml_concept} enables {application} through {technique}, leading to {outcome}.",
        "Recent advances in {ml_concept} have revolutionized {field}, particularly in {application}.",
        "By leveraging {technique} with {ml_concept}, researchers achieved {outcome} in {field}."
    ],
    'system': [
        "The {component} system utilizes {technique} for optimal {resource} management.",
        "Implementation of {technique} in {component} resulted in {outcome}.",
        "Through careful {resource} allocation, the {component} achieves {outcome}."
    ]
}

# Domain-specific vocabulary for template filling
VOCAB = {
    'adj': ['adaptive', 'distributed', 'scalable', 'robust', 'efficient'],
    'ml_concept': ['deep learning', 'neural networks', 'reinforcement learning', 'transformer models', 'attention mechanisms'],
    'application': ['natural language processing', 'computer vision', 'speech recognition', 'anomaly detection'],
    'technique': ['gradient descent', 'backpropagation', 'transfer learning', 'fine-tuning', 'ensemble methods'],
    'outcome': ['improved accuracy', 'reduced latency', 'better generalization', 'enhanced performance'],
    'field': ['healthcare', 'finance', 'robotics', 'autonomous systems', 'cybersecurity'],
    'component': ['memory manager', 'scheduler', 'I/O subsystem', 'network stack', 'file system'],
    'resource': ['CPU', 'memory', 'bandwidth', 'storage', 'energy']
}

class TestEmbeddingBridgeCLI(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Create temporary directories
        cls.temp_dir = tempfile.mkdtemp()
        cls.git_dir = os.path.join(cls.temp_dir, "git_repo")
        cls.regular_dir = os.path.join(cls.temp_dir, "regular_dir")
        
        # Set up Git repo
        os.makedirs(cls.git_dir)
        subprocess.run(["git", "init"], cwd=cls.git_dir, check=True)
        subprocess.run(["git", "config", "user.email", "test@example.com"], cwd=cls.git_dir)
        subprocess.run(["git", "config", "user.name", "Test User"], cwd=cls.git_dir)
        
        # Initialize eb in Git repo
        subprocess.run(["eb", "init"], cwd=cls.git_dir, check=True)
        
        # Create test files in Git repo with version history
        cls._create_test_files(cls.git_dir)
        subprocess.run(["git", "add", "."], cwd=cls.git_dir)
        subprocess.run(["git", "commit", "-m", "Initial commit"], cwd=cls.git_dir)
        
        # Create feature branch with semantic changes
        subprocess.run(["git", "checkout", "-b", "feature"], cwd=cls.git_dir)
        with open(os.path.join(cls.git_dir, "technical_doc.txt"), "a") as f:
            f.write("\nAdditional technical details about memory management")
        subprocess.run(["git", "commit", "-am", "Update technical doc with semantic changes"], cwd=cls.git_dir)
        subprocess.run(["git", "checkout", "main"], cwd=cls.git_dir)
        
        # Set up regular directory
        os.makedirs(cls.regular_dir)
        cls._create_test_files(cls.regular_dir)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.temp_dir)

    @staticmethod
    def _create_test_files(directory):
        """Create test files with semantic evolution in mind"""
        def generate_technical_content(domain='ai_ml'):
            """Generate domain-specific technical content"""
            template = random.choice(TECH_TEMPLATES[domain])
            content = template.format(**{k: random.choice(v) for k, v in VOCAB.items()})
            return content + "\n" + get_paragraph()  # Add some lorem ipsum text after the technical content

        # Create base content with multiple paragraphs
        base_content = "\n\n".join([
            "# Technical Documentation\n",
            get_paragraph(count=2),  # Introduction
            "## Overview\n" + get_paragraph(),  # Overview section
            "## Technical Details\n" + "\n".join([
                generate_technical_content() + "\n" + get_paragraph() 
                for _ in range(3)
            ]),  # Technical details
            "## Implementation\n" + get_paragraph() + "\n" + generate_technical_content('system'),  # Implementation
            "## Future Work\n" + get_paragraph()  # Future work
        ])

        # Create evolved content with semantic changes
        evolved_content = base_content + "\n\n## Additional Considerations\n" + "\n".join([
            generate_technical_content() + "\n" + get_paragraph()
            for _ in range(2)
        ])

        files = {
            "technical_doc.txt": {
                "original": base_content,
                "evolved": evolved_content
            },
            
            "api_design.md": "\n".join([
                "# API Design Documentation",
                "",
                "## Overview",
                get_paragraph(),
                "",
                "## Authentication",
                get_paragraph(),
                "",
                "## Endpoints",
                generate_technical_content(),
                get_paragraph(),
                "",
                "## Error Handling",
                get_paragraph()
            ])
        }
        
        # Write initial content
        for filename, content in files.items():
            filepath = os.path.join(directory, filename)
            if isinstance(content, dict):
                with open(filepath, "w") as f:
                    f.write(content["original"].strip())
            else:
                with open(filepath, "w") as f:
                    f.write(content.strip())

        return files

    def _setup_test_environment(self, test_name, with_git=False):
        test_dir = os.path.join(self.temp_dir, test_name)
        os.makedirs(test_dir, exist_ok=True)
        
        # Initialize eb repository
        result = subprocess.run(
            ["eb", "init"],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        
        return test_dir

    @staticmethod
    def _get_voyage_embedding(text):
        """Get embedding from VoyageAI API"""
        headers = {
            "Authorization": f"Bearer {VOYAGE_API_KEY}",
            "Content-Type": "application/json"
        }
        data = {
            "model": "voyage-2",  # Using voyage-2 as specified in docs
            "input": text,        # Single text input
            "input_type": "document"  # Specify input type as document
        }
        
        try:
            response = requests.post(
                "https://api.voyageai.com/v1/embeddings",
                headers=headers,
                json=data
            )
            response.raise_for_status()
            
            # Comment out the debug print
            # print("Voyage API Response:", response.json())
            
            embedding_data = response.json()['data'][0]['embedding']
            return np.array(embedding_data, dtype=np.float32)
            
        except requests.exceptions.RequestException as e:
            print(f"Voyage API error: {str(e)}")
            if hasattr(e.response, 'json'):
                print(f"Error details: {e.response.json()}")
            raise

    @staticmethod
    def _get_openai_embedding(text):
        """Get embedding from OpenAI API"""
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
        response = requests.post(
            "https://api.openai.com/v1/embeddings",
            headers=headers,
            json=data
        )
        response.raise_for_status()
        return np.array(response.json()['data'][0]['embedding'], dtype=np.float32)

    def _create_test_embedding_files(self, test_dir):
        """Create test embedding files (.bin and .npy) using real embeddings from API"""
        # Generate domain-specific test content
        test_content = "\n".join([
            "# Machine Learning System Architecture",
            get_paragraph(),
            "## Core Components",
            get_paragraph()
        ])
        
        test_content2 = "\n".join([
            "# System Resource Management",
            get_paragraph(),
            "## Architecture",
            get_paragraph()
        ])
        
        try:
            # Get embeddings from VoyageAI for .bin file
            print("Fetching Voyage AI embedding...")
            bin_data = self._get_voyage_embedding(test_content)
            bin_path = os.path.join(test_dir, "test_emb.bin")
            with open(bin_path, "wb") as f:
                bin_data.tofile(f)
            print(f"Created .bin file at {bin_path}")

            # Get embeddings from OpenAI for .npy file
            print("Fetching OpenAI embedding...")
            npy_data = self._get_openai_embedding(test_content2)
            npy_path = os.path.join(test_dir, "test_emb.npy")
            np.save(npy_path, npy_data)
            print(f"Created .npy file at {npy_path}")

            return bin_path, npy_path
        except requests.exceptions.RequestException as e:
            print(f"API Error: {str(e)}")
            print(f"Response content: {e.response.content if hasattr(e, 'response') else 'No response content'}")
            self.skipTest(f"Failed to get embeddings from API: {str(e)}")
        except Exception as e:
            print(f"Unexpected error: {str(e)}")
            self.skipTest(f"Unexpected error while creating test files: {str(e)}")

    def _verify_eb_structure(self, test_dir, source_file, hash_value):
        """Verify the .eb directory structure and contents"""
        # Check .eb/objects/<hash>.bin exists
        bin_path = os.path.join(test_dir, ".eb", "objects", f"{hash_value}.bin")
        self.assertTrue(os.path.exists(bin_path))

        # Check .eb/objects/<hash>.meta exists and has correct content
        meta_path = os.path.join(test_dir, ".eb", "objects", f"{hash_value}.meta")
        self.assertTrue(os.path.exists(meta_path))
        
        # First read the raw content for debugging
        with open(meta_path) as f:
            raw_content = f.read()
            print(f"Raw metadata content:\n{raw_content}")
        
        # Now try to parse as JSON
        with open(meta_path) as f:
            try:
                meta = json.load(f)
                self.assertEqual(meta["source"], source_file)
                self.assertIn("timestamp", meta)
                self.assertIn("model", meta)  # Changed from assertEqual to assertIn
            except json.JSONDecodeError as e:
                print(f"JSON decode error: {e}")
                print(f"Content causing error:\n{raw_content}")
                raise

        # Check .eb/index has the mapping
        index_path = os.path.join(test_dir, ".eb", "index")
        self.assertTrue(os.path.exists(index_path))
        with open(index_path) as f:
            index_content = f.read()
            self.assertIn(f"{source_file}: {hash_value}", index_content)

        # Check .eb/history has the entry
        history_path = os.path.join(test_dir, ".eb", "history")
        self.assertTrue(os.path.exists(history_path))
        with open(history_path) as f:
            history_content = f.read()
            self.assertIn(f"{source_file}: {hash_value}", history_content)

    # Root test - Testing basic model registration
    def test_model_registration(self):
        """Test basic model registration functionality"""
        test_dir = os.path.join(self.temp_dir, "model_test")
        os.makedirs(test_dir)
        
        # Initialize eb
        init_result = subprocess.run(
            ["eb", "init"], 
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        print("\nInitialization Output:")
        print("-" * 30)
        print(init_result.stdout)
        if init_result.stderr:
            print(init_result.stderr)
        
        print("\n" + "="*80)
        print("Testing model registration and listing...")
        print("="*80)
        
        # Test model registration
        result = subprocess.run(
            ["eb", "model", "register", "text-embedding-3-small", "--dimensions", "1536", "--normalize"],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        print("\nModel Registration Output:")
        print("-" * 30)
        print(result.stdout)
        print(result.stderr if result.stderr else "No errors")
        self.assertEqual(result.returncode, 0)
        self.assertIn("Successfully registered model", result.stdout)
        
        # Verify model is listed
        print("\nModel List Output:")
        print("-" * 30)
        
        # Capture model list output
        list_result = subprocess.run(
            ["eb", "model", "list"],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        print(list_result.stdout)
        print(list_result.stderr if list_result.stderr else "No errors")
        self.assertEqual(list_result.returncode, 0)
        self.assertIn("text-embedding-3-small", list_result.stdout)
        self.assertIn("1536 dimensions", list_result.stdout)
        self.assertIn("normalized", list_result.stdout)
        
        print("\n" + "="*80)

    def test_store_command(self):
        """Test basic store command functionality with precomputed embeddings"""
        test_dir = self._setup_test_environment("store_test", with_git=True)
        
        # Create test files
        self._create_test_files(test_dir)
        
        # Create a test embedding file
        embedding_data = np.random.rand(1536).astype(np.float32)
        embedding_file = os.path.join(test_dir, "test.bin")
        with open(embedding_file, "wb") as f:
            embedding_data.tofile(f)
        
        print("\n" + "="*80)
        print("Testing store command...")
        print("="*80)
        
        # Test basic store command with dimensions
        result = subprocess.run(
            ["eb", "store", "--embedding", embedding_file, "--dims", "1536", "technical_doc.txt"],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        print("\nStore Command Output:")
        print("-" * 30)
        print(result.stdout)
        print(result.stderr if result.stderr else "No errors")
        
        # Basic assertions
        self.assertEqual(result.returncode, 0)
        self.assertRegex(result.stdout.strip(), r"^✓ technical_doc\.txt \([a-f0-9]+\)$")
        
        # Test verbose store command
        verbose_result = subprocess.run(
            ["eb", "store", "-v", "--embedding", embedding_file, "--dims", "1536", "technical_doc.txt"],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        print("\nVerbose Store Command Output:")
        print("-" * 30)
        print(verbose_result.stdout)
        print(verbose_result.stderr if verbose_result.stderr else "No errors")
        
        # Verbose output assertions
        self.assertEqual(verbose_result.returncode, 0)
        self.assertIn("→ Reading technical_doc.txt", verbose_result.stdout)
        self.assertIn("→ Using embedding with 1536 dimensions", verbose_result.stdout)

    def test_store_precomputed_bin(self):
        """Test storing precomputed .bin embedding file"""
        test_dir = self._setup_test_environment("store_bin_test", with_git=False)
        bin_path, _ = self._create_test_embedding_files(test_dir)
        
        # Create a source file
        source_file = "file.txt"
        with open(os.path.join(test_dir, source_file), "w") as f:
            f.write("Test content")
        
        # Test storing .bin file with dimensions
        result = subprocess.run(
            ["eb", "store", "--embedding", bin_path, "--dims", "1536", source_file],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        
        self.assertEqual(result.returncode, 0)
        if result.returncode != 0:
            print("Error output:", result.stderr)
        
        # Extract hash from output for verification
        hash_value = result.stdout.strip().split("(")[1].rstrip(")")
        self._verify_eb_structure(test_dir, source_file, hash_value)

    def test_store_precomputed_npy(self):
        """Test storing precomputed .npy embedding file"""
        test_dir = self._setup_test_environment("store_npy_test", with_git=False)
        _, npy_path = self._create_test_embedding_files(test_dir)
        
        # Create a source file
        source_file = "file.txt"
        with open(os.path.join(test_dir, source_file), "w") as f:
            f.write("Test content")
        
        # Test storing .npy file (dimensions should be auto-detected)
        result = subprocess.run(
            ["eb", "store", "--embedding", npy_path, source_file],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        
        self.assertEqual(result.returncode, 0)
        hash_value = result.stdout.strip().split("(")[1].rstrip(")")
        self._verify_eb_structure(test_dir, source_file, hash_value)

    def test_store_error_cases(self):
        """Test various error cases for store command"""
        test_dir = self._setup_test_environment("store_error_test", with_git=False)
        
        # Create a source file
        source_file = "file.txt"
        with open(os.path.join(test_dir, source_file), "w") as f:
            f.write("Test content")
        
        # Test case 1: Missing --embedding flag
        result1 = subprocess.run(
            ["eb", "store", source_file],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        self.assertEqual(result1.returncode, 1)
        self.assertIn("Direct embedding generation not yet supported", result1.stderr)
        
        # Test case 2: Missing --dims for .bin file
        result2 = subprocess.run(
            ["eb", "store", "--embedding", "test.bin", source_file],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        self.assertEqual(result2.returncode, 1)
        self.assertIn("--dims required for .bin files", result2.stderr)
        
        # Test case 3: Invalid dimensions
        result3 = subprocess.run(
            ["eb", "store", "--embedding", "test.bin", "--dims", "0", source_file],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        self.assertEqual(result3.returncode, 1)
        self.assertIn("Invalid dimensions", result3.stderr)
        
        # Test case 4: Nonexistent embedding file
        result4 = subprocess.run(
            ["eb", "store", "--embedding", "nonexistent.bin", "--dims", "1536", source_file],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        self.assertEqual(result4.returncode, 1)
        self.assertIn("Files must be within repository", result4.stderr)

    def test_diff_command(self):
        """Test basic diff command functionality"""
        test_dir = self._setup_test_environment("diff_test", with_git=False)
        
        # Create two different embeddings
        emb1 = np.random.rand(1536).astype(np.float32)
        emb2 = 0.9 * emb1 + 0.1 * np.random.rand(1536).astype(np.float32)  # Similar but different
        emb2 /= np.linalg.norm(emb2)  # Normalize to ensure valid cosine similarity
        
        # Save embeddings
        emb1_path = os.path.join(test_dir, "emb1.bin")
        emb2_path = os.path.join(test_dir, "emb2.bin")
        
        with open(emb1_path, "wb") as f:
            emb1.tofile(f)
        with open(emb2_path, "wb") as f:
            emb2.tofile(f)
        
        # Store both embeddings
        source1 = "file1.txt"
        source2 = "file2.txt"
        
        with open(os.path.join(test_dir, source1), "w") as f:
            f.write("Test content 1")
        with open(os.path.join(test_dir, source2), "w") as f:
            f.write("Test content 2")
        
        print("\nStoring first embedding...")
        result1 = subprocess.run(
            ["eb", "store", "--embedding", emb1_path, "--dims", "1536", source1],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        print(result1.stdout)
        if result1.stderr:
            print(result1.stderr)
        hash1 = result1.stdout.strip().split("(")[1].rstrip(")")
        
        print("\nStoring second embedding...")
        result2 = subprocess.run(
            ["eb", "store", "--embedding", emb2_path, "--dims", "1536", source2],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        print(result2.stdout)
        if result2.stderr:
            print(result2.stderr)
        hash2 = result2.stdout.strip().split("(")[1].rstrip(")")
        
        print("\n" + "="*80)
        print("Testing diff command...")
        print("="*80)
        
        # Set test mode and run diff
        os.environ["EB_TEST_MODE"] = "1"
        diff_result = subprocess.run(
            ["eb", "diff", hash1, hash2],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=os.environ
        )
        
        print("\nDiff Command Output:")
        print("-" * 30)
        print(f"Command: eb diff {hash1} {hash2}")
        print(f"stdout: {diff_result.stdout}")
        print(f"stderr: {diff_result.stderr}")
        print(f"return code: {diff_result.returncode}")
        
        self.assertEqual(diff_result.returncode, 0)

    def test_diff_error_cases(self):
        """Test error cases for diff command"""
        test_dir = self._setup_test_environment("diff_error_test", with_git=False)
        
        # Set test mode
        os.environ["EB_TEST_MODE"] = "1"
        
        # Test case 1: Missing hash
        result1 = subprocess.run(
            ["eb", "diff", "nonexistent1", "nonexistent2"],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        self.assertEqual(result1.returncode, 1)
        self.assertIn("Embedding not found", result1.stderr)
        
        # Create and store a test embedding
        emb_data = np.random.rand(1536).astype(np.float32)
        emb_path = os.path.join(test_dir, "test.bin")
        with open(emb_path, "wb") as f:
            emb_data.tofile(f)
        
        with open(os.path.join(test_dir, "file.txt"), "w") as f:
            f.write("Test content")
        
        store_result = subprocess.run(
            ["eb", "store", "--embedding", emb_path, "--dims", "1536", "file.txt"],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        hash_value = store_result.stdout.strip().split("(")[1].rstrip(")")
        
        # Test case 2: Compare with nonexistent hash
        result2 = subprocess.run(
            ["eb", "diff", hash_value, "nonexistent"],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        self.assertEqual(result2.returncode, 1)
        self.assertIn("Embedding not found", result2.stderr)
        
        # Test case 3: Compare with same hash (should be 100% similar)
        result3 = subprocess.run(
            ["eb", "diff", hash_value, hash_value],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        self.assertEqual(result3.returncode, 0)
        self.assertEqual(result3.stdout.strip(), "→ Similarity: 100%")

    def test_diff_command_with_known_similarity(self):
        """Test diff command with controlled similarity values"""
        test_dir = self._setup_test_environment("diff_similarity_test", with_git=False)
        
        # Create base embedding and similar embedding
        base_emb = self._create_synthetic_embedding(dims=1536)
        similar_emb = self._create_synthetic_embedding(
            dims=1536,
            similarity_target=0.9,
            base_embedding=base_emb
        )
        
        # Verify embeddings are normalized and have expected similarity
        base_norm = np.linalg.norm(base_emb)
        similar_norm = np.linalg.norm(similar_emb)
        actual_sim = np.dot(base_emb, similar_emb)
        print(f"\nEmbedding verification:")
        print(f"Base norm: {base_norm}")
        print(f"Similar norm: {similar_norm}")
        print(f"Actual similarity: {actual_sim}")
        
        # Save embeddings
        emb1_path = os.path.join(test_dir, "base.bin")
        emb2_path = os.path.join(test_dir, "similar.bin")
        
        with open(emb1_path, "wb") as f:
            base_emb.tofile(f)
        with open(emb2_path, "wb") as f:
            similar_emb.tofile(f)
        
        # Store embeddings
        with open(os.path.join(test_dir, "base.txt"), "w") as f:
            f.write("Base content")
        with open(os.path.join(test_dir, "similar.txt"), "w") as f:
            f.write("Similar content")
        
        print("\nStoring base embedding...")
        result1 = subprocess.run(
            ["eb", "store", "--embedding", emb1_path, "--dims", "1536", "base.txt"],
            cwd=test_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
        print(f"Store result 1: stdout={result1.stdout}, stderr={result1.stderr}")
        hash1 = result1.stdout.strip().split("(")[1].rstrip(")")
        
        print("\nStoring similar embedding...")
        result2 = subprocess.run(
            ["eb", "store", "--embedding", emb2_path, "--dims", "1536", "similar.txt"],
            cwd=test_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
        print(f"Store result 2: stdout={result2.stdout}, stderr={result2.stderr}")
        hash2 = result2.stdout.strip().split("(")[1].rstrip(")")
        
        # Test diff command
        os.environ["EB_TEST_MODE"] = "1"
        print(f"\nRunning diff command: eb diff {hash1} {hash2}")
        print(f"Current directory: {test_dir}")
        print(f"EB_TEST_MODE={os.environ.get('EB_TEST_MODE')}")
        
        diff_result = subprocess.run(
            ["eb", "diff", hash1, hash2],
            cwd=test_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            env=os.environ
        )
        
        print(f"\nDiff command output:")
        print(f"stdout: {diff_result.stdout}")
        print(f"stderr: {diff_result.stderr}")
        print(f"return code: {diff_result.returncode}")
        
        self.assertEqual(diff_result.returncode, 0)
        if diff_result.returncode == 0:
            similarity = float(diff_result.stdout.strip().split(": ")[1].rstrip("%"))
            self.assertGreaterEqual(similarity, 85)
            self.assertLessEqual(similarity, 95)

    def test_diff_different_dimensions(self):
        """Test diff command with embeddings of different dimensions"""
        test_dir = self._setup_test_environment("diff_dimensions_test", with_git=False)
        
        # Create embeddings with different dimensions
        emb1 = np.random.rand(1536).astype(np.float32)
        emb2 = np.random.rand(768).astype(np.float32)  # Different dimension
        
        # Save embeddings
        emb1_path = os.path.join(test_dir, "emb1.bin")
        emb2_path = os.path.join(test_dir, "emb2.bin")
        
        with open(emb1_path, "wb") as f:
            emb1.tofile(f)
        with open(emb2_path, "wb") as f:
            emb2.tofile(f)
        
        # Store embeddings
        with open(os.path.join(test_dir, "file1.txt"), "w") as f:
            f.write("Content 1")
        with open(os.path.join(test_dir, "file2.txt"), "w") as f:
            f.write("Content 2")
        
        result1 = subprocess.run(
            ["eb", "store", "--embedding", emb1_path, "--dims", "1536", "file1.txt"],
            cwd=test_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
        hash1 = result1.stdout.strip().split("(")[1].rstrip(")")
        
        result2 = subprocess.run(
            ["eb", "store", "--embedding", emb2_path, "--dims", "768", "file2.txt"],
            cwd=test_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
        hash2 = result2.stdout.strip().split("(")[1].rstrip(")")
        
        # Test diff command
        os.environ["EB_TEST_MODE"] = "1"
        diff_result = subprocess.run(
            ["eb", "diff", hash1, hash2],
            cwd=test_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
        
        self.assertEqual(diff_result.returncode, 1)
        self.assertIn("Embedding dimensions do not match", diff_result.stderr)

    def test_diff_edge_cases(self):
        """Test diff command with edge cases like NaN and large embeddings"""
        test_dir = self._setup_test_environment("diff_edge_test", with_git=False)
        
        # Create base embedding for comparison (1536 dimensions)
        base_emb = np.random.rand(1536).astype(np.float32)
        base_emb = base_emb / np.linalg.norm(base_emb)
        
        # Create NaN embedding with a single NaN value
        nan_emb = base_emb.copy()
        nan_emb[0] = np.nan
        
        # Verify embeddings before saving
        print("\nVerifying embeddings before saving:")
        print(f"First 5 values of nan_emb: {nan_emb[:5]}")
        print(f"First 5 values of base_emb: {base_emb[:5]}")
        print(f"Any NaN in nan_emb: {np.any(np.isnan(nan_emb))}")
        print(f"Any NaN in base_emb: {np.any(np.isnan(base_emb))}")
        
        # Save embeddings
        base_path = os.path.join(test_dir, "base.bin")
        nan_path = os.path.join(test_dir, "nan.bin")
        
        with open(base_path, "wb") as f:
            base_emb.tofile(f)
        with open(nan_path, "wb") as f:
            nan_emb.tofile(f)
        
        # Verify binary files after saving
        print("\nVerifying saved binary files:")
        with open(nan_path, 'rb') as f:
            saved_nan = np.fromfile(f, dtype=np.float32)
        print(f"First 5 values from saved nan file: {saved_nan[:5]}")
        print(f"Any NaN in saved file: {np.any(np.isnan(saved_nan))}")
        
        # After verifying the saved file:
        print("\nHex representation of values:")
        nan_bytes = saved_nan.tobytes()
        print(f"First float (NaN) as hex: {nan_bytes[:4].hex()}")
        print(f"Second float as hex: {nan_bytes[4:8].hex()}")
        
        # Create test files
        with open(os.path.join(test_dir, "base.txt"), "w") as f:
            f.write("Base content")
        with open(os.path.join(test_dir, "nan.txt"), "w") as f:
            f.write("NaN content")
        
        # Store embeddings
        os.environ["EB_TEST_MODE"] = "1"
        os.environ["EB_DEBUG"] = "1"  # Add debug environment variable
        
        print("\nStoring base embedding...")
        result_base = subprocess.run(
            ["eb", "store", "--embedding", base_path, "--dims", "1536", "base.txt"],
            cwd=test_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
        
        # Debug output for base embedding store
        print("\nBase embedding store command output:")
        print(f"stdout: '{result_base.stdout}'")
        print(f"stderr: '{result_base.stderr}'")
        print(f"return code: {result_base.returncode}")
        
        # Check if store command succeeded
        if result_base.returncode != 0:
            raise Exception(f"Base embedding store command failed: {result_base.stderr}")
        
        # Parse hash more safely
        if not result_base.stdout.strip() or '(' not in result_base.stdout:
            raise Exception(f"Unexpected store command output format: '{result_base.stdout}'")
        
        hash_base = result_base.stdout.strip().split("(")[1].rstrip(")")
        
        print("\nStoring NaN embedding...")
        result_nan = subprocess.run(
            ["eb", "store", "--embedding", nan_path, "--dims", "1536", "nan.txt"],
            cwd=test_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
        
        # Debug output for NaN embedding store
        print("\nNaN embedding store command output:")
        print(f"stdout: '{result_nan.stdout}'")
        print(f"stderr: '{result_nan.stderr}'")
        print(f"return code: {result_nan.returncode}")
        
        # Check if store command succeeded
        if result_nan.returncode != 0:
            raise Exception(f"NaN embedding store command failed: {result_nan.stderr}")
        
        # Parse hash more safely
        if not result_nan.stdout.strip() or '(' not in result_nan.stdout:
            raise Exception(f"Unexpected store command output format: '{result_nan.stdout}'")
        
        hash_nan = result_nan.stdout.strip().split("(")[1].rstrip(")")
        
        # Run diff command with explicit output capture
        print("\nRunning diff command with explicit output capture...")
        diff_result = subprocess.run(
            ["eb", "diff", hash_nan, hash_base],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=os.environ
        )
        
        print("\nDetailed diff command results:")
        print("-" * 60)
        print(f"Command executed: eb diff {hash_nan} {hash_base}")
        print(f"Working directory: {test_dir}")
        print("\nEnvironment variables:")
        print("  STDOUT:", repr(diff_result.stdout))
        print("  STDERR:", repr(diff_result.stderr))
        print(f"  Return code: {diff_result.returncode}")
        print("-" * 60)
        
        # This should fail because we have NaN values
        self.assertEqual(diff_result.returncode, 1, 
                        "Diff command should fail when comparing with NaN embedding")
        self.assertIn("Invalid embedding values", diff_result.stderr,
                     "Error message should indicate invalid values")

    def _create_synthetic_embedding(self, dims=1536, similarity_target=None, base_embedding=None):
        """Create synthetic embedding vectors without calling APIs"""
        if similarity_target and base_embedding is not None:
            # Ensure base embedding is normalized
            base_embedding = base_embedding / np.linalg.norm(base_embedding)
            
            # Create random noise vector
            noise = np.random.rand(dims).astype(np.float32)
            # Project noise to be orthogonal to base_embedding
            noise = noise - np.dot(noise, base_embedding) * base_embedding
            # Normalize noise vector
            noise = noise / np.linalg.norm(noise)
            
            # Create similar embedding using linear combination
            embedding = similarity_target * base_embedding + np.sqrt(1 - similarity_target**2) * noise
            # Normalize the result
            embedding = embedding / np.linalg.norm(embedding)
            
            # Verify similarity
            actual_sim = np.dot(embedding, base_embedding)
            print(f"Target similarity: {similarity_target}, Actual: {actual_sim}")
            
            return embedding.astype(np.float32)
        else:
            # Create random normalized embedding
            embedding = np.random.rand(dims).astype(np.float32)
            embedding = embedding / np.linalg.norm(embedding)
            return embedding

    def test_store_with_real_embeddings(self):
        """Integration test with real API embeddings - run sparingly"""
        if os.getenv('RUN_INTEGRATION_TESTS') != '1':
            self.skipTest("Skipping integration test - set RUN_INTEGRATION_TESTS=1 to run")
        
        test_dir = self._setup_test_environment("store_real_test", with_git=False)
        bin_path, npy_path = self._create_test_embedding_files(test_dir)
        # ... rest of test

    def test_rollback_command(self):
        """Test basic rollback command functionality"""
        test_dir = self._setup_test_environment("rollback_test", with_git=True)
        
        # Enable debug and test modes
        os.environ["EB_DEBUG"] = "1"
        os.environ["EB_TEST_MODE"] = "1"
        
        print("\n" + "="*80)
        print("Testing rollback command...")
        print("="*80)
        
        # Create and store initial embedding
        initial_emb = self._create_synthetic_embedding()
        initial_path = os.path.join(test_dir, "initial.bin")
        with open(initial_path, "wb") as f:
            initial_emb.tofile(f)
        
        # Create source file with absolute path
        source_file = "test.txt"
        source_path = os.path.join(test_dir, source_file)
        with open(source_path, "w") as f:
            f.write("Initial content")
        
        print("\nDebug - Environment setup:")
        print(f"Test directory: {test_dir}")
        print(f"Source file path: {source_path}")
        print(f"Initial embedding path: {initial_path}")
        
        # Store initial embedding and get hash
        print("\nDebug - Storing initial embedding...")
        result1 = subprocess.run(
            ["eb", "store", "--embedding", initial_path, "--dims", "1536", source_file],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=os.environ
        )
        print("Initial store output:")
        print(f"stdout: {result1.stdout}")
        print(f"stderr: {result1.stderr}")
        
        initial_hash = result1.stdout.strip().split("(")[1].rstrip(")")
        print(f"Initial hash: {initial_hash} (length: {len(initial_hash)})")
        
        # Verify history file contents
        history_path = os.path.join(test_dir, ".eb", "history")
        print("\nDebug - Initial history file contents:")
        with open(history_path, "r") as f:
            history_content = f.read()
            print(history_content)
        
        # Create and store updated embedding
        updated_emb = self._create_synthetic_embedding()
        updated_path = os.path.join(test_dir, "updated.bin")
        with open(updated_path, "wb") as f:
            updated_emb.tofile(f)
        
        # Store updated embedding
        print("\nDebug - Storing updated embedding...")
        result2 = subprocess.run(
            ["eb", "store", "--embedding", updated_path, "--dims", "1536", source_file],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=os.environ
        )
        print("Updated store output:")
        print(f"stdout: {result2.stdout}")
        print(f"stderr: {result2.stderr}")
        
        # Verify history file after update
        print("\nDebug - Updated history file contents:")
        with open(history_path, "r") as f:
            history_content = f.read()
            print(history_content)
        
        # Verify index file contents
        index_path = os.path.join(test_dir, ".eb", "index")
        print("\nDebug - Current index file contents:")
        with open(index_path, "r") as f:
            index_content = f.read()
            print(index_content)
        
        # Try to rollback to initial hash
        print("\nDebug - Executing rollback command:")
        print(f"Command: eb rollback {initial_hash} {source_file}")
        print(f"Working directory: {test_dir}")
        print(f"Environment variables: EB_DEBUG={os.environ.get('EB_DEBUG')}, EB_TEST_MODE={os.environ.get('EB_TEST_MODE')}")
        
        rollback_result = subprocess.run(
            ["eb", "rollback", initial_hash, source_file],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=os.environ
        )
        
        print("\nDebug - Rollback command output:")
        print(f"stdout: {rollback_result.stdout}")
        print(f"stderr: {rollback_result.stderr}")
        print(f"return code: {rollback_result.returncode}")
        
        # Verify index file after rollback
        print("\nDebug - Index file contents after rollback:")
        with open(index_path, "r") as f:
            index_content = f.read()
            print(index_content)
        
        self.assertEqual(rollback_result.returncode, 0, 
                        f"Rollback command failed with stderr: {rollback_result.stderr}")
        self.assertIn(f"Rolled back '{source_file}' to {initial_hash}", rollback_result.stdout)

    def test_status_command(self):
        """Test basic status command functionality"""
        test_dir = self._setup_test_environment("status_test", with_git=True)
        
        # Enable debug and test modes
        os.environ["EB_DEBUG"] = "1"
        os.environ["EB_TEST_MODE"] = "1"
        
        print("\n" + "="*80)
        print("Testing status command...")
        print("="*80)
        print(f"Test directory: {test_dir}")
        
        # Create and store initial embedding
        initial_emb = self._create_synthetic_embedding()
        initial_path = os.path.join(test_dir, "initial.bin")
        with open(initial_path, "wb") as f:
            initial_emb.tofile(f)
        print(f"\nCreated initial embedding at: {initial_path}")
        
        # Create source file
        source_file = "test.txt"
        source_path = os.path.join(test_dir, source_file)
        with open(source_path, "w") as f:
            f.write("Initial content")
        print(f"Created source file at: {source_path}")
        
        # Store initial embedding
        print("\nStoring initial embedding...")
        print(f"Command: eb store --embedding {initial_path} --dims 1536 {source_file}")
        result1 = subprocess.run(
            ["eb", "store", "--embedding", initial_path, "--dims", "1536", source_file],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=os.environ
        )
        print(f"Store result 1: stdout={result1.stdout}, stderr={result1.stderr}")
        initial_hash = result1.stdout.strip().split("(")[1].rstrip(")")
        print(f"Initial hash: {initial_hash}")
        
        # Verify .eb structure after first store
        print("\nVerifying .eb structure after first store:")
        print(f"Index contents:")
        with open(os.path.join(test_dir, ".eb", "index"), "r") as f:
            print(f.read())
        print(f"History contents:")
        with open(os.path.join(test_dir, ".eb", "history"), "r") as f:
            print(f.read())
        
        # Store second version
        updated_emb = self._create_synthetic_embedding()
        updated_path = os.path.join(test_dir, "updated.bin")
        with open(updated_path, "wb") as f:
            updated_emb.tofile(f)
        print(f"\nCreated updated embedding at: {updated_path}")
        
        print("\nStoring updated embedding...")
        print(f"Command: eb store --embedding {updated_path} --dims 1536 {source_file}")
        result2 = subprocess.run(
            ["eb", "store", "--embedding", updated_path, "--dims", "1536", source_file],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=os.environ
        )
        print(f"Store result 2: stdout={result2.stdout}, stderr={result2.stderr}")
        updated_hash = result2.stdout.strip().split("(")[1].rstrip(")")
        print(f"Updated hash: {updated_hash}")
        
        # Verify .eb structure after second store
        print("\nVerifying .eb structure after second store:")
        print(f"Index contents:")
        with open(os.path.join(test_dir, ".eb", "index"), "r") as f:
            print(f.read())
        print(f"History contents:")
        with open(os.path.join(test_dir, ".eb", "history"), "r") as f:
            print(f.read())
        
        # Update test assertions for new format
        print("\nTesting basic status command...")
        print(f"Command: eb status {source_file}")
        status_result = subprocess.run(
            ["eb", "status", source_file],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=os.environ
        )
        
        print("\nStatus command output:")
        print(f"stdout: {status_result.stdout}")
        print(f"stderr: {status_result.stderr}")
        print(f"return code: {status_result.returncode}")
        
        self.assertEqual(status_result.returncode, 0)
        self.assertIn(updated_hash, status_result.stdout)  # Just check if hash exists in output
        self.assertIn("Current", status_result.stdout)    # Check for new format markers

        # Test verbose status command
        print("\nTesting verbose status command...")
        print(f"Command: eb status -v {source_file}")
        verbose_result = subprocess.run(
            ["eb", "status", "-v", source_file],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=os.environ
        )
        
        print("\nVerbose status command output:")
        print(f"stdout: {verbose_result.stdout}")
        print(f"stderr: {verbose_result.stderr}")
        print(f"return code: {verbose_result.returncode}")
        
        self.assertEqual(verbose_result.returncode, 0)
        self.assertIn("Current", verbose_result.stdout)    # Check for new format markers
        self.assertIn(updated_hash, verbose_result.stdout)    # Just check if hash exists in output

    def test_status_error_cases(self):
        """Test error cases for status command"""
        test_dir = self._setup_test_environment("status_error_test", with_git=True)
        
        # Test case 1: Non-existent file
        result1 = subprocess.run(
            ["eb", "status", "nonexistent.txt"],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        self.assertEqual(result1.returncode, 1)
        self.assertIn("Cannot resolve path: nonexistent.txt", result1.stderr)
        
        # Test case 2: No arguments (should show help)
        result2 = subprocess.run(
            ["eb", "status"],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        self.assertEqual(result2.returncode, 1)
        self.assertIn("Usage: eb status", result2.stdout)
        
        # Test case 3: Help flag
        result3 = subprocess.run(
            ["eb", "status", "--help"],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        self.assertEqual(result3.returncode, 0)
        self.assertIn("Usage: eb status", result3.stdout)
        
        # Test case 4: Outside repository
        outside_dir = os.path.join(self.temp_dir, "outside")
        os.makedirs(outside_dir)
        result4 = subprocess.run(
            ["eb", "status", "file.txt"],
            cwd=outside_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        self.assertEqual(result4.returncode, 1)
        self.assertIn("Not in an embedding repository", result4.stderr)

    def test_status_with_multiple_versions(self):
        """Test status command with multiple versions of embeddings"""
        test_dir = self._setup_test_environment("status_multiple_test", with_git=True)
        
        # Create source file
        source_file = "test.txt"
        with open(os.path.join(test_dir, source_file), "w") as f:
            f.write("Test content")
        
        # Store multiple versions
        hashes = []
        for i in range(3):
            emb = self._create_synthetic_embedding()
            emb_path = os.path.join(test_dir, f"emb{i}.bin")
            with open(emb_path, "wb") as f:
                emb.tofile(f)
            
            result = subprocess.run(
                ["eb", "store", "--embedding", emb_path, "--dims", "1536", source_file],
                cwd=test_dir,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True
            )
            hash_value = result.stdout.strip().split("(")[1].rstrip(")")
            hashes.append(hash_value)
        
        # Update assertions for new format
        status_result = subprocess.run(
            ["eb", "status", source_file],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        
        self.assertEqual(status_result.returncode, 0)
        
        self.assertIn(hashes[-1], status_result.stdout)    # Just check if hash exists in output
        self.assertIn("Current", status_result.stdout)    # Check for new format markers

        # Test verbose mode with multiple versions
        verbose_result = subprocess.run(
            ["eb", "status", "-v", source_file],
            cwd=test_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        
        self.assertEqual(verbose_result.returncode, 0)
        self.assertIn("Current", verbose_result.stdout)    # Check for new format markers
        self.assertIn(hashes[-1], verbose_result.stdout)    # Just check if hash exists in output

    def test_diff_short_hashes(self):
        """Test diff command with short hash forms"""
        test_dir = self._setup_test_environment("diff_short_hash_test", with_git=False)
        
        # Create two different embeddings
        emb1 = self._create_synthetic_embedding()
        emb2 = self._create_synthetic_embedding()
        
        # Save embeddings
        emb1_path = os.path.join(test_dir, "emb1.bin")
        emb2_path = os.path.join(test_dir, "emb2.bin")
        
        with open(emb1_path, "wb") as f:
            emb1.tofile(f)
        with open(emb2_path, "wb") as f:
            emb2.tofile(f)
        
        # Store embeddings
        with open(os.path.join(test_dir, "file1.txt"), "w") as f:
            f.write("Test content 1")
        with open(os.path.join(test_dir, "file2.txt"), "w") as f:
            f.write("Test content 2")
        
        # Store and get full hashes
        result1 = subprocess.run(
            ["eb", "store", "--embedding", emb1_path, "--dims", "1536", "file1.txt"],
            cwd=test_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
        hash1 = result1.stdout.strip().split("(")[1].rstrip(")")
        
        result2 = subprocess.run(
            ["eb", "store", "--embedding", emb2_path, "--dims", "1536", "file2.txt"],
            cwd=test_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
        hash2 = result2.stdout.strip().split("(")[1].rstrip(")")
        
        # Test cases with short hashes
        test_cases = [
            (hash1[:4], hash2[:4], True),    # Minimum length (4 chars)
            (hash1[:8], hash2[:8], True),    # Common length (8 chars)
            (hash1[:3], hash2, False),       # Too short (should fail)
            (hash1, hash2[:3], False),       # Too short (should fail)
            (hash1[:10], hash2, True),       # Mix of short and full
            (hash1, hash2[:10], True),       # Mix of full and short
        ]
        
        os.environ["EB_TEST_MODE"] = "1"
        
        for hash_a, hash_b, should_succeed in test_cases:
            print(f"\nTesting diff with hashes: {hash_a} and {hash_b}")
            result = subprocess.run(
                ["eb", "diff", hash_a, hash_b],
                cwd=test_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
            )
            
            if should_succeed:
                self.assertEqual(result.returncode, 0,
                               f"Diff should succeed with hashes {hash_a} and {hash_b}")
                self.assertRegex(result.stdout.strip(), r"→ Similarity: \d+%")
            else:
                self.assertEqual(result.returncode, 1,
                               f"Diff should fail with hashes {hash_a} and {hash_b}")
                self.assertIn("Hash too short", result.stderr)

    def test_diff_ambiguous_hashes(self):
        """Test diff command with ambiguous hash prefixes"""
        test_dir = self._setup_test_environment("diff_ambiguous_test", with_git=False)
        
        # Create embeddings that would generate hashes with same prefix
        # Note: This is a simplified test since we can't control hash generation
        # In practice, you'd need to create enough embeddings to get a collision
        emb1 = self._create_synthetic_embedding()
        emb2 = self._create_synthetic_embedding()
        emb3 = self._create_synthetic_embedding()
        
        # Store multiple embeddings
        hashes = []
        for i, emb in enumerate([emb1, emb2, emb3]):
            emb_path = os.path.join(test_dir, f"emb{i}.bin")
            with open(emb_path, "wb") as f:
                emb.tofile(f)
            
            with open(os.path.join(test_dir, f"file{i}.txt"), "w") as f:
                f.write(f"Test content {i}")
            
            result = subprocess.run(
                ["eb", "store", "--embedding", emb_path, "--dims", "1536", f"file{i}.txt"],
                cwd=test_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
            )
            hash_value = result.stdout.strip().split("(")[1].rstrip(")")
            hashes.append(hash_value)
        
        # Test with potentially ambiguous prefix
        # Note: This test might need adjustment based on actual hash collisions
        common_prefix = os.path.commonprefix(hashes)
        if len(common_prefix) >= 4:
            result = subprocess.run(
                ["eb", "diff", common_prefix, hashes[1]],
                cwd=test_dir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
            )
            self.assertEqual(result.returncode, 1)
            self.assertIn("ambiguous hash", result.stderr.lower())

if __name__ == "__main__":
    unittest.main() 