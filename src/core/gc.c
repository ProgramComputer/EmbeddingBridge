/*
 * EmbeddingBridge - Garbage Collection Implementation
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
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <limits.h>
#include <fcntl.h>

#include "gc.h"
#include "types.h"
#include "error.h"
#include "path_utils.h"
#include "debug.h"

/* Define PATH_MAX if not available */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Default grace period for unreferenced objects (2 weeks) */
#define DEFAULT_PRUNE_EXPIRE_SECONDS (14 * 24 * 60 * 60)

/* GC lock file name */
#define GC_LOCK_FILE "gc.lock"

/* Forward declarations for helper functions */
static int remove_unreferenced_embeddings(const char* objects_dir, time_t expire_time);
static bool is_referenced(const char* object_id);
static time_t parse_expire_time(const char* expire_str);

/**
 * Run garbage collection on the repository
 * 
 * @param prune_expire Expiration string for pruning (e.g., "2.weeks.ago", "now", or "never")
 * @param aggressive Whether to do more aggressive optimization
 * @param result Pointer to store operation result
 * @return Status code (0 = success)
 */
eb_status_t gc_run(const char* prune_expire, bool aggressive, eb_gc_result_t* result)
{
	/* Initialize result struct */
	if (result) {
		result->status = EB_SUCCESS;
		result->message[0] = '\0';
		result->objects_removed = 0;
		result->bytes_freed = 0;
	}

	/* Check if another GC is running */
	if (gc_is_running()) {
		if (result) {
			result->status = EB_ERROR_LOCK_FAILED;
			strcpy(result->message, "Another garbage collection process is running");
		}
		return EB_ERROR_LOCK_FAILED;
	}

	/* Get repository path */
	char* repo_path = get_repository_path();
	if (!repo_path) {
		if (result) {
			result->status = EB_ERROR_NOT_INITIALIZED;
			strcpy(result->message, "Repository not initialized");
		}
		return EB_ERROR_NOT_INITIALIZED;
	}

	/* Create GC lock file */
	char lock_path[PATH_MAX];
	int lock_fd;
	snprintf(lock_path, sizeof(lock_path), "%s/%s", repo_path, GC_LOCK_FILE);
	lock_fd = open(lock_path, O_CREAT | O_EXCL | O_WRONLY, 0644);
	if (lock_fd < 0) {
		if (result) {
			result->status = EB_ERROR_LOCK_FAILED;
			strcpy(result->message, "Failed to create GC lock file");
		}
		free(repo_path);
		return EB_ERROR_LOCK_FAILED;
	}
	
	/* Write PID to lock file */
	char pid_str[16];
	snprintf(pid_str, sizeof(pid_str), "%d\n", (int)getpid());
	write(lock_fd, pid_str, strlen(pid_str));
	close(lock_fd);

	/* Determine expiration time */
	time_t expire_time;
	if (!prune_expire) {
		/* Default: 2 weeks ago */
		expire_time = time(NULL) - DEFAULT_PRUNE_EXPIRE_SECONDS;
	} else if (strcmp(prune_expire, "now") == 0) {
		/* No grace period */
		expire_time = time(NULL);
	} else if (strcmp(prune_expire, "never") == 0) {
		/* Don't prune */
		if (result) {
			sprintf(result->message, "Pruning disabled, no objects removed");
		}
		unlink(lock_path); /* Remove lock file */
		free(repo_path);
		return EB_SUCCESS;
	} else {
		expire_time = parse_expire_time(prune_expire);
		if (expire_time == 0) {
			if (result) {
				result->status = EB_ERROR_INVALID_PARAMETER;
				sprintf(result->message, "Invalid expiration format: %s", prune_expire);
			}
			unlink(lock_path); /* Remove lock file */
			free(repo_path);
			return EB_ERROR_INVALID_PARAMETER;
		}
	}

	/* Get objects directory path */
	char objects_dir[PATH_MAX];
	snprintf(objects_dir, sizeof(objects_dir), "%s/objects", repo_path);

	/* Check if objects directory exists */
	struct stat st;
	if (stat(objects_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
		if (result) {
			result->status = EB_ERROR_NOT_INITIALIZED;
			sprintf(result->message, "Objects directory not found");
		}
		unlink(lock_path); /* Remove lock file */
		free(repo_path);
		return EB_ERROR_NOT_INITIALIZED;
	}

	/* Remove unreferenced objects */
	int removed = remove_unreferenced_embeddings(objects_dir, expire_time);
	
	if (result) {
		result->objects_removed = removed;
		sprintf(result->message, "Removed %d unreferenced embedding objects", removed);
	}

	/* Additional aggressive optimization if requested */
	if (aggressive) {
		/* TODO: Implement additional optimizations like:
		 * - Repack embeddings
		 * - Optimize indices
		 * - Compress data
		 */
		if (result) {
			strcat(result->message, " (aggressive mode)");
		}
	}

	/* Remove lock file */
	unlink(lock_path);
	free(repo_path);
	return EB_SUCCESS;
}

/**
 * Check if another garbage collection process is running
 * 
 * @return true if another gc is running, false otherwise
 */
bool gc_is_running(void)
{
	char* repo_path = get_repository_path();
	if (!repo_path)
		return false;
	
	char lock_path[PATH_MAX];
	snprintf(lock_path, sizeof(lock_path), "%s/%s", repo_path, GC_LOCK_FILE);
	
	/* Check if lock file exists */
	struct stat st;
	if (stat(lock_path, &st) == 0) {
		/* Lock file exists, read PID from it */
		FILE* lock_file = fopen(lock_path, "r");
		if (lock_file) {
			char pid_str[16];
			if (fgets(pid_str, sizeof(pid_str), lock_file)) {
				int pid = atoi(pid_str);
				
				/* Check if process with this PID exists */
				if (pid > 0) {
					char proc_path[32];
					snprintf(proc_path, sizeof(proc_path), "/proc/%d", pid);
					if (stat(proc_path, &st) == 0) {
						/* Process exists, gc is running */
						fclose(lock_file);
						free(repo_path);
						return true;
					}
				}
			}
			fclose(lock_file);
			
			/* PID doesn't exist, lock file is stale, remove it */
			unlink(lock_path);
		}
	}
	
	free(repo_path);
	return false;
}

/**
 * Find unreferenced embedding objects
 * 
 * @param unreferenced_out Array to store unreferenced object hashes
 * @param max_unreferenced Maximum number of objects to store
 * @param count_out Pointer to store actual count of unreferenced objects
 * @param expire_time Expiration time for objects
 * @return Status code (0 = success)
 */
eb_status_t gc_find_unreferenced(char** unreferenced_out, 
                               size_t max_unreferenced, 
                               size_t* count_out,
                               time_t expire_time)
{
	if (!unreferenced_out || !count_out)
		return EB_ERROR_INVALID_PARAMETER;
	
	/* Initialize count */
	*count_out = 0;
	
	/* Get repository path */
	char* repo_path = get_repository_path();
	if (!repo_path)
		return EB_ERROR_NOT_INITIALIZED;
	
	/* Get objects directory path */
	char objects_dir[PATH_MAX];
	snprintf(objects_dir, sizeof(objects_dir), "%s/objects", repo_path);
	
	/* Check if objects directory exists */
	struct stat st;
	if (stat(objects_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
		free(repo_path);
		return EB_ERROR_NOT_INITIALIZED;
	}
	
	/* Scan objects directory */
	DIR* dir = opendir(objects_dir);
	if (!dir) {
		free(repo_path);
		return EB_ERROR_IO;
	}
	
	struct dirent* entry;
	
	while ((entry = readdir(dir)) != NULL && *count_out < max_unreferenced) {
		if (entry->d_name[0] == '.')
			continue;
		
		char object_path[PATH_MAX];
		snprintf(object_path, sizeof(object_path), "%s/%s", objects_dir, entry->d_name);
		
		/* Check if it's a file */
		if (stat(object_path, &st) == 0 && S_ISREG(st.st_mode)) {
			/* Check if it's unreferenced */
			if (!is_referenced(entry->d_name)) {
				/* Check if it's older than expire_time */
				if (st.st_mtime < expire_time) {
					/* Add to unreferenced list */
					unreferenced_out[*count_out] = strdup(entry->d_name);
					(*count_out)++;
				}
			}
		}
	}
	
	closedir(dir);
	free(repo_path);
	return EB_SUCCESS;
}

/**
 * Remove a specific object from the repository
 * 
 * @param object_hash Hash of the object to remove
 * @param size_removed_out Pointer to store the size of removed object
 * @return Status code (0 = success)
 */
eb_status_t gc_remove_object(const char* object_hash, size_t* size_removed_out)
{
	if (!object_hash)
		return EB_ERROR_INVALID_PARAMETER;
	
	/* Initialize size if pointer provided */
	if (size_removed_out)
		*size_removed_out = 0;
	
	/* Get repository path */
	char* repo_path = get_repository_path();
	if (!repo_path)
		return EB_ERROR_NOT_INITIALIZED;
	
	/* Get object path */
	char object_path[PATH_MAX];
	snprintf(object_path, sizeof(object_path), "%s/objects/%s", repo_path, object_hash);
	
	/* Check if object exists */
	struct stat st;
	if (stat(object_path, &st) != 0) {
		free(repo_path);
		return EB_ERROR_NOT_FOUND;
	}
	
	/* Check if it's referenced */
	if (is_referenced(object_hash)) {
		free(repo_path);
		return EB_ERROR_REFERENCED;
	}
	
	/* Store size if requested */
	if (size_removed_out)
		*size_removed_out = st.st_size;
	
	/* Remove the file */
	if (unlink(object_path) != 0) {
		free(repo_path);
		return EB_ERROR_IO;
	}
	
	free(repo_path);
	return EB_SUCCESS;
}

/**
 * Parse an expiration time string (e.g., "2.weeks.ago") into a timestamp
 */
static time_t parse_expire_time(const char* expire_str)
{
	time_t now = time(NULL);
	time_t result = 0;
	
	char* str_copy = strdup(expire_str);
	if (!str_copy)
		return 0;
	
	/* Parse format like "2.weeks.ago" */
	char* token = strtok(str_copy, ".");
	if (!token) {
		free(str_copy);
		return 0;
	}
	
	/* Get the number value */
	char* endptr;
	long value = strtol(token, &endptr, 10);
	if (*endptr != '\0' || value < 0) {
		free(str_copy);
		return 0;
	}
	
	/* Get the unit */
	token = strtok(NULL, ".");
	if (!token) {
		free(str_copy);
		return 0;
	}
	
	/* Convert to seconds based on unit */
	long seconds = 0;
	if (strcmp(token, "seconds") == 0 || strcmp(token, "second") == 0) {
		seconds = value;
	} else if (strcmp(token, "minutes") == 0 || strcmp(token, "minute") == 0) {
		seconds = value * 60;
	} else if (strcmp(token, "hours") == 0 || strcmp(token, "hour") == 0) {
		seconds = value * 60 * 60;
	} else if (strcmp(token, "days") == 0 || strcmp(token, "day") == 0) {
		seconds = value * 24 * 60 * 60;
	} else if (strcmp(token, "weeks") == 0 || strcmp(token, "week") == 0) {
		seconds = value * 7 * 24 * 60 * 60;
	} else if (strcmp(token, "months") == 0 || strcmp(token, "month") == 0) {
		seconds = value * 30 * 24 * 60 * 60; /* Approximate */
	} else if (strcmp(token, "years") == 0 || strcmp(token, "year") == 0) {
		seconds = value * 365 * 24 * 60 * 60; /* Approximate */
	} else {
		free(str_copy);
		return 0;
	}
	
	/* Verify the "ago" part */
	token = strtok(NULL, ".");
	if (!token || strcmp(token, "ago") != 0) {
		free(str_copy);
		return 0;
	}
	
	result = now - seconds;
	free(str_copy);
	return result;
}

/**
 * Check if an object is referenced by any set
 */
static bool is_referenced(const char* object_id)
{
	/* Get repository path */
	char* repo_path = get_repository_path();
	if (!repo_path)
		return false;
	
	/* Get sets directory path */
	char sets_dir[PATH_MAX];
	snprintf(sets_dir, sizeof(sets_dir), "%s/sets", repo_path);
	
	/* Check if sets directory exists */
	struct stat st;
	if (stat(sets_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
		free(repo_path);
		return false;
	}
	
	/* Iterate through all sets */
	DIR* dir = opendir(sets_dir);
	if (!dir) {
		free(repo_path);
		return false;
	}
	
	/* For each set, check if it references the object */
	struct dirent* entry;
	bool referenced = false;
	
	while ((entry = readdir(dir)) != NULL && !referenced) {
		if (entry->d_name[0] == '.')
			continue;
		
		char set_dir[PATH_MAX];
		snprintf(set_dir, sizeof(set_dir), "%s/sets/%s", repo_path, entry->d_name);
		
		/* Check if it's a directory */
		if (stat(set_dir, &st) == 0 && S_ISDIR(st.st_mode)) {
			/* TODO: In a real implementation, check reference files within the set
			 * to determine if the object_id is referenced.
			 * 
			 * For now, we'll simulate by checking if there's a file with the object_id
			 * in the set directory
			 */
			
			char reference_path[PATH_MAX];
			snprintf(reference_path, sizeof(reference_path), "%s/refs/%s", set_dir, object_id);
			
			if (stat(reference_path, &st) == 0) {
				referenced = true;
			}
		}
	}
	
	closedir(dir);
	free(repo_path);
	
	return referenced;
}

/**
 * Remove unreferenced embedding objects older than the expire time
 * 
 * @param objects_dir Path to the objects directory
 * @param expire_time Timestamp before which unreferenced objects will be removed
 * @return Number of objects removed
 */
static int remove_unreferenced_embeddings(const char* objects_dir, time_t expire_time)
{
	int removed_count = 0;
	size_t bytes_freed = 0;
	
	/* In a real implementation, this would scan the objects directory for
	 * embedding objects, check if they are referenced by any set, and
	 * remove them if they are unreferenced and older than expire_time.
	 * 
	 * For this simulation, we'll create a mock implementation that:
	 * 1. Looks for files in the "objects" directory
	 * 2. Checks if they're referenced
	 * 3. Checks their modification time
	 * 4. Removes them if unreferenced and old enough
	 */
	
	DIR* dir = opendir(objects_dir);
	if (!dir)
		return 0;
	
	struct dirent* entry;
	struct stat st;
	
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.')
			continue;
		
		char object_path[PATH_MAX];
		snprintf(object_path, sizeof(object_path), "%s/%s", objects_dir, entry->d_name);
		
		/* Check if it's a file */
		if (stat(object_path, &st) == 0 && S_ISREG(st.st_mode)) {
			/* Check if it's unreferenced */
			if (!is_referenced(entry->d_name)) {
				/* Check if it's older than expire_time */
				if (st.st_mtime < expire_time) {
					/* Remove the file */
					bytes_freed += st.st_size;
					if (unlink(object_path) == 0)
						removed_count++;
				}
			}
		}
	}
	
	closedir(dir);
	return removed_count;
} 