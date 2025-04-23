/*
 * EmbeddingBridge - Transport Layer Interface
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef EB_TRANSPORT_H
#define EB_TRANSPORT_H

#include "types.h"
#include <stdbool.h>

/**
 * Transport types supported by EmbeddingBridge
 */
enum transport_type {
	TRANSPORT_UNKNOWN = 0,
	TRANSPORT_LOCAL,   /* Local filesystem access */
	TRANSPORT_SSH,     /* SSH protocol */
	TRANSPORT_HTTP,    /* HTTP(S) protocol */
	TRANSPORT_S3
};

/* Forward declaration of transport structure */
typedef struct eb_transport eb_transport_t;

/**
 * Transport operation function types
 */
typedef int (*transport_connect_fn)(eb_transport_t *transport);
typedef int (*transport_disconnect_fn)(eb_transport_t *transport);
typedef int (*transport_send_fn)(eb_transport_t *transport, const void *data, size_t size, const char *hash);
typedef int (*transport_receive_fn)(eb_transport_t *transport, void *buffer, size_t size, size_t *received);
typedef int (*transport_list_fn)(eb_transport_t *transport, char ***refs, size_t *count);
typedef int (*transport_delete_fn)(eb_transport_t *transport, const char **refs, size_t count);

/**
 * Transport operations structure
 */
struct transport_ops {
	transport_connect_fn connect;
	transport_disconnect_fn disconnect;
	transport_send_fn send_data;
	transport_receive_fn receive_data;
	transport_list_fn list_refs;
	transport_delete_fn delete_refs;
};

/**
 * Transport structure
 */
struct eb_transport {
	const char *url;              /* Remote URL */
	enum transport_type type;     /* Transport type */
	struct transport_ops *ops;    /* Transport operations */
	void *data;                   /* Protocol-specific data */
	bool connected;               /* Connection state */
	eb_status_t last_error;       /* Last error code */
	char error_msg[256];          /* Last error message */
	const char *target_path;      /* Target path for operations */
	bool data_is_precompressed;  /* Flag to indicate if data is already compressed */
};

/**
 * Create and initialize a transport based on URL
 *
 * @param url URL to the remote repository
 * @return Initialized transport or NULL on error
 */
eb_transport_t *transport_open(const char *url);

/**
 * Close and free a transport
 *
 * @param transport Transport to close
 */
void transport_close(eb_transport_t *transport);

/**
 * Connect to the remote repository
 *
 * @param transport Transport to use
 * @return Status code (0 = success)
 */
int transport_connect(eb_transport_t *transport);

/**
 * Disconnect from the remote repository
 *
 * @param transport Transport to use
 * @return Status code (0 = success)
 */
int transport_disconnect(eb_transport_t *transport);

/**
 * Send data to the remote repository
 *
 * @param transport Transport to use
 * @param data Data to send
 * @param size Size of data
 * @param hash Hash of the data
 * @return Status code (0 = success)
 */
int transport_send_data(eb_transport_t *transport, const void *data, size_t size, const char *hash);

/**
 * Receive data from the remote repository
 *
 * @param transport Transport to use
 * @param buffer Buffer to store received data
 * @param size Size of buffer
 * @param received Actual number of bytes received
 * @return Status code (0 = success)
 */
int transport_receive_data(eb_transport_t *transport, void *buffer, size_t size, size_t *received);

/**
 * List references in the remote repository
 *
 * @param transport Transport to use
 * @param refs Pointer to store null-terminated array of reference names
 * @param count Pointer to store number of references
 * @return Status code (0 = success)
 */
int transport_list_refs(eb_transport_t *transport, char ***refs, size_t *count);

/**
 * Get the last error message from transport
 *
 * @param transport Transport instance
 * @return Error message string
 */
const char *transport_get_error(eb_transport_t *transport);

/**
 * Delete references/objects in the remote repository
 *
 * @param transport Transport to use
 * @param refs Array of reference/object names to delete
 * @param count Number of references/objects
 * @return Status code (0 = success)
 */
int transport_delete_refs(eb_transport_t *transport, const char **refs, size_t count);

/* Protocol-specific initialization functions */
int ssh_transport_init(void);
int http_transport_init(void);
int local_transport_init(void);

#endif /* EB_TRANSPORT_H */ 