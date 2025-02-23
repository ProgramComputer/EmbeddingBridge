/*
 * EmbeddingBridge - Debug Utilities
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef EB_DEBUG_H
#define EB_DEBUG_H

#ifdef EB_DEBUG
    #define DEBUG_PRINT(...) fprintf(stderr, "DEBUG: " __VA_ARGS__)
#else
    #define DEBUG_PRINT(...) ((void)0)  // Do nothing in release builds
#endif

#endif /* EB_DEBUG_H */ 