/*
 * EmbeddingBridge - Local Transport Implementation
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
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "transport.h"
#include "error.h"
#include "debug.h"

// Function declaration
static int mkdir_p(const char *path);

/* Local transport specific data */
struct local_data {
	char *path;          /* Local repository path */
	int is_connected;    /* Connection state */
	FILE *current_file;  /* Current file for read/write operations */
	char *current_name;  /* Current filename */
};

/* Helper to parse local URL */
static char *parse_local_url(const char *url)
{
	char *path = NULL;
	
	/* Check for file:// prefix */
	if (strncmp(url, "file://", 7) == 0) {
		path = strdup(url + 7);
	} else {
		/* Assume it's already a path */
		path = strdup(url);
	}
	
	/* Normalize path by removing trailing slashes */
	if (path) {
		size_t len = strlen(path);
		while (len > 0 && path[len - 1] == '/') {
			path[--len] = '\0';
		}
	}
	
	return path;
}

/* Connect to local repository */
static int local_connect(eb_transport_t *transport)
{
	struct local_data *local;
	struct stat st;
	
	if (!transport)
		return EB_ERROR_INVALID_PARAMETER;
	
	/* Create local data */
	local = calloc(1, sizeof(*local));
	if (!local) {
		snprintf(transport->error_msg, sizeof(transport->error_msg),
			 "Failed to allocate memory for local transport");
		return EB_ERROR_MEMORY;
	}
	
	/* Parse URL to get local path */
	local->path = parse_local_url(transport->url);
	if (!local->path) {
		free(local);
		snprintf(transport->error_msg, sizeof(transport->error_msg),
			 "Failed to parse local URL: %s", transport->url);
		return EB_ERROR_INVALID_PARAMETER;
	}
	
	/* Check if path exists and is a directory */
	if (stat(local->path, &st) < 0) {
		snprintf(transport->error_msg, sizeof(transport->error_msg),
			 "Failed to access repository at %s: %s", 
			 local->path, strerror(errno));
		free(local->path);
		free(local);
		return EB_ERROR_NOT_FOUND;
	}
	
	if (!S_ISDIR(st.st_mode)) {
		snprintf(transport->error_msg, sizeof(transport->error_msg),
			 "Path is not a directory: %s", local->path);
		free(local->path);
		free(local);
		return EB_ERROR_INVALID_PARAMETER;
	}
	
	/* Check if it's a valid EB repository */
	char *eb_dir = malloc(strlen(local->path) + 5);
	if (!eb_dir) {
		free(local->path);
		free(local);
		snprintf(transport->error_msg, sizeof(transport->error_msg),
			 "Failed to allocate memory for path");
		return EB_ERROR_MEMORY;
	}
	
	sprintf(eb_dir, "%s/.embr", local->path);
	
	if (stat(eb_dir, &st) < 0 || !S_ISDIR(st.st_mode)) {
		free(eb_dir);
		free(local->path);
		free(local);
		snprintf(transport->error_msg, sizeof(transport->error_msg),
			 "Not a valid EB repository: %s", local->path);
		return EB_ERROR_INVALID_REPOSITORY;
	}
	
	free(eb_dir);
	
	/* Store local data in transport */
	transport->data = local;
	local->is_connected = 1;
	
	DEBUG_PRINT("Local transport connected to %s", local->path);
	return EB_SUCCESS;
}

/* Disconnect from local repository */
static int local_disconnect(eb_transport_t *transport)
{
	struct local_data *local;
	
	if (!transport)
		return EB_ERROR_INVALID_PARAMETER;
	
	local = (struct local_data *)transport->data;
	if (!local)
		return EB_SUCCESS;
	
	/* Close current file if open */
	if (local->current_file) {
		fclose(local->current_file);
		local->current_file = NULL;
	}
	
	/* Free resources */
	free(local->path);
	free(local->current_name);
	free(local);
	transport->data = NULL;
	
	return EB_SUCCESS;
}

/* Send data to local repository */
static int local_send_data(eb_transport_t *transport, const void *data, size_t size)
{
	struct local_data *local;
	char tmp_path[PATH_MAX];
	char *objects_dir;
	size_t bytes_written;
	
	if (!transport || !data)
		return EB_ERROR_INVALID_PARAMETER;
	
	local = (struct local_data *)transport->data;
	if (!local || !local->is_connected)
		return EB_ERROR_NOT_CONNECTED;
	
	/* Create temporary file for writing */
	snprintf(tmp_path, sizeof(tmp_path), "%s/.embr/tmp/send_XXXXXX", local->path);
	int fd = mkstemp(tmp_path);
	if (fd < 0) {
		/* Check if tmp directory exists and create it if needed */
		char tmp_dir[PATH_MAX];
		snprintf(tmp_dir, sizeof(tmp_dir), "%s/.embr/tmp", local->path);
		
		if (mkdir_p(tmp_dir) != 0) {
			snprintf(transport->error_msg, sizeof(transport->error_msg),
				 "Failed to create temporary directory: %s", strerror(errno));
			return EB_ERROR_IO;
		}
		
		/* Try again */
		fd = mkstemp(tmp_path);
		if (fd < 0) {
			snprintf(transport->error_msg, sizeof(transport->error_msg),
				 "Failed to create temporary file: %s", strerror(errno));
			return EB_ERROR_IO;
		}
	}
	
	/* Write data to temporary file */
	bytes_written = write(fd, data, size);
	if (bytes_written < size) {
		close(fd);
		unlink(tmp_path);
		snprintf(transport->error_msg, sizeof(transport->error_msg),
			 "Failed to write data: %s", strerror(errno));
		return EB_ERROR_IO;
	}
	
	close(fd);
	
	/* Determine target path based on data content */
	/* In a real implementation, this would calculate hash and organize into objects dir */
	/* For now, we just move to objects directory with a timestamp-based name */
	objects_dir = malloc(strlen(local->path) + 32);
	if (!objects_dir) {
		unlink(tmp_path);
		snprintf(transport->error_msg, sizeof(transport->error_msg),
			 "Failed to allocate memory for path");
		return EB_ERROR_MEMORY;
	}
	
	sprintf(objects_dir, "%s/.embr/objects", local->path);
	
	/* Create objects directory if it doesn't exist */
	if (mkdir_p(objects_dir) != 0) {
		free(objects_dir);
		unlink(tmp_path);
		snprintf(transport->error_msg, sizeof(transport->error_msg),
			 "Failed to create objects directory: %s", strerror(errno));
		return EB_ERROR_IO;
	}
	
	/* Generate target filename */
	char target_path[PATH_MAX];
	time_t now = time(NULL);
	snprintf(target_path, sizeof(target_path), "%s/%ld", objects_dir, now);
	free(objects_dir);
	
	/* Move temporary file to target path */
	if (rename(tmp_path, target_path) < 0) {
		unlink(tmp_path);
		snprintf(transport->error_msg, sizeof(transport->error_msg),
			 "Failed to rename temporary file: %s", strerror(errno));
		return EB_ERROR_IO;
	}
	
	return EB_SUCCESS;
}

/* Receive data from local repository */
static int local_receive_data(eb_transport_t *transport, void *buffer, 
                             size_t size, size_t *received)
{
	struct local_data *local;
	size_t bytes_read;
	
	if (!transport || !buffer || !received)
		return EB_ERROR_INVALID_PARAMETER;
	
	local = (struct local_data *)transport->data;
	if (!local || !local->is_connected)
		return EB_ERROR_NOT_CONNECTED;
	
	*received = 0;
	
	/* If no file is open, open the first object file */
	if (!local->current_file) {
		char objects_dir[PATH_MAX];
		DIR *dir;
		struct dirent *entry;
		struct stat st;
		char *first_file = NULL;
		time_t oldest_time = 0;
		
		snprintf(objects_dir, sizeof(objects_dir), "%s/.embr/objects", local->path);
		
		dir = opendir(objects_dir);
		if (!dir) {
			snprintf(transport->error_msg, sizeof(transport->error_msg),
				 "Failed to open objects directory: %s", strerror(errno));
			return EB_ERROR_IO;
		}
		
		/* Find the oldest object file */
		while ((entry = readdir(dir)) != NULL) {
			if (entry->d_name[0] == '.')
				continue;
			
			char file_path[PATH_MAX];
			snprintf(file_path, sizeof(file_path), "%s/%s", objects_dir, entry->d_name);
			
			if (stat(file_path, &st) < 0)
				continue;
			
			if (S_ISREG(st.st_mode)) {
				if (!first_file || st.st_mtime < oldest_time) {
					free(first_file);
					first_file = strdup(file_path);
					oldest_time = st.st_mtime;
				}
			}
		}
		
		closedir(dir);
		
		if (!first_file) {
			/* No object files found */
			return EB_SUCCESS;
		}
		
		/* Open the file */
		local->current_file = fopen(first_file, "rb");
		if (!local->current_file) {
			snprintf(transport->error_msg, sizeof(transport->error_msg),
				 "Failed to open object file: %s", strerror(errno));
			free(first_file);
			return EB_ERROR_IO;
		}
		
		local->current_name = first_file;
	}
	
	/* Read data from current file */
	bytes_read = fread(buffer, 1, size, local->current_file);
	*received = bytes_read;
	
	if (bytes_read < size) {
		/* End of file reached */
		if (ferror(local->current_file)) {
			snprintf(transport->error_msg, sizeof(transport->error_msg),
				 "Failed to read from file: %s", strerror(errno));
			fclose(local->current_file);
			free(local->current_name);
			local->current_file = NULL;
			local->current_name = NULL;
			return EB_ERROR_IO;
		}
		
		/* Close current file */
		fclose(local->current_file);
		free(local->current_name);
		local->current_file = NULL;
		local->current_name = NULL;
	}
	
	return EB_SUCCESS;
}

/* List references in local repository */
static int local_list_refs(eb_transport_t *transport, char ***refs, size_t *count)
{
	struct local_data *local;
	DIR *dir;
	struct dirent *entry;
	struct stat st;
	char **ref_list = NULL;
	size_t ref_count = 0;
	size_t ref_capacity = 0;
	char refs_dir[PATH_MAX];
	
	if (!transport || !refs || !count)
		return EB_ERROR_INVALID_PARAMETER;
	
	local = (struct local_data *)transport->data;
	if (!local || !local->is_connected)
		return EB_ERROR_NOT_CONNECTED;
	
	/* Initialize output parameters */
	*refs = NULL;
	*count = 0;
	
	/* Open refs directory */
	snprintf(refs_dir, sizeof(refs_dir), "%s/.embr/refs", local->path);
	
	dir = opendir(refs_dir);
	if (!dir) {
		snprintf(transport->error_msg, sizeof(transport->error_msg),
			 "Failed to open refs directory: %s", strerror(errno));
		return EB_ERROR_IO;
	}
	
	/* Allocate initial ref list */
	ref_capacity = 16;
	ref_list = malloc(ref_capacity * sizeof(char *));
	if (!ref_list) {
		closedir(dir);
		snprintf(transport->error_msg, sizeof(transport->error_msg),
			 "Failed to allocate memory for reference list");
		return EB_ERROR_MEMORY;
	}
	
	/* Read refs directory */
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.')
			continue;
		
		char ref_path[PATH_MAX];
		snprintf(ref_path, sizeof(ref_path), "%s/%s", refs_dir, entry->d_name);
		
		if (stat(ref_path, &st) < 0)
			continue;
		
		if (S_ISREG(st.st_mode)) {
			/* Check if we need to resize the ref list */
			if (ref_count >= ref_capacity) {
				ref_capacity *= 2;
				char **new_refs = realloc(ref_list, ref_capacity * sizeof(char *));
				if (!new_refs) {
					/* Free existing refs */
					for (size_t i = 0; i < ref_count; i++)
						free(ref_list[i]);
					free(ref_list);
					closedir(dir);
					
					snprintf(transport->error_msg, sizeof(transport->error_msg),
						 "Failed to resize reference list");
					return EB_ERROR_MEMORY;
				}
				ref_list = new_refs;
			}
			
			/* Open and read reference file */
			FILE *ref_file = fopen(ref_path, "r");
			if (ref_file) {
				char ref_content[41]; /* SHA-1 hash is 40 chars + null terminator */
				if (fgets(ref_content, sizeof(ref_content), ref_file)) {
					/* Remove trailing newline if present */
					size_t len = strlen(ref_content);
					if (len > 0 && ref_content[len - 1] == '\n')
						ref_content[len - 1] = '\0';
					
					/* Format ref name and content */
					char *ref_entry = malloc(strlen(entry->d_name) + strlen(ref_content) + 10);
					if (ref_entry) {
						sprintf(ref_entry, "%s %s", ref_content, entry->d_name);
						ref_list[ref_count++] = ref_entry;
					}
				}
				fclose(ref_file);
			}
		} else if (S_ISDIR(st.st_mode)) {
			/* Handle nested refs (e.g., refs/heads) in a real implementation */
			/* For simplicity, we skip nested refs in this example */
		}
	}
	
	closedir(dir);
	
	/* Set output parameters */
	*refs = ref_list;
	*count = ref_count;
	
	return EB_SUCCESS;
}

/* Export local transport operations */
struct transport_ops local_ops = {
	.connect = local_connect,
	.disconnect = local_disconnect,
	.send_data = local_send_data,
	.receive_data = local_receive_data,
	.list_refs = local_list_refs
};

static int mkdir_p(const char *path) {
    char tmp[PATH_MAX];
    char *p = NULL;
    size_t len;
    int ret;

    if (!path || !*path) return -1;
    
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') tmp[len - 1] = 0;
    
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            ret = mkdir(tmp, 0755);
            if (ret != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    ret = mkdir(tmp, 0755);
    return (ret != 0 && errno != EEXIST) ? -1 : 0;
} 