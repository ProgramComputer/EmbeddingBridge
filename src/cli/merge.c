/*
 * EmbeddingBridge - Set Merge Implementation
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
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>
#include <ctype.h>
#include "merge.h"
#include "set.h"
#include "cli.h"
#include "../core/path_utils.h"
#include "../core/error.h"
#include "../core/debug.h"

/* Command usage strings */
static const char* MERGE_USAGE = 
    "Usage: embr merge <source-set> [<target-set>] [options]\n"
    "\n"
    "Merge embeddings from source set into target set.\n"
    "If target set is not specified, merges into the current set.\n"
    "\n"
    "Options:\n"
    "  --strategy=<strategy>    Merge strategy to use (union, mean, max, weighted)\n"
    "\n"
    "Strategies:\n"
    "  union      Default. Keep all embeddings, prioritize target for conflicts\n"
    "  mean       For conflicts, compute element-wise mean of embeddings\n"
    "  max        For conflicts, take element-wise maximum of embeddings\n"
    "  weighted   For conflicts, apply weighted combination based on metadata\n"
    "\n"
    "Examples:\n"
    "  embr merge feature-set                  # Merge feature-set into current set\n"
    "  embr merge feature-set main             # Merge feature-set into main set\n"
    "  embr merge feature-set --strategy=mean  # Use mean strategy for conflicts\n";

/* Main entry point for the merge command */
int cmd_merge(int argc, char** argv)
{
	/* Help option */
	if (argc < 2 || strcmp(argv[1], "--help") == 0 || 
	    strcmp(argv[1], "-h") == 0) {
		printf("%s", MERGE_USAGE);
		return 0;
	}

	/* Delegate to the handler */
	return handle_merge(argc, argv);
}

/* Helper function to get path to sets directory */
static char* get_set_dir_path(void)
{
	char* eb_root = find_repo_root(".");
	if (!eb_root)
		return NULL;
	
	char* set_dir = malloc(strlen(eb_root) + strlen(".embr/sets") + 2);
	if (!set_dir) {
		free(eb_root);
		return NULL;
	}
	
	sprintf(set_dir, "%s/%s", eb_root, ".embr/sets");
	free(eb_root);
	return set_dir;
}

/* Convert string to merge strategy */
bool eb_parse_merge_strategy(const char* strategy_name, eb_merge_strategy_t* strategy_out)
{
	if (!strategy_name || !strategy_out)
		return false;
	
	if (strcasecmp(strategy_name, "union") == 0)
		*strategy_out = EB_MERGE_UNION;
	else if (strcasecmp(strategy_name, "mean") == 0)
		*strategy_out = EB_MERGE_MEAN;
	else if (strcasecmp(strategy_name, "max") == 0)
		*strategy_out = EB_MERGE_MAX;
	else if (strcasecmp(strategy_name, "weighted") == 0)
		*strategy_out = EB_MERGE_WEIGHTED;
	else
		return false;
	
	return true;
}

/* Get string name for merge strategy */
const char* eb_merge_strategy_name(eb_merge_strategy_t strategy)
{
	switch (strategy) {
		case EB_MERGE_UNION:
			return "union";
		case EB_MERGE_MEAN:
			return "mean";
		case EB_MERGE_MAX:
			return "max";
		case EB_MERGE_WEIGHTED:
			return "weighted";
		default:
			return "unknown";
	}
}

/* Load embedding references from a set */
eb_status_t eb_load_embedding_refs(const char* set_path, eb_embedding_ref_t** refs_out)
{
	if (!set_path || !refs_out)
		return EB_ERROR_INVALID_PARAMETER;
	
	*refs_out = NULL;
	
	/* Path to refs directory */
	char refs_path[1024];
	snprintf(refs_path, sizeof(refs_path), "%s/refs", set_path);
	
	/* Open refs directory */
	DIR* refs_dir = opendir(refs_path);
	if (!refs_dir)
		return EB_ERROR_NOT_FOUND;
	
	/* Read all files in refs directory */
	struct dirent* entry;
	eb_embedding_ref_t* head = NULL;
	
	while ((entry = readdir(refs_dir)) != NULL) {
		/* Skip . and .. */
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;
		
		/* Create full path to reference file */
		char ref_path[1024];
		snprintf(ref_path, sizeof(ref_path), "%s/%s", refs_path, entry->d_name);
		
		/* Read reference file */
		FILE* ref_file = fopen(ref_path, "r");
		if (!ref_file)
			continue;
		
		/* Read hash reference from file */
		char hash_ref[100] = {0};
		if (fgets(hash_ref, sizeof(hash_ref), ref_file)) {
			/* Remove newline */
			size_t len = strlen(hash_ref);
			if (len > 0 && hash_ref[len - 1] == '\n')
				hash_ref[len - 1] = '\0';
			
			/* Allocate and populate reference struct */
			eb_embedding_ref_t* ref = malloc(sizeof(eb_embedding_ref_t));
			if (ref) {
				ref->source_file = strdup(entry->d_name); 
				ref->hash_ref = strdup(hash_ref);
				ref->metadata = NULL;  /* We'll add metadata later if needed */
				
				/* Add to linked list */
				ref->next = head;
				head = ref;
			}
		}
		
		fclose(ref_file);
	}
	
	closedir(refs_dir);
	*refs_out = head;
	return EB_SUCCESS;
}

/* Free embedding references linked list */
void eb_free_embedding_refs(eb_embedding_ref_t* refs)
{
	while (refs) {
		eb_embedding_ref_t* next = refs->next;
		free(refs->source_file);
		free(refs->hash_ref);
		free(refs->metadata);
		free(refs);
		refs = next;
	}
}

/* Main merge function implementation */
eb_status_t eb_merge_sets(
	const char* source_set,
	const char* target_set,
	eb_merge_strategy_t strategy,
	eb_merge_result_t* result
)
{
	/* Initialize result if provided */
	if (result) {
		memset(result, 0, sizeof(eb_merge_result_t));
	}
	
	/* Validate input */
	if (!source_set || !*source_set)
		return EB_ERROR_INVALID_PARAMETER;
	
	/* Get sets directory */
	char* set_dir = get_set_dir_path();
	if (!set_dir)
		return EB_ERROR_NOT_INITIALIZED;
	
	/* If target_set is NULL, use current set */
	char current_set[100] = {0};
	if (!target_set || !*target_set) {
		eb_status_t status = get_current_set(current_set, sizeof(current_set));
		if (status != EB_SUCCESS) {
			free(set_dir);
			return status;
		}
		target_set = current_set;
	}
	
	/* Prevent merging a set with itself */
	if (strcmp(source_set, target_set) == 0) {
		free(set_dir);
		return EB_ERROR_INVALID_PARAMETER;
	}
	
	/* Construct paths to source and target sets */
	char* source_path = malloc(strlen(set_dir) + strlen(source_set) + 2);
	if (!source_path) {
		free(set_dir);
		return EB_ERROR_MEMORY;
	}
	
	sprintf(source_path, "%s/%s", set_dir, source_set);
	
	char* target_path = malloc(strlen(set_dir) + strlen(target_set) + 2);
	if (!target_path) {
		free(set_dir);
		free(source_path);
		return EB_ERROR_MEMORY;
	}
	
	sprintf(target_path, "%s/%s", set_dir, target_set);
	
	/* Verify sets exist */
	struct stat st;
	if (stat(source_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
		free(set_dir);
		free(source_path);
		free(target_path);
		return EB_ERROR_NOT_FOUND;
	}
	
	if (stat(target_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
		free(set_dir);
		free(source_path);
		free(target_path);
		return EB_ERROR_NOT_FOUND;
	}
	
	/* Ensure refs directories exist */
	char source_refs_path[1024];
	char target_refs_path[1024];
	snprintf(source_refs_path, sizeof(source_refs_path), "%s/refs", source_path);
	snprintf(target_refs_path, sizeof(target_refs_path), "%s/refs", target_path);
	
	/* Create target refs directory if it doesn't exist */
	if (stat(target_refs_path, &st) != 0) {
		if (mkdir(target_refs_path, 0755) != 0) {
			free(set_dir);
			free(source_path);
			free(target_path);
			return EB_ERROR_FILE_IO;
		}
	}
	
	/* Load embedding references from source and target sets */
	eb_embedding_ref_t* source_refs = NULL;
	eb_embedding_ref_t* target_refs = NULL;
	
	eb_status_t status = eb_load_embedding_refs(source_path, &source_refs);
	if (status != EB_SUCCESS) {
		free(set_dir);
		free(source_path);
		free(target_path);
		return status;
	}
	
	status = eb_load_embedding_refs(target_path, &target_refs);
	if (status != EB_SUCCESS && status != EB_ERROR_NOT_FOUND) {
		eb_free_embedding_refs(source_refs);
		free(set_dir);
		free(source_path);
		free(target_path);
		return status;
	}
	
	/* Perform the merge operation */
	int merged_count = 0;
	int new_count = 0;
	int conflict_count = 0;
	int error_count = 0;
	
	/* For each source ref, see if it exists in target */
	eb_embedding_ref_t* src_ref = source_refs;
	while (src_ref) {
		bool exists = false;
		eb_embedding_ref_t* tgt_ref = target_refs;
		
		/* Check if source file already has an embedding in target */
		while (tgt_ref && !exists) {
			if (strcmp(src_ref->source_file, tgt_ref->source_file) == 0) {
				exists = true;
				
				/* If hash references are different, apply merge strategy */
				if (strcmp(src_ref->hash_ref, tgt_ref->hash_ref) != 0) {
					conflict_count++;
					
					switch (strategy) {
						case EB_MERGE_UNION:
							/* For union, we keep target's version by default */
							printf("Keeping target version for %s\n", src_ref->source_file);
							break;
							
						case EB_MERGE_MEAN:
						case EB_MERGE_MAX:
						case EB_MERGE_WEIGHTED:
							/* For other strategies, we need to compute a new embedding */
							printf("Computing merged embedding for %s\n", src_ref->source_file);
							
							/* 
							 * TODO: Implement actual merging of embeddings based on strategy
							 * This would involve:
							 * 1. Loading the actual embedding objects using hash references
							 * 2. Performing the mathematical operation (mean/max/weighted)
							 * 3. Storing the new embedding
							 * 4. Creating a reference to the new embedding
							 */
							
							/* For now, just use target's version as a stub implementation */
							printf("Note: Full merging logic not yet implemented. Keeping target version.\n");
							merged_count++;
							break;
					}
				}
			}
			tgt_ref = tgt_ref->next;
		}
		
		/* If source file doesn't exist in target, copy it over */
		if (!exists) {
			printf("Adding new embedding for %s\n", src_ref->source_file);
			
			/* Create a new reference file in target */
			char ref_path[1024];
			snprintf(ref_path, sizeof(ref_path), "%s/%s", 
				target_refs_path, src_ref->source_file);
			
			FILE* ref_file = fopen(ref_path, "w");
			if (ref_file) {
				/* Write hash reference to file */
				fprintf(ref_file, "%s\n", src_ref->hash_ref);
				fclose(ref_file);
				new_count++;
			} else {
				printf("Error: Failed to create reference file for %s\n", src_ref->source_file);
				error_count++;
			}
		}
		
		src_ref = src_ref->next;
	}
	
	/* Update result statistics if provided */
	if (result) {
		result->new_count = new_count;
		result->updated_count = merged_count;
		result->conflict_count = conflict_count;
		result->error_count = error_count;
	}
	
	/* Clean up resources */
	eb_free_embedding_refs(source_refs);
	eb_free_embedding_refs(target_refs);
	free(set_dir);
	free(source_path);
	free(target_path);
	
	return EB_SUCCESS;
}

/* CLI handler for merge command */
int handle_merge(int argc, char** argv)
{
	/* Parse arguments */
	const char* source_set = NULL;
	const char* target_set = NULL;
	const char* strategy = NULL;
	
	for (int i = 0; i < argc; i++) {
		if (strncmp(argv[i], "--strategy=", 11) == 0) {
			strategy = argv[i] + 11;
		} else if (strcmp(argv[i], "--strategy") == 0 && i + 1 < argc) {
			strategy = argv[++i];
		} else if (!source_set) {
			source_set = argv[i];
		} else if (!target_set) {
			target_set = argv[i];
		}
	}
	
	if (!source_set) {
		fprintf(stderr, "Error: No source set specified for merge\n");
		return 1;
	}
	
	/* Parse the merge strategy */
	eb_merge_strategy_t merge_strategy = EB_MERGE_UNION; /* Default strategy */
	
	if (strategy) {
		if (!eb_parse_merge_strategy(strategy, &merge_strategy)) {
			printf("Unknown merge strategy '%s', using 'union' instead\n", strategy);
		}
	}
	
	/* Perform the merge */
	eb_merge_result_t result = {0};
	eb_status_t status = eb_merge_sets(source_set, target_set, merge_strategy, &result);
	
	if (status == EB_SUCCESS) {
		printf("Merge complete:\n");
		printf("  %d new embeddings added\n", result.new_count);
		printf("  %d existing embeddings merged\n", result.updated_count);
		
		if (result.conflict_count > 0) {
			printf("  %d conflicts encountered\n", result.conflict_count);
		}
		
		if (result.error_count > 0) {
			printf("  %d errors occurred during merge\n", result.error_count);
		}
	} else {
		handle_error(status, "Failed to merge sets");
		return 1;
	}
	
	return 0;
} 