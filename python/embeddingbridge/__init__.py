"""
EmbeddingBridge - Python Interface to Vector Embedding Storage
Copyright (C) 2024 ProgramComputer

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
"""

from .core import EmbeddingStore, EmbeddingBridge, CommandResult

__version__ = "0.1.0"
__all__ = ['EmbeddingStore', 'EmbeddingBridge', 'CommandResult'] 