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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Debug levels */
typedef enum {
    EB_DEBUG_NONE = 0,    /* No debugging */
    EB_DEBUG_ERROR = 1,   /* Error messages only */
    EB_DEBUG_WARN = 2,    /* Warnings and errors */
    EB_DEBUG_INFO = 3,    /* Informational messages */
    EB_DEBUG_DEBUG = 4,   /* Debug-level messages */
    EB_DEBUG_TRACE = 5    /* Trace-level messages (very verbose) */
} eb_debug_level_t;

/* Current debug level (can be changed at runtime) */
extern eb_debug_level_t eb_debug_level;

/* Get the basename of a file path */
#define BASENAME(path) (strrchr(path, '/') ? strrchr(path, '/') + 1 : path)

/* Debug printf macros */
#ifdef EB_DEBUG_ENABLED
    /* When debugging is enabled */
    #define DEBUG_LEVEL(level, fmt, ...) \
        do { \
            if (level <= eb_debug_level) { \
                time_t now = time(NULL); \
                struct tm *tm_info = localtime(&now); \
                char time_str[20]; \
                strftime(time_str, 20, "%Y-%m-%d %H:%M:%S", tm_info); \
                fprintf(stderr, "[%s] %s:%d: " fmt "\n", \
                        time_str, BASENAME(__FILE__), __LINE__, ##__VA_ARGS__); \
            } \
        } while (0)
    
    #define DEBUG_ERROR(fmt, ...) DEBUG_LEVEL(EB_DEBUG_ERROR, "ERROR: " fmt, ##__VA_ARGS__)
    #define DEBUG_WARN(fmt, ...)  DEBUG_LEVEL(EB_DEBUG_WARN, "WARN: " fmt, ##__VA_ARGS__)
    #define DEBUG_INFO(fmt, ...)  DEBUG_LEVEL(EB_DEBUG_INFO, "INFO: " fmt, ##__VA_ARGS__)
    #define DEBUG_PRINT(fmt, ...) DEBUG_LEVEL(EB_DEBUG_DEBUG, fmt, ##__VA_ARGS__)
    #define DEBUG_TRACE(fmt, ...) DEBUG_LEVEL(EB_DEBUG_TRACE, "TRACE: " fmt, ##__VA_ARGS__)
#else
    /* When debugging is disabled */
    #define DEBUG_ERROR(fmt, ...) do {} while (0)
    #define DEBUG_WARN(fmt, ...)  do {} while (0)
    #define DEBUG_INFO(fmt, ...)  do {} while (0)
    #define DEBUG_PRINT(fmt, ...) do {} while (0)
    #define DEBUG_TRACE(fmt, ...) do {} while (0)
#endif

/* Assert macro */
#ifdef EB_DEBUG_ENABLED
    #define EB_ASSERT(cond, msg) \
        do { \
            if (!(cond)) { \
                DEBUG_ERROR("Assertion failed: %s", msg); \
                abort(); \
            } \
        } while (0)
#else
    #define EB_ASSERT(cond, msg) do {} while (0)
#endif

/* Initialize debugging */
void eb_debug_init(void);

/* Set debug level */
void eb_debug_set_level(eb_debug_level_t level);

/* Get current debug level */
eb_debug_level_t eb_debug_get_level(void);

#endif /* EB_DEBUG_H */ 