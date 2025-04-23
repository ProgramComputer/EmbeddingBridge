/*
 * EmbeddingBridge - Remote Operations
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef EB_REMOTE_H
#define EB_REMOTE_H

#include <stdbool.h>
#include <stddef.h>
#include "status.h"

/**
 * Initialize the remote subsystem
 *
 * @return Status code
 */
eb_status_t eb_remote_init(void);

/**
 * Shut down the remote subsystem
 */
void eb_remote_shutdown(void);

/**
 * Clean up resources used by the remote registry
 */
void eb_remote_cleanup(void);

/**
 * Add a remote
 *
 * @param name Remote name
 * @param url Remote URL
 * @param auth_token Authentication token (can be NULL)
 * @param compression_level Compression level (0-9, 0 = no compression)
 * @param verify_ssl Whether to verify SSL certificates
 * @param format Data format (e.g., "json", can be NULL for default)
 * @return Status code
 */
eb_status_t eb_remote_add(
    const char *name,
    const char *url,
    const char *auth_token,
    int compression_level,
    bool verify_ssl,
    const char *format);

/**
 * Remove a remote
 *
 * @param name Remote name
 * @return Status code
 */
eb_status_t eb_remote_remove(const char *name);

/**
 * Get information about a remote
 *
 * @param name Remote name
 * @param url Buffer to store the URL (can be NULL)
 * @param url_size Size of the URL buffer
 * @param timeout Pointer to store the timeout (can be NULL)
 * @param verify_ssl Pointer to store whether to verify SSL certificates (can be NULL)
 * @param transformer Buffer to store the transformer name (can be NULL)
 * @param transformer_size Size of the transformer buffer
 * @return Status code
 */
eb_status_t eb_remote_info(
    const char *name, 
    char *url, 
    size_t url_size,
    int *timeout, 
    bool *verify_ssl, 
    char *transformer, 
    size_t transformer_size);

/**
 * List all remotes
 *
 * @param names Pointer to store the array of remote names (caller must free each string and the array)
 * @param count Pointer to store the number of remotes
 * @return Status code
 */
eb_status_t eb_remote_list(char ***names, int *count);

/**
 * Push data to a remote
 *
 * @param remote_name Remote name
 * @param data Data to push
 * @param size Size of data
 * @param path Path on the remote
 * @param hash Hash of the data (embedding)
 * @return Status code
 */
eb_status_t eb_remote_push(
    const char *remote_name,
    const void *data,
    size_t size,
    const char *path,
    const char *hash);

/**
 * Pull data from a remote
 *
 * @param remote_name Remote name
 * @param path Path on the remote
 * @param data_out Pointer to store the data (caller must free)
 * @param size_out Pointer to store the size of the data
 * @return Status code
 */
eb_status_t eb_remote_pull(
    const char *remote_name,
    const char *path,
    void **data_out,
    size_t *size_out);

/**
 * Pull data from a remote with delta update option
 *
 * @param remote_name Remote name
 * @param path Path on the remote
 * @param data_out Pointer to store the data (caller must free)
 * @param size_out Pointer to store the size of the data
 * @param delta_update Whether to only pull changed data
 * @return Status code
 */
eb_status_t eb_remote_pull_delta(
    const char *remote_name,
    const char *path,
    void **data_out,
    size_t *size_out,
    bool delta_update);

/**
 * Load remote configuration from the config file
 *
 * @param config_dir Configuration directory
 * @return Status code
 */
eb_status_t eb_remote_load_config(const char *config_dir);

/**
 * Save remote configuration to the config file
 *
 * @param config_dir Configuration directory
 * @return Status code
 */
eb_status_t eb_remote_save_config(const char *config_dir);

/**
 * Add a dataset to the registry
 *
 * @param name Dataset name
 * @param remote_name Associated remote name
 * @param path Path to the dataset on the remote
 * @return Status code
 */
eb_status_t eb_dataset_add(const char *name, const char *remote_name, const char *path);

/**
 * Remove a dataset from the registry
 *
 * @param name Dataset name
 * @return Status code
 */
eb_status_t eb_dataset_remove(const char *name);

/**
 * List all datasets
 *
 * @param names Pointer to store the array of dataset names (caller must free each string and the array)
 * @param count Pointer to store the number of datasets
 * @return Status code
 */
eb_status_t eb_dataset_list(char ***names, int *count);

/**
 * Get information about a dataset
 *
 * @param name Dataset name
 * @param remote_name Buffer to store the remote name (can be NULL)
 * @param remote_name_size Size of the remote name buffer
 * @param path Buffer to store the path (can be NULL)
 * @param path_size Size of the path buffer
 * @param has_documents Pointer to store whether the dataset has documents (can be NULL)
 * @param has_queries Pointer to store whether the dataset has queries (can be NULL)
 * @return Status code
 */
eb_status_t eb_dataset_info(
    const char *name, 
    char *remote_name, 
    size_t remote_name_size,
    char *path, 
    size_t path_size, 
    bool *has_documents, 
    bool *has_queries);

/**
 * Update dataset metadata
 *
 * @param name Dataset name
 * @param created_at Creation timestamp (can be NULL)
 * @param source Source information (can be NULL)
 * @param task Task information (can be NULL)
 * @param dense_model Dense model information (can be NULL)
 * @param sparse_model Sparse model information (can be NULL)
 * @return Status code
 */
eb_status_t eb_dataset_update_metadata(
    const char *name, 
    const char *created_at,
    const char *source, 
    const char *task,
    const char *dense_model, 
    const char *sparse_model);

/**
 * Set dataset document and query availability flags
 *
 * @param name Dataset name
 * @param has_documents Whether the dataset has documents
 * @param has_queries Whether the dataset has queries
 * @return Status code
 */
eb_status_t eb_dataset_set_availability(const char *name, bool has_documents, bool has_queries);

/**
 * Prune old or unused data from a remote
 *
 * @param remote_name Remote name
 * @param path Path on the remote
 * @param older_than Only prune data older than this timestamp (in seconds)
 * @param dry_run If true, only report what would be pruned without actually removing anything
 * @return Status code
 */
eb_status_t eb_remote_prune(
    const char *remote_name,
    const char *path,
    time_t older_than,
    bool dry_run);

/**
 * Save operation states to persistent storage
 *
 * @param filename Path to the state file
 * @return Status code
 */
eb_status_t save_operation_states(const char *filename);

/**
 * Load operation states from persistent storage
 *
 * @param filename Path to the state file
 * @return Status code
 */
eb_status_t load_operation_states(const char *filename);

/**
 * Resume an interrupted push operation
 *
 * @param remote_name Remote name
 * @param data Data to push
 * @param size Size of data
 * @param path Path on the remote
 * @param hash Hash of the data (embedding)
 * @return Status code
 */
eb_status_t eb_remote_resume_push(
    const char *remote_name,
    const void *data,
    size_t size,
    const char *path,
    const char *hash);

/**
 * List resumable operations
 *
 * @param operation_list Pointer to store the array of operation descriptions (caller must free)
 * @param count Pointer to store the number of operations
 * @return Status code
 */
eb_status_t eb_remote_list_operations(
    char ***operation_list,
    size_t *count);

/**
 * List all files (hashes) in a remote set path.
 *
 * @param remote_name Remote name
 * @param set_path Path to the set on the remote (e.g., "sets/<set_name>")
 * @param files_out Pointer to array of strings (caller must free each string and the array)
 * @param count_out Pointer to number of files
 * @return Status code
 */
eb_status_t eb_remote_list_files(const char *remote_name, const char *set_path, char ***files_out, size_t *count_out);

/**
 * Delete files from a remote set path.
 *
 * @param remote_name Remote name
 * @param set_path Path to the set on the remote (e.g., "sets/<set_name>")
 * @param files Array of file names to delete
 * @param count Number of files to delete
 * @return Status code
 */
eb_status_t eb_remote_delete_files(const char *remote_name, const char *set_path, const char **files, size_t count);

#endif /* EB_REMOTE_H */ 