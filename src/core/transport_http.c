/*
 * EmbeddingBridge - HTTP Transport Implementation
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "transport.h"
#include "error.h"
#include "debug.h"
#include "config.h"

/* Stub implementation - HTTP transport is disabled */
struct http_data {
	void *placeholder;  /* Just a placeholder to avoid empty struct */
};

static int http_connect(eb_transport_t *transport)
{
	if (!transport)
		return EB_ERROR_INVALID_PARAMETER;
	
	snprintf(transport->error_msg, sizeof(transport->error_msg),
			 "HTTP transport is not supported in this build");
	transport->last_error = EB_ERROR_NOT_IMPLEMENTED;
	
	DEBUG_PRINT("HTTP transport not supported");
	return EB_ERROR_NOT_IMPLEMENTED;
}

static int http_disconnect(eb_transport_t *transport)
{
	if (!transport)
		return EB_ERROR_INVALID_PARAMETER;
	
	return EB_SUCCESS;
}

static int http_send_data(eb_transport_t *transport, const void *data, size_t size)
{
	(void)data;  /* Suppress unused parameter warning */
	(void)size;  /* Suppress unused parameter warning */
	
	if (!transport)
		return EB_ERROR_INVALID_PARAMETER;
	
	snprintf(transport->error_msg, sizeof(transport->error_msg),
			 "HTTP transport is not supported in this build");
	transport->last_error = EB_ERROR_NOT_IMPLEMENTED;
	
	return EB_ERROR_NOT_IMPLEMENTED;
}

static int http_receive_data(eb_transport_t *transport, void *buffer, 
						  size_t size, size_t *received)
{
	(void)buffer;  /* Suppress unused parameter warning */
	(void)size;    /* Suppress unused parameter warning */
	
	if (!transport || !received)
		return EB_ERROR_INVALID_PARAMETER;
	
	*received = 0;
	
	snprintf(transport->error_msg, sizeof(transport->error_msg),
			 "HTTP transport is not supported in this build");
	transport->last_error = EB_ERROR_NOT_IMPLEMENTED;
	
	return EB_ERROR_NOT_IMPLEMENTED;
}

static int http_list_refs(eb_transport_t *transport, char ***refs, size_t *count)
{
	if (!transport || !refs || !count)
		return EB_ERROR_INVALID_PARAMETER;
	
	*refs = NULL;
	*count = 0;
	
	snprintf(transport->error_msg, sizeof(transport->error_msg),
			 "HTTP transport is not supported in this build");
	transport->last_error = EB_ERROR_NOT_IMPLEMENTED;
	
	return EB_ERROR_NOT_IMPLEMENTED;
}

/* HTTP transport operations structure */
struct transport_ops http_ops = {
	.connect = http_connect,
	.disconnect = http_disconnect,
	.send_data = http_send_data,
	.receive_data = http_receive_data,
	.list_refs = http_list_refs
};

/* HTTP transport initialization */
int http_transport_init(void)
{
	DEBUG_PRINT("HTTP transport module initialized (stub implementation)");
	return EB_SUCCESS;
} 