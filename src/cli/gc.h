/*
 * EmbeddingBridge - Garbage Collection CLI Command
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef EB_CLI_GC_H
#define EB_CLI_GC_H

#include "../core/error.h"

/**
 * Handle 'eb gc' command
 * 
 * @param argc Argument count
 * @param argv Argument array
 * @return Status code (0 = success)
 */
int cmd_gc(int argc, char** argv);

#endif /* EB_CLI_GC_H */ 