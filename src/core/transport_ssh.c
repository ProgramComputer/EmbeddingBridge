/*
 * EmbeddingBridge - SSH Transport Implementation
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
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>

#include "transport.h"
#include "error.h"
#include "debug.h"

/* Stub implementation - SSH transport is disabled */
struct ssh_data {
	void *placeholder;  /* Just a placeholder to avoid empty struct */
};

static int ssh_connect(eb_transport_t *transport)
{
	if (!transport)
		return EB_ERROR_INVALID_PARAMETER;
	
	snprintf(transport->error_msg, sizeof(transport->error_msg),
		 "SSH transport is not supported in this build");
	transport->last_error = EB_ERROR_NOT_IMPLEMENTED;
	
	DEBUG_PRINT("SSH transport not supported");
	return EB_ERROR_NOT_IMPLEMENTED;
}

static int ssh_disconnect(eb_transport_t *transport)
{
	if (!transport)
		return EB_ERROR_INVALID_PARAMETER;
	
	return EB_SUCCESS;
}

static int ssh_send_data(eb_transport_t *transport, const void *data, size_t size, const char *hash)
{
	(void)data;  /* Suppress unused parameter warning */
	(void)size;  /* Suppress unused parameter warning */
	(void)hash;  /* Suppress unused parameter warning */
	
	if (!transport)
		return EB_ERROR_INVALID_PARAMETER;
	
	snprintf(transport->error_msg, sizeof(transport->error_msg),
			 "SSH transport is not supported in this build");
	transport->last_error = EB_ERROR_NOT_IMPLEMENTED;
	
	return EB_ERROR_NOT_IMPLEMENTED;
}

static int ssh_receive_data(eb_transport_t *transport, void *buffer, 
						  size_t size, size_t *received)
{
	(void)buffer;  /* Suppress unused parameter warning */
	(void)size;    /* Suppress unused parameter warning */
	
	if (!transport || !received)
		return EB_ERROR_INVALID_PARAMETER;
	
	*received = 0;
	
	snprintf(transport->error_msg, sizeof(transport->error_msg),
		 "SSH transport is not supported in this build");
	transport->last_error = EB_ERROR_NOT_IMPLEMENTED;
	
	return EB_ERROR_NOT_IMPLEMENTED;
}

static int ssh_list_refs(eb_transport_t *transport, char ***refs, size_t *count)
{
	if (!transport || !refs || !count)
		return EB_ERROR_INVALID_PARAMETER;
	
	*refs = NULL;
	*count = 0;
	
	snprintf(transport->error_msg, sizeof(transport->error_msg),
		 "SSH transport is not supported in this build");
	transport->last_error = EB_ERROR_NOT_IMPLEMENTED;
	
	return EB_ERROR_NOT_IMPLEMENTED;
}

/* SSH transport operations structure */
struct transport_ops ssh_ops = {
	.connect = ssh_connect,
	.disconnect = ssh_disconnect,
	.send_data = ssh_send_data,
	.receive_data = ssh_receive_data,
	.list_refs = ssh_list_refs
};

/* SSH transport initialization */
int ssh_transport_init(void)
{
	DEBUG_PRINT("SSH transport module initialized (stub implementation)");
	return EB_SUCCESS;
} 