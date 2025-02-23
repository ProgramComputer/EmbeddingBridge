"""Embedding Bridge - A bridge between Python and C for embedding management."""

from .store import VectorMemoryStore
from .bridge import EmbeddingBridge
from .api import EmbeddingMigrationTool

__version__ = "0.1.0"

__all__ = ['VectorMemoryStore', 'EmbeddingBridge', 'EmbeddingMigrationTool']
