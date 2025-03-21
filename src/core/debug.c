/*
 * EmbeddingBridge - Debug Utilities Implementation
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "debug.h"

/* Define the global debug level variable */
eb_debug_level_t eb_debug_level = EB_DEBUG_NONE;

/**
 * Initialize the debug system
 *
 * This checks environment variables to set the initial debug level.
 */
void eb_debug_init(void) {
    const char *debug_env = getenv("EB_DEBUG_LEVEL");
    
    if (debug_env) {
        int level = atoi(debug_env);
        if (level >= EB_DEBUG_NONE && level <= EB_DEBUG_TRACE) {
            eb_debug_level = (eb_debug_level_t)level;
        }
    }
    
    /* Enable debug mode if EB_DEBUG or DEBUG environment variable is set */
    const char *debug_enabled = getenv("EB_DEBUG");
    if (!debug_enabled) {
        debug_enabled = getenv("DEBUG");  /* Also check for DEBUG without EB_ prefix */
    }
    
    if (debug_enabled && (strcmp(debug_enabled, "1") == 0 || 
                          strcasecmp(debug_enabled, "true") == 0 ||
                          strcasecmp(debug_enabled, "yes") == 0 ||
                          strcasecmp(debug_enabled, "on") == 0)) {
        if (eb_debug_level == EB_DEBUG_NONE) {
            eb_debug_level = EB_DEBUG_INFO;  /* Default to INFO level if debug is enabled */
        }
    }
    
    DEBUG_INFO("Debug system initialized (level: %d)", eb_debug_level);
}

/**
 * Set the debug level
 *
 * @param level New debug level
 */
void eb_debug_set_level(eb_debug_level_t level) {
    if (level >= EB_DEBUG_NONE && level <= EB_DEBUG_TRACE) {
        int old_level = eb_debug_level;
        eb_debug_level = level;
        DEBUG_INFO("Debug level changed from %d to %d", old_level, level);
    }
}

/**
 * Get the current debug level
 *
 * @return Current debug level
 */
eb_debug_level_t eb_debug_get_level(void) {
    return eb_debug_level;
} 