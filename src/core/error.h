/*
 * EmbeddingBridge - Error Handling
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef EB_ERROR_H
#define EB_ERROR_H

/* Include the common status codes */
#include "status.h"

/* Convert error code to string */
const char* eb_status_str(eb_status_t status);

/* Get error message for system error */
const char* eb_strerror(int errnum);

/* Set error state */
void eb_set_error(eb_status_t status, const char* message);

#endif /* EB_ERROR_H */ 