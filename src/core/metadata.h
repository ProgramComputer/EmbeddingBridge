/*
 * EmbeddingBridge - Metadata Handling
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef EB_METADATA_H
#define EB_METADATA_H

#include "types.h"

/* Simple key-value format:
 * source: file.txt
 * timestamp: 2024-02-21T12:00:00Z
 * model: user-provided
 */

eb_status_t eb_write_metadata(const char* path, const char* source, const char* model);
eb_status_t eb_read_metadata(const char* path, char** source, char** model);

#endif /* EB_METADATA_H */ 