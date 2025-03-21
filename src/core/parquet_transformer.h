/**
 * EmbeddingBridge - Bridging the gap between embedding models and applications
 * Copyright (C) 2024
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef EB_PARQUET_TRANSFORMER_H
#define EB_PARQUET_TRANSFORMER_H

#include <stdbool.h>
#include "transformer.h"
#include "status.h"

/**
 * Arrow/Parquet libraries are required for this transformer.
 * These must be installed on the system before this module can be used.
 *
 * On Debian/Ubuntu: sudo apt install -y libarrow-dev libparquet-dev
 * On CentOS/RHEL: sudo yum install -y arrow-devel parquet-devel
 * On macOS: brew install apache-arrow
 *
 * Alternatively, run: make build-arrow
 */

/**
 * Create a new Parquet transformer
 *
 * @param compression_level ZSTD compression level (0-22, 0=disabled)
 * @return Pointer to transformer or NULL if creation failed (libraries not installed)
 */
struct eb_transformer *eb_parquet_transformer_create(int compression_level);

/**
 * Register the parquet transformer with the system
 *
 * @return EB_SUCCESS if registered, EB_ERROR_DEPENDENCY_MISSING if Arrow/Parquet not installed
 */
eb_status_t eb_register_parquet_transformer(void);

#endif /* EB_PARQUET_TRANSFORMER_H */ 