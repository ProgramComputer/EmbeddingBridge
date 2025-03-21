/*
 * EmbeddingBridge - Set Command Header
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef EB_SET_H
#define EB_SET_H

#include "../core/types.h"
#include "../core/store.h"

/**
 * Main entry point for the set command
 * 
 * @param argc Argument count
 * @param argv Argument vector
 * @return Status code (0 = success)
 */
int cmd_set(int argc, char** argv);

/**
 * Create a new set of embeddings
 * 
 * @param name Name of the set to create
 * @param description Optional description of the set
 * @param base_set Optional base set to clone from
 * @return Status code (0 = success)
 */
eb_status_t set_create(const char* name, const char* description, const char* base_set);

/**
 * List all available sets
 * 
 * @param verbose Show detailed information about each set
 * @return Status code (0 = success)
 */
eb_status_t set_list(bool verbose);

/**
 * Switch to a different set
 * 
 * @param name Name of the set to switch to
 * @return Status code (0 = success)
 */
eb_status_t set_switch(const char* name);

/**
 * Show differences between sets
 * 
 * @param set1 First set to compare
 * @param set2 Second set to compare
 * @return Status code (0 = success)
 */
eb_status_t set_diff(const char* set1, const char* set2);

/**
 * Delete a set of embeddings
 * 
 * @param name Name of the set to delete
 * @param force Force deletion even if set contains unique embeddings
 * @return Status code (0 = success)
 */
eb_status_t set_delete(const char* name, bool force);

/**
 * Show status of current set
 * 
 * @return Status code (0 = success)
 */
eb_status_t set_status(void);

/**
 * Get the currently active set name
 * 
 * @param name_out Buffer to store the name
 * @param size Size of the buffer
 * @return Status code (0 = success)
 */
eb_status_t get_current_set(char* name_out, size_t size);

#endif /* EB_SET_H */ 