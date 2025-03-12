/*
 * EmbeddingBridge - Log Command Interface
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef EB_LOG_H
#define EB_LOG_H

/* Return codes */
#define LOG_SUCCESS          0
#define LOG_ERROR_ARGS       1
#define LOG_ERROR_REPO       2
#define LOG_ERROR_FILE       3
#define LOG_ERROR_MEMORY     4

/**
 * Display the log of embeddings for one or more files
 * 
 * @param argc: Argument count
 * @param argv: Arguments including file paths and options
 * @return: Status code (LOG_SUCCESS on success)
 */
int cmd_log(int argc, char** argv);

#endif /* EB_LOG_H */ 