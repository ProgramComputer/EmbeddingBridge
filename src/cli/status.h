/*
 * EmbeddingBridge - Status Command Implementation
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef EB_CLI_STATUS_H
#define EB_CLI_STATUS_H

/**
 * Display the current status and history of embeddings for a source file.
 * Shows the current hash and version history in a Git-like format.
 *
 * @param argc  Argument count
 * @param argv  Argument vector
 * @return      0 on success, 1 on error
 */
int cmd_status(int argc, char **argv);

#endif /* EB_CLI_STATUS_H */ 