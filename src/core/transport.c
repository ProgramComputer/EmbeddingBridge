/*
 * EmbeddingBridge - Transport Layer Implementation
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
#include "transport.h"
#include "error.h"
#include "debug.h"

/* Forward declarations of protocol-specific operations */
extern struct transport_ops ssh_ops;
extern struct transport_ops http_ops;
extern struct transport_ops local_ops;

/* Helper function to check for URL prefix */
static int starts_with(const char *str, const char *prefix)
{
	return strncmp(str, prefix, strlen(prefix)) == 0;
}

eb_transport_t *transport_open(const char *url)
{
	eb_transport_t *transport;
	
	if (!url)
		return NULL;
		
	transport = calloc(1, sizeof(*transport));
	if (!transport)
		return NULL;
		
	transport->url = strdup(url);
	if (!transport->url) {
		free(transport);
		return NULL;
	}
	
	/* Determine transport type based on URL */
	if (starts_with(url, "ssh://") || strchr(url, '@')) {
		transport->type = TRANSPORT_SSH;
		transport->ops = &ssh_ops;
		DEBUG_PRINT("Using SSH transport for %s", url);
	} else if (starts_with(url, "http://") || starts_with(url, "https://")) {
		transport->type = TRANSPORT_HTTP;
		transport->ops = &http_ops;
		DEBUG_PRINT("Using HTTP transport for %s", url);
	} else if (starts_with(url, "file://") || !strchr(url, ':')) {
		transport->type = TRANSPORT_LOCAL;
		transport->ops = &local_ops;
		DEBUG_PRINT("Using local transport for %s", url);
	} else {
		transport->type = TRANSPORT_UNKNOWN;
		snprintf(transport->error_msg, sizeof(transport->error_msg),
			 "Unsupported URL scheme: %s", url);
		transport->last_error = EB_ERROR_UNSUPPORTED;
		free((void *)transport->url);
		free(transport);
		return NULL;
	}
	
	transport->connected = false;
	transport->last_error = EB_SUCCESS;
	transport->error_msg[0] = '\0';
	
	return transport;
}

void transport_close(eb_transport_t *transport)
{
	if (!transport)
		return;
	
	if (transport->connected)
		transport_disconnect(transport);
	
	free((void *)transport->url);
	free(transport);
}

int transport_connect(eb_transport_t *transport)
{
	int result;
	
	if (!transport)
		return EB_ERROR_INVALID_PARAMETER;
	
	if (!transport->ops || !transport->ops->connect) {
		transport->last_error = EB_ERROR_NOT_IMPLEMENTED;
		snprintf(transport->error_msg, sizeof(transport->error_msg),
			 "Connect operation not implemented for this transport");
		return EB_ERROR_NOT_IMPLEMENTED;
	}
	
	if (transport->connected)
		return EB_SUCCESS;
	
	result = transport->ops->connect(transport);
	if (result == EB_SUCCESS)
		transport->connected = true;
	else
		transport->last_error = result;
	
	return result;
}

int transport_disconnect(eb_transport_t *transport)
{
	int result;
	
	if (!transport)
		return EB_ERROR_INVALID_PARAMETER;
	
	if (!transport->connected)
		return EB_SUCCESS;
	
	if (!transport->ops || !transport->ops->disconnect) {
		transport->last_error = EB_ERROR_NOT_IMPLEMENTED;
		snprintf(transport->error_msg, sizeof(transport->error_msg),
			 "Disconnect operation not implemented for this transport");
		return EB_ERROR_NOT_IMPLEMENTED;
	}
	
	result = transport->ops->disconnect(transport);
	if (result == EB_SUCCESS)
		transport->connected = false;
	else
		transport->last_error = result;
	
	return result;
}

int transport_send_data(eb_transport_t *transport, const void *data, size_t size)
{
	int result;
	
	if (!transport || !data)
		return EB_ERROR_INVALID_PARAMETER;
	
	if (!transport->connected) {
		result = transport_connect(transport);
		if (result != EB_SUCCESS)
			return result;
	}
	
	if (!transport->ops || !transport->ops->send_data) {
		transport->last_error = EB_ERROR_NOT_IMPLEMENTED;
		snprintf(transport->error_msg, sizeof(transport->error_msg),
			 "Send operation not implemented for this transport");
		return EB_ERROR_NOT_IMPLEMENTED;
	}
	
	result = transport->ops->send_data(transport, data, size);
	if (result != EB_SUCCESS)
		transport->last_error = result;
	
	return result;
}

int transport_receive_data(eb_transport_t *transport, void *buffer, size_t size, 
                          size_t *received)
{
	int result;
	
	if (!transport || !buffer || !received)
		return EB_ERROR_INVALID_PARAMETER;
	
	if (!transport->connected) {
		result = transport_connect(transport);
		if (result != EB_SUCCESS)
			return result;
	}
	
	if (!transport->ops || !transport->ops->receive_data) {
		transport->last_error = EB_ERROR_NOT_IMPLEMENTED;
		snprintf(transport->error_msg, sizeof(transport->error_msg),
			 "Receive operation not implemented for this transport");
		return EB_ERROR_NOT_IMPLEMENTED;
	}
	
	result = transport->ops->receive_data(transport, buffer, size, received);
	if (result != EB_SUCCESS)
		transport->last_error = result;
	
	return result;
}

int transport_list_refs(eb_transport_t *transport, char ***refs, size_t *count)
{
	int result;
	
	if (!transport || !refs || !count)
		return EB_ERROR_INVALID_PARAMETER;
	
	if (!transport->connected) {
		result = transport_connect(transport);
		if (result != EB_SUCCESS)
			return result;
	}
	
	if (!transport->ops || !transport->ops->list_refs) {
		transport->last_error = EB_ERROR_NOT_IMPLEMENTED;
		snprintf(transport->error_msg, sizeof(transport->error_msg),
			 "List refs operation not implemented for this transport");
		return EB_ERROR_NOT_IMPLEMENTED;
	}
	
	result = transport->ops->list_refs(transport, refs, count);
	if (result != EB_SUCCESS)
		transport->last_error = result;
	
	return result;
}

const char *transport_get_error(eb_transport_t *transport)
{
	if (!transport)
		return "Invalid transport parameter";
	
	if (transport->error_msg[0] == '\0') {
		switch (transport->last_error) {
		case EB_SUCCESS:
			return "Success";
		case EB_ERROR_INVALID_PARAMETER:
			return "Invalid parameter";
		case EB_ERROR_NOT_IMPLEMENTED:
			return "Operation not implemented";
		case EB_ERROR_IO:
			return "I/O error";
		case EB_ERROR_MEMORY:
			return "Memory allocation failed";
		case EB_ERROR_NOT_INITIALIZED:
			return "Transport not initialized";
		case EB_ERROR_CONNECTION_FAILED:
			return "Connection failed";
		case EB_ERROR_UNSUPPORTED:
			return "Unsupported operation or protocol";
		default:
			return "Unknown error";
		}
	}
	
	return transport->error_msg;
} 