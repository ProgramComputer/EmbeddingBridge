/*
 * EmbeddingBridge - Configuration Definitions
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef EB_CONFIG_H
#define EB_CONFIG_H

/* Default HTTP settings */
#define EB_HTTP_USER_AGENT "EmbeddingBridge/1.0"
#define EB_HTTP_TIMEOUT 30  /* Default timeout in seconds */
#define EB_HTTP_MAX_REDIRECTS 5
#define EB_HTTP_BUFFER_SIZE 4096

/* Default authentication settings */
#define EB_AUTH_TOKEN_ENV "EB_AUTH_TOKEN"
#define EB_AUTH_USER_ENV "EB_AUTH_USER"
#define EB_AUTH_PASSWORD_ENV "EB_AUTH_PASSWORD"

/* Config file related settings */
#define EB_CONFIG_FILE ".embr/config"
#define EB_CONFIG_DEFAULT_SECTION "core"
#define EB_CONFIG_BUFFER_SIZE 4096

#endif /* EB_CONFIG_H */ 