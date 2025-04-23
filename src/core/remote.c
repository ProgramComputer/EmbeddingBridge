/*
 * EmbeddingBridge - Remote Operations
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
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "remote.h"
#include "transport.h"
#include "transformer.h"
#include "compress.h"
#include "debug.h"

/* Maximum number of remotes we can track */
#define MAX_REMOTES 32

/* Error codes not defined in status.h */
#define EB_ERROR_TRANSPORT EB_ERROR_CONNECTION_FAILED
#define EB_ERROR_PROTOCOL EB_ERROR_REMOTE_PROTOCOL
#define EB_ERROR_FORMAT EB_ERROR_INVALID_FORMAT

/* Forward declarations of static functions */
static void recover_transactions(void);
static void calculate_checksum(const void *data, size_t size, char *checksum_out, size_t checksum_size);
static int start_operation(const char *remote_name, const char *path, size_t size, const void *data, int operation_type);
static void update_operation(int op_idx, size_t transferred);
static void complete_operation(int op_idx);
static bool verify_data_integrity(const void *data, size_t size, const char *expected_checksum);
static const char* get_remote_target_format(const char* remote_name);

/* Remote configuration structure */
typedef struct {
    char name[64];                /* Remote name */
    char url[256];                /* Base URL */
    char token[256];              /* Authentication token */
    int  timeout;                 /* Timeout in seconds */
    bool verify_ssl;              /* Whether to verify SSL certificates */
    char transformer_name[32];    /* Name of transformer to use */
    char target_format[32];       /* Target format for transformation (e.g., "parquet") */
} remote_config_t;

/* Dataset structure */
typedef struct {
    char name[128];               /* Dataset name */
    char remote_name[64];         /* Associated remote name */
    char path[512];               /* Path to dataset on the remote */
    bool has_documents;           /* Whether it has documents */
    bool has_queries;             /* Whether it has queries */
    char created_at[32];          /* Creation timestamp */
    char source[128];             /* Source information */
    char task[64];                /* Task information */
    char dense_model[128];        /* Dense model information */
    char sparse_model[128];       /* Sparse model information */
} dataset_info_t;

/* Global registry of remotes */
static remote_config_t remotes[MAX_REMOTES];
static int remote_count = 0;
static pthread_mutex_t remote_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Maximum number of datasets we can track */
#define MAX_DATASETS 128
static dataset_info_t datasets[MAX_DATASETS];
static int dataset_count = 0;
static pthread_mutex_t dataset_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Flag to indicate if the remote subsystem is initialized */
static bool initialized = false;

/* Constants for batched operations */
#define BATCH_SIZE (8 * 1024 * 1024)  /* 8MB batches */
#define MAX_RETRIES 3                 /* Maximum retry attempts */
#define RETRY_DELAY_MS 1000           /* Delay between retries (milliseconds) */

/* 
 * Structure to track operation state for resumable operations 
 * This allows recovering from interrupted operations
 */
typedef struct {
    char remote_name[64];     /* Remote name */
    char path[256];           /* Remote path */
    size_t total_size;        /* Total data size */
    size_t transferred;       /* Amount transferred so far */
    time_t start_time;        /* When the operation started */
    time_t last_update;       /* Last progress update */
    char checksum[128];       /* MD5 checksum of the data */
    int operation_type;       /* 0 = push, 1 = pull */
    bool completed;           /* Whether operation completed */
} operation_state_t;

/* Maximum number of operations we can track */
#define MAX_OPERATIONS 32
static operation_state_t operations[MAX_OPERATIONS];
static int operation_count = 0;
static pthread_mutex_t operation_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Temporary reference file path */
#define TEMP_REF_FILE ".embr/REMOTE_TEMP"
/* Lock file to prevent concurrent operations */
#define LOCK_FILE ".embr/REMOTE_LOCK"
/* Commit log for durability */
#define COMMIT_LOG ".embr/REMOTE_JOURNAL"
/* Completed reference path */
#define REF_FILE ".embr/REMOTE_HEAD"

/* Lock state for atomic operations */
static bool atomic_lock_held = false;

/* Sleep for milliseconds */
static void sleep_ms(int milliseconds) {
#ifdef _WIN32
    Sleep(milliseconds);
#else
    usleep(milliseconds * 1000);
#endif
}

/* Initialize the remote subsystem */
eb_status_t eb_remote_init(void) {
    if (initialized) {
        return EB_SUCCESS;
    }
    
    /* Initialize the transformer registry */
    eb_status_t status = eb_transformer_registry_init();
    if (status != EB_SUCCESS) {
        DEBUG_ERROR("Failed to initialize transformer registry");
        return status;
    }
    
    /* Register built-in transformers */
    status = eb_register_builtin_transformers();
    if (status != EB_SUCCESS) {
        DEBUG_ERROR("Failed to register built-in transformers");
        return status;
    }
    
    /* Perform crash recovery if needed */
    recover_transactions();
    
    /* Load saved operation states */
    status = load_operation_states(".embr/operations.state");
    if (status != EB_SUCCESS) {
        DEBUG_WARN("Failed to load operation states: %d", status);
        /* Non-fatal error, continue */
    }
    
    /* Load remote configuration */
    status = eb_remote_load_config(".embr");
    if (status != EB_SUCCESS) {
        DEBUG_WARN("Failed to load remote configuration: %d", status);
        /* Non-fatal error, continue */
    }
    
    initialized = true;
    DEBUG_INFO("Remote subsystem initialized");
    
    return EB_SUCCESS;
}

/* Add a new remote */
eb_status_t eb_remote_add(const char *name, const char *url, const char *token,
                        int timeout, bool verify_ssl, const char *transformer) {
    if (!name || !url) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    pthread_mutex_lock(&remote_mutex);
    
    /* Check if we already have a remote with this name */
    for (int i = 0; i < remote_count; i++) {
        if (strcmp(remotes[i].name, name) == 0) {
            pthread_mutex_unlock(&remote_mutex);
        return EB_ERROR_ALREADY_EXISTS;
        }
    }
    
    /* Check if we have room for another remote */
    if (remote_count >= MAX_REMOTES) {
        pthread_mutex_unlock(&remote_mutex);
        return EB_ERROR_RESOURCE_EXHAUSTED;
    }
    
    /* Create new remote */
    remote_config_t *remote = &remotes[remote_count++];
    strncpy(remote->name, name, sizeof(remote->name) - 1);
    strncpy(remote->url, url, sizeof(remote->url) - 1);
    
    if (token) {
        strncpy(remote->token, token, sizeof(remote->token) - 1);
    } else {
        remote->token[0] = '\0';
    }
    
    remote->timeout = (timeout > 0) ? timeout : 30;  /* Default: 30 seconds */
    remote->verify_ssl = verify_ssl;
    
    if (transformer) {
        strncpy(remote->transformer_name, transformer, sizeof(remote->transformer_name) - 1);
    } else {
        /* Default to JSON transformer if none specified */
        strncpy(remote->transformer_name, "json", sizeof(remote->transformer_name) - 1);
    }
    
    pthread_mutex_unlock(&remote_mutex);
    
    /* Save the updated configuration */
    eb_status_t status = eb_remote_save_config(".embr");
    if (status != EB_SUCCESS) {
        DEBUG_WARN("Failed to save remote configuration: %d", status);
        /* Non-fatal error, continue */
    }
    
    DEBUG_INFO("Added remote '%s' with URL '%s'", name, url);
    return EB_SUCCESS;
}

/* Remove a remote */
eb_status_t eb_remote_remove(const char *name) {
    if (!name) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    pthread_mutex_lock(&remote_mutex);
    
    /* Find the remote */
    int index = -1;
    for (int i = 0; i < remote_count; i++) {
        if (strcmp(remotes[i].name, name) == 0) {
            index = i;
            break;
        }
    }
    
    if (index == -1) {
        pthread_mutex_unlock(&remote_mutex);
        return EB_ERROR_NOT_FOUND;
    }
    
    /* Remove the remote by shifting the array */
    if (index < remote_count - 1) {
        memmove(&remotes[index], &remotes[index + 1], 
                (remote_count - index - 1) * sizeof(remote_config_t));
    }
    remote_count--;
    
    pthread_mutex_unlock(&remote_mutex);
    
    /* Save the updated configuration */
    eb_status_t status = eb_remote_save_config(".embr");
    if (status != EB_SUCCESS) {
        DEBUG_WARN("Failed to save remote configuration: %d", status);
        /* Non-fatal error, continue */
    }
    
    DEBUG_INFO("Removed remote '%s'", name);
    return EB_SUCCESS;
}

/* Get information about a remote */
eb_status_t eb_remote_info(const char *name, char *url, size_t url_size,
                         int *timeout, bool *verify_ssl, char *transformer, size_t transformer_size) {
    if (!name) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    pthread_mutex_lock(&remote_mutex);
    
    /* Find the remote */
    int index = -1;
    for (int i = 0; i < remote_count; i++) {
        if (strcmp(remotes[i].name, name) == 0) {
            index = i;
            break;
        }
    }
    
    if (index == -1) {
        pthread_mutex_unlock(&remote_mutex);
    return EB_ERROR_NOT_FOUND;
}

    /* Copy the information */
    if (url && url_size > 0) {
        strncpy(url, remotes[index].url, url_size - 1);
        url[url_size - 1] = '\0';
    }
    
    if (timeout) {
        *timeout = remotes[index].timeout;
    }
    
    if (verify_ssl) {
        *verify_ssl = remotes[index].verify_ssl;
    }
    
    if (transformer && transformer_size > 0) {
        strncpy(transformer, remotes[index].transformer_name, transformer_size - 1);
        transformer[transformer_size - 1] = '\0';
    }
    
    pthread_mutex_unlock(&remote_mutex);
    return EB_SUCCESS;
}

/* List all remotes */
eb_status_t eb_remote_list(char ***names, int *count) {
    if (!names || !count) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    pthread_mutex_lock(&remote_mutex);
    
    if (remote_count == 0) {
        *names = NULL;
        *count = 0;
        pthread_mutex_unlock(&remote_mutex);
        return EB_SUCCESS;
    }
    
    /* Allocate array of string pointers */
    char **name_list = (char **)malloc(remote_count * sizeof(char *));
    if (!name_list) {
        pthread_mutex_unlock(&remote_mutex);
        return EB_ERROR_OUT_OF_MEMORY;
    }
    
    /* Allocate and copy each name */
    for (int i = 0; i < remote_count; i++) {
        name_list[i] = strdup(remotes[i].name);
        if (!name_list[i]) {
            /* Clean up on error */
            for (int j = 0; j < i; j++) {
                free(name_list[j]);
            }
            free(name_list);
            pthread_mutex_unlock(&remote_mutex);
            return EB_ERROR_OUT_OF_MEMORY;
        }
    }
    
    *names = name_list;
    *count = remote_count;
    
    pthread_mutex_unlock(&remote_mutex);
    return EB_SUCCESS;
}

/* Add a dataset to the registry */
eb_status_t eb_dataset_add(const char *name, const char *remote_name, const char *path) {
    if (!name || !remote_name || !path) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    pthread_mutex_lock(&dataset_mutex);
    
    /* Check if we already have a dataset with this name */
    for (int i = 0; i < dataset_count; i++) {
        if (strcmp(datasets[i].name, name) == 0) {
            pthread_mutex_unlock(&dataset_mutex);
            return EB_ERROR_ALREADY_EXISTS;
        }
    }
    
    /* Check if we have room for another dataset */
    if (dataset_count >= MAX_DATASETS) {
        pthread_mutex_unlock(&dataset_mutex);
        return EB_ERROR_RESOURCE_EXHAUSTED;
    }
    
    /* Create new dataset entry */
    dataset_info_t *dataset = &datasets[dataset_count++];
    strncpy(dataset->name, name, sizeof(dataset->name) - 1);
    strncpy(dataset->remote_name, remote_name, sizeof(dataset->remote_name) - 1);
    strncpy(dataset->path, path, sizeof(dataset->path) - 1);
    
    /* Initialize with default values */
    dataset->has_documents = false;
    dataset->has_queries = false;
    dataset->created_at[0] = '\0';
    dataset->source[0] = '\0';
    dataset->task[0] = '\0';
    dataset->dense_model[0] = '\0';
    dataset->sparse_model[0] = '\0';
    
    pthread_mutex_unlock(&dataset_mutex);
    
    DEBUG_INFO("Added dataset '%s' on remote '%s' with path '%s'", 
                 name, remote_name, path);
    return EB_SUCCESS;
}

/* Remove a dataset */
eb_status_t eb_dataset_remove(const char *name) {
    if (!name) {
        return EB_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&dataset_mutex);
    
    /* Find the dataset */
    int index = -1;
    for (int i = 0; i < dataset_count; i++) {
        if (strcmp(datasets[i].name, name) == 0) {
            index = i;
            break;
        }
    }
    
    if (index == -1) {
        pthread_mutex_unlock(&dataset_mutex);
        return EB_ERROR_NOT_FOUND;
    }
    
    /* Remove the dataset by shifting the array */
    if (index < dataset_count - 1) {
        memmove(&datasets[index], &datasets[index + 1], 
                (dataset_count - index - 1) * sizeof(dataset_info_t));
    }
    dataset_count--;
    
    pthread_mutex_unlock(&dataset_mutex);
    
    DEBUG_INFO("Removed dataset '%s'", name);
    return EB_SUCCESS;
}

/* List all datasets */
eb_status_t eb_dataset_list(char ***names, int *count) {
    if (!names || !count) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    pthread_mutex_lock(&dataset_mutex);
    
    if (dataset_count == 0) {
        *names = NULL;
        *count = 0;
        pthread_mutex_unlock(&dataset_mutex);
        return EB_SUCCESS;
    }
    
    /* Allocate array of string pointers */
    char **name_list = (char **)malloc(dataset_count * sizeof(char *));
    if (!name_list) {
        pthread_mutex_unlock(&dataset_mutex);
        return EB_ERROR_OUT_OF_MEMORY;
    }
    
    /* Allocate and copy each name */
    for (int i = 0; i < dataset_count; i++) {
        name_list[i] = strdup(datasets[i].name);
        if (!name_list[i]) {
            /* Clean up on error */
            for (int j = 0; j < i; j++) {
                free(name_list[j]);
            }
            free(name_list);
            pthread_mutex_unlock(&dataset_mutex);
            return EB_ERROR_OUT_OF_MEMORY;
        }
    }
    
    *names = name_list;
    *count = dataset_count;
    
    pthread_mutex_unlock(&dataset_mutex);
    return EB_SUCCESS;
}

/* Get information about a dataset */
eb_status_t eb_dataset_info(const char *name, char *remote_name, size_t remote_name_size,
                          char *path, size_t path_size, bool *has_documents, bool *has_queries) {
    if (!name) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    pthread_mutex_lock(&dataset_mutex);
    
    /* Find the dataset */
    int index = -1;
    for (int i = 0; i < dataset_count; i++) {
        if (strcmp(datasets[i].name, name) == 0) {
            index = i;
            break;
        }
    }
    
    if (index == -1) {
        pthread_mutex_unlock(&dataset_mutex);
        return EB_ERROR_NOT_FOUND;
    }
    
    /* Copy the information */
    if (remote_name && remote_name_size > 0) {
        strncpy(remote_name, datasets[index].remote_name, remote_name_size - 1);
        remote_name[remote_name_size - 1] = '\0';
    }
    
    if (path && path_size > 0) {
        strncpy(path, datasets[index].path, path_size - 1);
        path[path_size - 1] = '\0';
    }
    
    if (has_documents) {
        *has_documents = datasets[index].has_documents;
    }
    
    if (has_queries) {
        *has_queries = datasets[index].has_queries;
    }
    
    pthread_mutex_unlock(&dataset_mutex);
    return EB_SUCCESS;
}

/* Update dataset metadata */
eb_status_t eb_dataset_update_metadata(const char *name, const char *created_at,
                                    const char *source, const char *task,
                                    const char *dense_model, const char *sparse_model) {
    if (!name) {
        return EB_ERROR_INVALID_PARAMETER;
    }

    pthread_mutex_lock(&dataset_mutex);
    
    /* Find the dataset */
    int index = -1;
    for (int i = 0; i < dataset_count; i++) {
        if (strcmp(datasets[i].name, name) == 0) {
            index = i;
            break;
        }
    }
    
    if (index == -1) {
        pthread_mutex_unlock(&dataset_mutex);
        return EB_ERROR_NOT_FOUND;
    }
    
    /* Update metadata */
    if (created_at) {
        strncpy(datasets[index].created_at, created_at, sizeof(datasets[index].created_at) - 1);
    }
    
    if (source) {
        strncpy(datasets[index].source, source, sizeof(datasets[index].source) - 1);
    }
    
    if (task) {
        strncpy(datasets[index].task, task, sizeof(datasets[index].task) - 1);
    }
    
    if (dense_model) {
        strncpy(datasets[index].dense_model, dense_model, sizeof(datasets[index].dense_model) - 1);
    }
    
    if (sparse_model) {
        strncpy(datasets[index].sparse_model, sparse_model, sizeof(datasets[index].sparse_model) - 1);
    }
    
    pthread_mutex_unlock(&dataset_mutex);
    
    DEBUG_INFO("Updated metadata for dataset '%s'", name);
    return EB_SUCCESS;
}

/* Set dataset document and query availability flags */
eb_status_t eb_dataset_set_availability(const char *name, bool has_documents, bool has_queries) {
    if (!name) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    pthread_mutex_lock(&dataset_mutex);
    
    /* Find the dataset */
    int index = -1;
    for (int i = 0; i < dataset_count; i++) {
        if (strcmp(datasets[i].name, name) == 0) {
            index = i;
            break;
        }
    }
    
    if (index == -1) {
        pthread_mutex_unlock(&dataset_mutex);
        return EB_ERROR_NOT_FOUND;
    }
    
    /* Update availability flags */
    datasets[index].has_documents = has_documents;
    datasets[index].has_queries = has_queries;
    
    pthread_mutex_unlock(&dataset_mutex);
    
    DEBUG_INFO("Updated availability for dataset '%s': documents=%d, queries=%d", 
                 name, has_documents, has_queries);
    return EB_SUCCESS;
}

/* Shutdown the remote subsystem */
void eb_remote_shutdown(void) {
    if (!initialized) {
        return;
    }
    
    initialized = false;
    
    /* Save operation states */
    eb_status_t status = save_operation_states(".embr/operations.state");
    if (status != EB_SUCCESS) {
        DEBUG_WARN("Failed to save operation states: %d", status);
        /* Non-fatal error, continue */
    }
    
    /* Cleanup transformer registry */
    eb_transformer_registry_cleanup();
    
    /* Reset counts */
    remote_count = 0;
    dataset_count = 0;
    
    DEBUG_INFO("Remote subsystem shutdown");
}

/* Check if remote operations are available */
bool eb_remote_available(void) {
    return initialized;
}

/* 
 * Acquire an atomic operation lock
 * Returns EB_SUCCESS if the lock was acquired, or error code if not
 */
static eb_status_t acquire_atomic_lock(void) {
    /* Fail if we already hold the lock */
    if (atomic_lock_held) {
        return EB_SUCCESS;  /* Already have it */
    }
    
    /* Try to create the lock file */
    FILE *lock_file = fopen(LOCK_FILE, "wx");  /* x = fail if exists */
    
    if (!lock_file) {
        if (errno == EEXIST) {
            /* Lock is held by another process/thread */
            return EB_ERROR_LOCK_FAILED;
        }
        
        /* Other error (e.g., permission denied) */
        DEBUG_ERROR("Failed to acquire lock: %s", strerror(errno));
        return EB_ERROR_IO;
    }
    
    /* Lock acquired, write our PID to the file */
    fprintf(lock_file, "%d", getpid());
    fclose(lock_file);
    
    atomic_lock_held = true;
    DEBUG_INFO("Acquired atomic operation lock");
    
    return EB_SUCCESS;
}

/*
 * Release the atomic operation lock
 */
static void release_atomic_lock(void) {
    if (!atomic_lock_held) {
        return;  /* Not holding the lock */
    }
    
    /* Remove the lock file */
    if (unlink(LOCK_FILE) != 0) {
        DEBUG_WARN("Failed to remove lock file: %s", strerror(errno));
        /* We'll still consider the lock released */
    }
    
    atomic_lock_held = false;
    DEBUG_INFO("Released atomic operation lock");
}

/*
 * Begin a transaction by creating a journal entry
 * Returns EB_SUCCESS if the transaction was started, or error code if not
 */
static eb_status_t begin_transaction(const char *operation, const char *remote_name, const char *path) {
    DEBUG_PRINT("begin_transaction: Starting with operation=%s, remote=%s, path=%s", 
               operation ? operation : "(null)", 
               remote_name ? remote_name : "(null)", 
               path ? path : "(null)");
    
    /* Acquire the atomic lock */
    eb_status_t status = acquire_atomic_lock();
    if (status != EB_SUCCESS) {
        DEBUG_ERROR("begin_transaction: Failed to acquire lock: %d", status);
        return status;
    }
    
    DEBUG_PRINT("begin_transaction: Lock acquired");
    
    /* Create the journal directory if it doesn't exist */
    char journal_dir[256];
    strncpy(journal_dir, COMMIT_LOG, sizeof(journal_dir));
    char *last_slash = strrchr(journal_dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        mkdir(journal_dir, 0755);
    }
    
    /* Open the journal for writing (append mode) */
    FILE *journal = fopen(COMMIT_LOG, "a");
    if (!journal) {
        DEBUG_ERROR("begin_transaction: Failed to open journal: %s", strerror(errno));
        release_atomic_lock();
        return EB_ERROR_IO;
    }
    
    DEBUG_PRINT("begin_transaction: Journal opened");
    
    /* Write transaction details */
    time_t now = time(NULL);
    fprintf(journal, "BEGIN %ld %s %s %s\n", (long)now, operation, remote_name, path);
    
    fflush(journal);
    fclose(journal);
    
    DEBUG_PRINT("begin_transaction: Journal entry written and closed");
    DEBUG_PRINT("begin_transaction: Successfully completed");
    return EB_SUCCESS;
}

/*
 * Commit a transaction by updating the reference file atomically
 * Returns EB_SUCCESS if the transaction was committed, or error code if not
 */
static eb_status_t commit_transaction(void) {
    if (!atomic_lock_held) {
        DEBUG_ERROR("Attempted to commit without holding lock");
        return EB_ERROR_LOCK_FAILED;
    }
    
    /* Check if the temporary reference file exists */
    FILE *temp_ref = fopen(TEMP_REF_FILE, "r");
    if (!temp_ref) {
        DEBUG_ERROR("No temporary reference file exists");
        return EB_ERROR_NOT_FOUND;
    }
    fclose(temp_ref);
    
    /* Atomically replace the reference file with the temporary one */
    if (rename(TEMP_REF_FILE, REF_FILE) != 0) {
        DEBUG_ERROR("Failed to rename temp ref to ref: %s", strerror(errno));
        return EB_ERROR_IO;
    }
    
    /* Record the commit in the journal */
    FILE *journal = fopen(COMMIT_LOG, "a");
    if (journal) {
        time_t now = time(NULL);
        fprintf(journal, "COMMIT %ld\n", (long)now);
        fflush(journal);
        fclose(journal);
    }
    
    /* Release the lock */
    release_atomic_lock();
    
    return EB_SUCCESS;
}

/*
 * Abort a transaction by cleaning up the temporary file
 * Returns EB_SUCCESS if the transaction was aborted, or error code if not
 */
static eb_status_t abort_transaction(void) {
    if (!atomic_lock_held) {
        DEBUG_ERROR("Attempted to abort without holding lock");
        return EB_ERROR_LOCK_FAILED;
    }
    
    /* Remove the temporary reference file if it exists */
    if (unlink(TEMP_REF_FILE) != 0 && errno != ENOENT) {
        DEBUG_WARN("Failed to remove temp ref: %s", strerror(errno));
        /* Continue anyway */
    }
    
    /* Record the abort in the journal */
    FILE *journal = fopen(COMMIT_LOG, "a");
    if (journal) {
        time_t now = time(NULL);
        fprintf(journal, "ABORT %ld\n", (long)now);
        fflush(journal);
        fclose(journal);
    }
    
    /* Release the lock */
    release_atomic_lock();
    
    return EB_SUCCESS;
}

/*
 * Check if there are interrupted transactions that need recovery
 * Returns true if recovery is needed, false otherwise
 */
static bool recovery_needed(void) {
    FILE *journal = fopen(COMMIT_LOG, "r");
    if (!journal) {
        return false;  /* No journal, no recovery needed */
    }
    
    /* Read the journal and see if there are any BEGIN without COMMIT or ABORT */
    char line[512];
    bool transaction_in_progress = false;
    
    while (fgets(line, sizeof(line), journal)) {
        if (strncmp(line, "BEGIN", 5) == 0) {
            transaction_in_progress = true;
        } else if (strncmp(line, "COMMIT", 6) == 0 || strncmp(line, "ABORT", 5) == 0) {
            transaction_in_progress = false;
        }
    }
    
    fclose(journal);
    return transaction_in_progress;
}

/*
 * Recover from interrupted transactions
 * This is called during initialization to ensure ACID properties are maintained
 */
static void recover_transactions(void) {
    if (!recovery_needed()) {
        return;  /* No recovery needed */
    }
    
    DEBUG_WARN("Interrupted transaction detected, recovering...");
    
    /* Check if the lock file exists */
    FILE *lock_file = fopen(LOCK_FILE, "r");
    if (lock_file) {
        /* Lock file exists, read the PID */
        int pid;
        if (fscanf(lock_file, "%d", &pid) == 1) {
            /* Check if the process is still running */
            if (kill(pid, 0) == 0) {
                DEBUG_WARN("Process %d still holds the lock, not recovering", pid);
                fclose(lock_file);
                return;
            }
        }
        fclose(lock_file);
        
        /* Remove the stale lock file */
        unlink(LOCK_FILE);
    }
    
    /* Check if the temporary reference file exists */
    FILE *temp_ref = fopen(TEMP_REF_FILE, "r");
    if (temp_ref) {
        fclose(temp_ref);
        
        /* Temporary file exists - attempt to complete the transaction */
        DEBUG_INFO("Completing interrupted transaction");
        
        /* Atomically replace the reference file with the temporary one */
        if (rename(TEMP_REF_FILE, REF_FILE) != 0) {
            DEBUG_ERROR("Recovery failed to rename temp ref: %s", strerror(errno));
            unlink(TEMP_REF_FILE);  /* Clean up */
        } else {
            DEBUG_INFO("Transaction recovered successfully");
        }
    } else {
        /* No temporary file, abort the transaction */
        DEBUG_INFO("Aborting interrupted transaction");
    }
    
    /* Record the recovery in the journal */
    FILE *journal = fopen(COMMIT_LOG, "a");
    if (journal) {
        time_t now = time(NULL);
        fprintf(journal, "RECOVER %ld\n", (long)now);
        fflush(journal);
        fclose(journal);
    }
}

/* 
 * Update eb_remote_push to use the atomic transaction model
 */
eb_status_t eb_remote_push(
    const char *remote_name,
    const void *data,
    size_t size,
    const char *path,
    const char *hash) {
    
    DEBUG_PRINT("eb_remote_push: Starting with remote=%s, size=%zu, path=%s", 
               remote_name ? remote_name : "(null)", 
               size, 
               path ? path : "(null)");
    
    if (!remote_name || !data || !path) {
        DEBUG_PRINT("eb_remote_push: Parameter validation failed - remote_name=%p, data=%p, path=%p",
                  (void*)remote_name, data, (void*)path);
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    DEBUG_PRINT("eb_remote_push: Parameters validated successfully");
    
    /* Begin a new transaction */
    DEBUG_PRINT("eb_remote_push: Attempting to begin transaction");
    eb_status_t status = begin_transaction("PUSH", remote_name, path);
    if (status != EB_SUCCESS) {
        DEBUG_ERROR("eb_remote_push: Failed to begin transaction: %d", status);
        return status;
    }
    
    DEBUG_PRINT("eb_remote_push: Transaction started successfully");
    
    /* Start tracking this operation */
    DEBUG_PRINT("eb_remote_push: Starting operation tracking");
    int op_idx = start_operation(remote_name, path, size, data, 0);
    DEBUG_PRINT("eb_remote_push: Operation tracking started, op_idx=%d", op_idx);
    
    /* Find the remote configuration */
    DEBUG_PRINT("eb_remote_push: Looking up remote configuration for '%s'", remote_name);
    pthread_mutex_lock(&remote_mutex);
    
    int remote_index = -1;
    for (int i = 0; i < remote_count; i++) {
        DEBUG_PRINT("eb_remote_push: Checking remote[%d]='%s'", i, remotes[i].name);
        if (strcmp(remotes[i].name, remote_name) == 0) {
            remote_index = i;
            break;
        }
    }
    
    if (remote_index == -1) {
        DEBUG_PRINT("eb_remote_push: Remote '%s' not found in list of %d remotes", 
                  remote_name, remote_count);
        pthread_mutex_unlock(&remote_mutex);
        DEBUG_ERROR("Remote '%s' not found", remote_name);
        abort_transaction();
        return EB_ERROR_NOT_FOUND;
    }
    
    DEBUG_PRINT("eb_remote_push: Found remote '%s' at index %d with URL '%s'", 
              remote_name, remote_index, remotes[remote_index].url);
    
    /* Make a copy of the remote configuration to avoid holding the lock */
    remote_config_t remote_config = remotes[remote_index];
    pthread_mutex_unlock(&remote_mutex);
    
    /* Construct the full URL for the path */
    char full_url[1024];
    snprintf(full_url, sizeof(full_url), "%s/%s", remote_config.url, path);
    DEBUG_INFO("Pushing to URL: %s", full_url);
    
    /* Open the transport */
    DEBUG_PRINT("eb_remote_push: Opening transport for URL '%s'", full_url);
    eb_transport_t *transport = transport_open(full_url);
    if (!transport) {
        DEBUG_ERROR("Failed to open transport for '%s'", full_url);
        abort_transaction();
        return EB_ERROR_TRANSPORT;
    }
    
    /* Set the target path for the transport */
    transport->target_path = strdup(path);
    
    /* Ensure the data_is_precompressed flag is initialized to false */
    transport->data_is_precompressed = false;
    
    DEBUG_INFO("eb_remote_push: Transport opened successfully, type=%d", transport->type);
    
    /* Connect to the remote */
    DEBUG_PRINT("eb_remote_push: Connecting to remote");
    int connect_result = transport_connect(transport);
    if (connect_result != EB_SUCCESS) {
        DEBUG_ERROR("Failed to connect to '%s': %s", 
                  full_url, transport_get_error(transport));
        transport_close(transport);
        abort_transaction();
        return connect_result;
    }
    
    /* Set the target path for the operation */
    /* Make sure target_path is properly allocated */
    if (path) {
        /* Free existing target_path if it exists */
        if (transport->target_path) {
            free((void*)transport->target_path);
        }
        
        char *target_path_copy = strdup(path);
        if (!target_path_copy) {
            DEBUG_ERROR("Failed to allocate memory for target path");
            transport_disconnect(transport);
            transport_close(transport);
            abort_transaction();
            return EB_ERROR_MEMORY;
        }
        transport->target_path = target_path_copy;
    }
    
    eb_status_t result = EB_SUCCESS;
    
    /* For small data, send directly */
    if (size <= BATCH_SIZE) {
        /* Skip compression for all formats to avoid issues with Parquet transformer */
        DEBUG_WARN("Skipping pre-compression before transport");
        void *compressed_data = (void*)data;  /* Use original data */
        size_t compressed_size = size;
        
        /* Send the data with retries */
        int retry_count = 0;
        while (retry_count < MAX_RETRIES) {
            DEBUG_INFO("About to call transport_send_data with %p, %p, %zu", 
                     transport, compressed_data, compressed_size);
            
            /* Set flag to indicate data is not pre-compressed */
            transport->data_is_precompressed = false;
            DEBUG_WARN("Setting data_is_precompressed flag to FALSE");
            
            result = transport_send_data(transport, compressed_data, compressed_size, hash);
            
            DEBUG_INFO("transport_send_data returned: %d", result);
            
            if (result == EB_SUCCESS) {
                break;
            }
            
            DEBUG_WARN("Retry %d/%d: Failed to send data: %s", 
                        retry_count + 1, MAX_RETRIES, 
                        transport_get_error(transport));
            
            retry_count++;
            if (retry_count < MAX_RETRIES) {
                sleep_ms(RETRY_DELAY_MS);
            }
        }
        
        if (result != EB_SUCCESS) {
            DEBUG_ERROR("Failed to send data after %d retries: %s", 
                      MAX_RETRIES, transport_get_error(transport));
            transport_disconnect(transport);
            transport_close(transport);
            abort_transaction();
            return result;
        }
        
        /* Mark operation as completed */
        if (op_idx >= 0) {
            update_operation(op_idx, size);
            complete_operation(op_idx);
        }
    } else {
        /* For large data, send in batches */
        const unsigned char *data_ptr = (const unsigned char *)data;
        size_t remaining = size;
        size_t batch_number = 0;
        size_t total_batches = (size + BATCH_SIZE - 1) / BATCH_SIZE;
        
        while (remaining > 0 && result == EB_SUCCESS) {
            /* Determine current batch size */
            size_t current_batch_size = (remaining > BATCH_SIZE) ? BATCH_SIZE : remaining;
            
            /* Compress the batch */
            void *compressed_batch = NULL;
            size_t compressed_size = 0;
            
            result = compress_buffer(data_ptr, current_batch_size, 
                                   remote_config.timeout, 
                                   &compressed_batch, &compressed_size);
            if (result != EB_SUCCESS) {
                DEBUG_ERROR("Failed to compress batch %zu/%zu: %d", 
                          batch_number + 1, total_batches, result);
                transport_disconnect(transport);
                transport_close(transport);
                abort_transaction();
                return result;
            }
            
            /* Send batch header with batch info */
            char batch_header[256];
            snprintf(batch_header, sizeof(batch_header), 
                     "BATCH %zu/%zu SIZE %zu COMPRESSED %zu", 
                     batch_number + 1, total_batches, 
                     current_batch_size, compressed_size);
            
            /* Encode batch header length at the start (4 bytes) */
            size_t header_len = strlen(batch_header);
            unsigned char header_len_bytes[4];
            header_len_bytes[0] = (header_len >> 24) & 0xFF;
            header_len_bytes[1] = (header_len >> 16) & 0xFF;
            header_len_bytes[2] = (header_len >> 8) & 0xFF;
            header_len_bytes[3] = header_len & 0xFF;
            
            /* Send header length */
            result = transport_send_data(transport, header_len_bytes, 4, NULL);
            if (result != EB_SUCCESS) {
                DEBUG_ERROR("Failed to send batch header length: %s", 
                          transport_get_error(transport));
                free(compressed_batch);
                transport_disconnect(transport);
                transport_close(transport);
                abort_transaction();
                return result;
            }
            
            /* Send header */
            result = transport_send_data(transport, batch_header, header_len, NULL);
            if (result != EB_SUCCESS) {
                DEBUG_ERROR("Failed to send batch header: %s", 
                          transport_get_error(transport));
                free(compressed_batch);
                transport_disconnect(transport);
                transport_close(transport);
                abort_transaction();
                return result;
            }
            
            /* Send the compressed batch with retries */
            int retry_count = 0;
            while (retry_count < MAX_RETRIES) {
                result = transport_send_data(transport, compressed_batch, compressed_size, NULL);
                if (result == EB_SUCCESS) {
                    break;
                }
                
                DEBUG_WARN("Retry %d/%d: Failed to send batch %zu/%zu: %s", 
                            retry_count + 1, MAX_RETRIES, batch_number + 1, total_batches,
                            transport_get_error(transport));
                
                retry_count++;
                if (retry_count < MAX_RETRIES) {
                    sleep_ms(RETRY_DELAY_MS);
                }
            }
            
            free(compressed_batch);
            
            if (result != EB_SUCCESS) {
                DEBUG_ERROR("Failed to send batch %zu/%zu after %d retries: %s", 
                          batch_number + 1, total_batches, MAX_RETRIES,
                          transport_get_error(transport));
                transport_disconnect(transport);
                transport_close(transport);
                abort_transaction();
                return result;
            }
            
            /* Update progress */
            size_t transferred = (batch_number + 1) * BATCH_SIZE;
            if (transferred > size) {
                transferred = size;
            }
            
            if (op_idx >= 0) {
                update_operation(op_idx, transferred);
            }
            
            DEBUG_INFO("Sent batch %zu/%zu (%.1f%%)", 
                     batch_number + 1, total_batches,
                     (float)(batch_number + 1) * 100.0f / (float)total_batches);
            
            /* Move to next batch */
            data_ptr += current_batch_size;
            remaining -= current_batch_size;
            batch_number++;
        }
        
        /* Send end marker */
        const char *end_marker = "END";
        result = transport_send_data(transport, end_marker, strlen(end_marker), NULL);
        if (result != EB_SUCCESS) {
            DEBUG_ERROR("Failed to send end marker: %s", 
                      transport_get_error(transport));
            transport_disconnect(transport);
            transport_close(transport);
            abort_transaction();
            return result;
        } else {
            DEBUG_INFO("Push completed successfully: %zu bytes in %zu batches", 
                     size, total_batches);
            
            /* Mark operation as completed */
            if (op_idx >= 0) {
                complete_operation(op_idx);
            }
        }
    }
    
    /* Disconnect and cleanup */
    transport_disconnect(transport);
    transport_close(transport);
    
    /* Create the temp ref file with operation details */
    FILE *temp_ref = fopen(TEMP_REF_FILE, "w");
    if (temp_ref) {
        time_t now = time(NULL);
        fprintf(temp_ref, "OPERATION push\n");
        fprintf(temp_ref, "REMOTE %s\n", remote_name);
        fprintf(temp_ref, "PATH %s\n", path);
        fprintf(temp_ref, "SIZE %zu\n", size);
        fprintf(temp_ref, "TIMESTAMP %ld\n", (long)now);
        
        /* Calculate and store checksum for verification */
        char checksum[128];
        calculate_checksum(data, size, checksum, sizeof(checksum));
        fprintf(temp_ref, "CHECKSUM %s\n", checksum);
        
        fflush(temp_ref);
        fclose(temp_ref);
    } else {
        DEBUG_ERROR("Failed to create temp ref file: %s", strerror(errno));
        abort_transaction();
        return EB_ERROR_IO;
    }
    
    /* Commit the transaction */
    status = commit_transaction();
    if (status != EB_SUCCESS) {
        DEBUG_ERROR("Failed to commit transaction: %d", status);
        abort_transaction();
        return status;
    }
    
    return EB_SUCCESS;
}

/* Pull data from a remote with optional delta update */
eb_status_t eb_remote_pull_delta(
    const char *remote_name,
    const char *path,
    void **data_out,
    size_t *size_out,
    bool delta_only) {
    
    if (!remote_name || !data_out || !size_out) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    *data_out = NULL;
    *size_out = 0;
    
    /* Find the remote configuration */
    pthread_mutex_lock(&remote_mutex);
    
    int remote_index = -1;
    for (int i = 0; i < remote_count; i++) {
        if (strcmp(remotes[i].name, remote_name) == 0) {
            remote_index = i;
            break;
        }
    }
    
    if (remote_index == -1) {
        pthread_mutex_unlock(&remote_mutex);
        DEBUG_ERROR("Remote '%s' not found", remote_name);
        return EB_ERROR_NOT_FOUND;
    }
    
    /* Make a copy of the remote configuration to avoid holding the lock */
    remote_config_t remote_config = remotes[remote_index];
    pthread_mutex_unlock(&remote_mutex);
    
    /* Construct the full URL for the path */
    char full_url[1024];
    if (delta_only) {
        snprintf(full_url, sizeof(full_url), "%s/%s?delta=true", remote_config.url, path);
        DEBUG_INFO("Pulling deltas from URL: %s", full_url);
    } else {
        snprintf(full_url, sizeof(full_url), "%s/%s", remote_config.url, path);
        DEBUG_INFO("Pulling from URL: %s", full_url);
    }
    
    DEBUG_INFO("Opening transport to URL: %s", full_url);
    
    /* Open the transport */
    eb_transport_t *transport = transport_open(full_url);
    if (!transport) {
        DEBUG_ERROR("Failed to open transport for %s", full_url);
        return EB_ERROR_TRANSPORT;
    }
    
    /* Connect to the remote */
    int connect_status = transport_connect(transport);
    if (connect_status != EB_SUCCESS) {
        DEBUG_ERROR("Failed to connect to %s: %s", 
                  full_url, transport_get_error(transport));
        transport_close(transport);
        return connect_status;
    }
    
    /* Set the target path for the operation */
    if (path) {
        char *target_path_copy = strdup(path);
        if (!target_path_copy) {
            DEBUG_ERROR("Failed to allocate memory for target path");
            transport_disconnect(transport);
            transport_close(transport);
            return EB_ERROR_MEMORY;
        }
        transport->target_path = target_path_copy;
    }
    
    /* We'll implement a more efficient version of the pull operation */
    eb_status_t result = EB_SUCCESS;
    
    /* Single buffer for all data */
    const size_t buffer_size = 4 * 1024 * 1024; /* 4MB initial buffer */
    unsigned char *buffer = malloc(buffer_size);
        if (!buffer) {
        DEBUG_ERROR("Failed to allocate initial download buffer");
            transport_disconnect(transport);
            transport_close(transport);
        return EB_ERROR_MEMORY;
    }
    
            size_t total_received = 0;
    size_t buffer_capacity = buffer_size;
    
    /* Single download operation instead of many small ones */
    DEBUG_INFO("Starting efficient download with initial buffer of %zu bytes", buffer_capacity);
    size_t bytes_received = 0;
    
    /* Initial read to get enough data to determine format */
    result = transport_receive_data(transport, buffer, buffer_capacity, &bytes_received);
                if (result != EB_SUCCESS) {
        DEBUG_ERROR("Failed to receive data: %s", transport_get_error(transport));
                    free(buffer);
                    transport_disconnect(transport);
                    transport_close(transport);
                    return result;
                }
                
    total_received = bytes_received;
    DEBUG_INFO("Received initial %zu bytes", total_received);
    
    /* Check if we need to resize the buffer and get more data */
    if (bytes_received == buffer_capacity) {
        /* Need a larger buffer - data didn't all fit */
        while (bytes_received == buffer_capacity) {
            /* Double the buffer size */
            buffer_capacity *= 2;
            DEBUG_INFO("Expanding buffer to %zu bytes", buffer_capacity);
            
            unsigned char *new_buffer = realloc(buffer, buffer_capacity);
                if (!new_buffer) {
                DEBUG_ERROR("Failed to resize download buffer");
                    free(buffer);
                    transport_disconnect(transport);
                    transport_close(transport);
                return EB_ERROR_MEMORY;
                }
                
                buffer = new_buffer;
            
            /* Get more data */
            result = transport_receive_data(
                transport, 
                buffer + total_received, 
                buffer_capacity - total_received,
                &bytes_received);
            
            if (result != EB_SUCCESS) {
                DEBUG_ERROR("Failed to receive additional data: %s", transport_get_error(transport));
                free(buffer);
                transport_disconnect(transport);
                transport_close(transport);
                return result;
            }
            
            if (bytes_received == 0) {
                /* No more data available */
                break;
            }
            
            total_received += bytes_received;
            DEBUG_INFO("Received additional %zu bytes, total now %zu bytes", 
                     bytes_received, total_received);
        }
    }
    
    /* Everything has been received - process the data */
    DEBUG_INFO("Download complete, received total of %zu bytes", total_received);
    
    /* Decompress the data if needed */
    if (total_received > 2 && buffer[0] == 0x28 && buffer[1] == 0xB5) {
        DEBUG_INFO("Detected ZSTD compressed data");
        void *decompressed_data = NULL;
        size_t decompressed_size = 0;
        
        result = eb_decompress_zstd(buffer, total_received, 
                                 &decompressed_data, &decompressed_size);
        
        /* Free the original compressed buffer */
        free(buffer);
        
        if (result != EB_SUCCESS) {
            DEBUG_ERROR("Failed to decompress data: %d", result);
            transport_disconnect(transport);
            transport_close(transport);
            return result;
        }
        
        /* Return the decompressed data */
        *data_out = decompressed_data;
        *size_out = decompressed_size;
        
        DEBUG_INFO("Successfully pulled and decompressed %zu bytes (original: %zu bytes) from %s", 
                 decompressed_size, total_received, full_url);
    } else {
        /* Data wasn't compressed, return as-is */
        *data_out = buffer;
        *size_out = total_received;
        
        DEBUG_INFO("Successfully pulled %zu bytes from %s", 
                 total_received, full_url);
    }
    
    /* Disconnect and cleanup */
    transport_disconnect(transport);
    transport_close(transport);
    
    return EB_SUCCESS;
}

/* Pull data from a remote (backward compatibility wrapper) */
eb_status_t eb_remote_pull(
    const char *remote_name,
    const char *path,
    void **data_out,
    size_t *size_out) {
    
    return eb_remote_pull_delta(remote_name, path, data_out, size_out, false);
}

/* Prune old or unused data from a remote */
eb_status_t eb_remote_prune(
    const char *remote_name,
    const char *path,
    time_t older_than,
    bool dry_run) {
    
    if (!remote_name || !path) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    /* Find the remote configuration */
    pthread_mutex_lock(&remote_mutex);
    
    int remote_index = -1;
    for (int i = 0; i < remote_count; i++) {
        if (strcmp(remotes[i].name, remote_name) == 0) {
            remote_index = i;
            break;
        }
    }
    
    if (remote_index == -1) {
        pthread_mutex_unlock(&remote_mutex);
        DEBUG_ERROR("Remote '%s' not found", remote_name);
        return EB_ERROR_NOT_FOUND;
    }
    
    /* Make a copy of the remote configuration to avoid holding the lock */
    remote_config_t remote_config = remotes[remote_index];
    pthread_mutex_unlock(&remote_mutex);
    
    /* Construct the full URL with prune command */
    char full_url[1024];
    snprintf(full_url, sizeof(full_url), "%s/%s?prune=%ld&dry_run=%d", 
             remote_config.url, path, (long)older_than, dry_run ? 1 : 0);
    
    DEBUG_INFO("Pruning remote '%s' path '%s', older than %ld seconds%s", 
             remote_name, path, (long)older_than, 
             dry_run ? " (dry run)" : "");
    
    /* Open the transport */
    eb_transport_t *transport = transport_open(full_url);
    if (!transport) {
        DEBUG_ERROR("Failed to open transport for '%s'", full_url);
        return EB_ERROR_TRANSPORT;
    }
    
    /* Connect to the remote */
    int connect_result = transport_connect(transport);
    if (connect_result != EB_SUCCESS) {
        DEBUG_ERROR("Failed to connect to '%s': %s", 
                  full_url, transport_get_error(transport));
        transport_close(transport);
        return connect_result;
    }
    
    /* Send prune command */
    const char *prune_cmd = "PRUNE";
    eb_status_t result = transport_send_data(transport, prune_cmd, strlen(prune_cmd), NULL);
    if (result != EB_SUCCESS) {
        DEBUG_ERROR("Failed to send prune command: %s", 
                  transport_get_error(transport));
        transport_disconnect(transport);
        transport_close(transport);
        return result;
    }
    
    /* Receive response */
    char response[1024];
    size_t bytes_received = 0;
    
    result = transport_receive_data(transport, response, sizeof(response) - 1, &bytes_received);
    if (result != EB_SUCCESS) {
        DEBUG_ERROR("Failed to receive prune response: %s", 
                  transport_get_error(transport));
        transport_disconnect(transport);
        transport_close(transport);
        return result;
    }
    
    /* Null-terminate the response */
    response[bytes_received] = '\0';
    
    /* Parse the prune response */
    int pruned_count = 0;
    size_t pruned_bytes = 0;
    
    if (sscanf(response, "PRUNED %d FILES %zu BYTES", &pruned_count, &pruned_bytes) != 2) {
        DEBUG_ERROR("Invalid prune response format: %s", response);
        transport_disconnect(transport);
        transport_close(transport);
        return EB_ERROR_PROTOCOL;
    }
    
    /* Disconnect and cleanup */
    transport_disconnect(transport);
    transport_close(transport);
    
    if (dry_run) {
        DEBUG_INFO("Prune would remove %d files (%zu bytes)", 
                 pruned_count, pruned_bytes);
    } else {
        DEBUG_INFO("Pruned %d files (%zu bytes)", pruned_count, pruned_bytes);
    }
    
    return EB_SUCCESS;
}

/* Calculate a simple checksum for transfer validation */
static void calculate_checksum(const void *data, size_t size, char *checksum_out, size_t checksum_size) {
    /* This is a simple MD5-like algorithm for demonstration
     * In a real implementation, use a proper MD5 or SHA hash function
     */
    const unsigned char *bytes = (const unsigned char *)data;
    unsigned long hash = 5381;
    
    for (size_t i = 0; i < size; i++) {
        hash = ((hash << 5) + hash) + bytes[i];
    }
    
    snprintf(checksum_out, checksum_size, "%lx", hash);
}

/* Find an operation state by remote name and path */
static int find_operation(const char *remote_name, const char *path, int operation_type) {
    pthread_mutex_lock(&operation_mutex);
    
    for (int i = 0; i < operation_count; i++) {
        if (strcmp(operations[i].remote_name, remote_name) == 0 &&
            strcmp(operations[i].path, path) == 0 &&
            operations[i].operation_type == operation_type) {
            pthread_mutex_unlock(&operation_mutex);
            return i;
        }
    }
    
    pthread_mutex_unlock(&operation_mutex);
    return -1;
}

/* Start tracking a new operation */
static int start_operation(const char *remote_name, const char *path, 
                         size_t total_size, const void *data, int operation_type) {
    pthread_mutex_lock(&operation_mutex);
    
    /* Check if we have room for another operation */
    if (operation_count >= MAX_OPERATIONS) {
        /* Find and replace the oldest completed operation */
        int oldest_idx = -1;
        time_t oldest_time = time(NULL);
        
        for (int i = 0; i < operation_count; i++) {
            if (operations[i].completed && operations[i].last_update < oldest_time) {
                oldest_idx = i;
                oldest_time = operations[i].last_update;
            }
        }
        
        if (oldest_idx == -1) {
            /* No completed operations to replace */
            pthread_mutex_unlock(&operation_mutex);
            return -1;
        }
        
        /* Replace this operation */
        operation_state_t *op = &operations[oldest_idx];
        strncpy(op->remote_name, remote_name, sizeof(op->remote_name) - 1);
        strncpy(op->path, path, sizeof(op->path) - 1);
        op->total_size = total_size;
        op->transferred = 0;
        op->start_time = time(NULL);
        op->last_update = op->start_time;
        op->operation_type = operation_type;
        op->completed = false;
        
        /* Calculate checksum */
        if (data) {
            calculate_checksum(data, total_size, op->checksum, sizeof(op->checksum));
        } else {
            op->checksum[0] = '\0';
        }
        
        pthread_mutex_unlock(&operation_mutex);
        return oldest_idx;
    } else {
        /* Add new operation */
        operation_state_t *op = &operations[operation_count];
        strncpy(op->remote_name, remote_name, sizeof(op->remote_name) - 1);
        strncpy(op->path, path, sizeof(op->path) - 1);
        op->total_size = total_size;
        op->transferred = 0;
        op->start_time = time(NULL);
        op->last_update = op->start_time;
        op->operation_type = operation_type;
        op->completed = false;
        
        /* Calculate checksum */
        if (data) {
            calculate_checksum(data, total_size, op->checksum, sizeof(op->checksum));
        } else {
            op->checksum[0] = '\0';
        }
        
        int index = operation_count++;
        pthread_mutex_unlock(&operation_mutex);
        return index;
    }
}

/* Update operation progress */
static void update_operation(int op_idx, size_t transferred) {
    if (op_idx < 0 || op_idx >= operation_count) {
        return;
    }
    
    pthread_mutex_lock(&operation_mutex);
    operations[op_idx].transferred = transferred;
    operations[op_idx].last_update = time(NULL);
    pthread_mutex_unlock(&operation_mutex);
}

/* Mark operation as completed */
static void complete_operation(int op_idx) {
    if (op_idx < 0 || op_idx >= operation_count) {
        return;
    }
    
    pthread_mutex_lock(&operation_mutex);
    operations[op_idx].completed = true;
    operations[op_idx].last_update = time(NULL);
    pthread_mutex_unlock(&operation_mutex);
}

/* Verify data integrity using checksum */
static bool verify_data_integrity(const void *data, size_t size, const char *expected_checksum) {
    if (!data || !expected_checksum) {
        return false;
    }
    
    char actual_checksum[128];
    calculate_checksum(data, size, actual_checksum, sizeof(actual_checksum));
    
    return (strcmp(actual_checksum, expected_checksum) == 0);
}

/* Check if an operation can be resumed and get the resume position */
static size_t get_resume_position(const char *remote_name, const char *path, 
                                int operation_type, const void *data, size_t size) {
    int op_idx = find_operation(remote_name, path, operation_type);
    if (op_idx == -1) {
        return 0;  /* No resumable operation found */
    }
    
    pthread_mutex_lock(&operation_mutex);
    operation_state_t *op = &operations[op_idx];
    
    /* If operation was completed, can't resume */
    if (op->completed) {
        pthread_mutex_unlock(&operation_mutex);
        return 0;
    }
    
    /* If total size doesn't match, can't resume */
    if (op->total_size != size) {
        pthread_mutex_unlock(&operation_mutex);
        return 0;
    }
    
    /* For push operations, verify data integrity */
    if (operation_type == 0 && data) {
        char current_checksum[128];
        calculate_checksum(data, size, current_checksum, sizeof(current_checksum));
        
        if (strcmp(current_checksum, op->checksum) != 0) {
            /* Data has changed, can't resume */
            pthread_mutex_unlock(&operation_mutex);
            return 0;
        }
    }
    
    size_t resume_pos = op->transferred;
    pthread_mutex_unlock(&operation_mutex);
    
    return resume_pos;
}

/* Save operation states to persistent storage */
eb_status_t save_operation_states(const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        DEBUG_ERROR("Failed to open operation state file for writing: %s", filename);
        return EB_ERROR_IO;
    }
    
    pthread_mutex_lock(&operation_mutex);
    
    /* Write header */
    const char *header = "OPERATION_STATE_V1";
    fwrite(header, strlen(header), 1, f);
    
    /* Write count */
    fwrite(&operation_count, sizeof(operation_count), 1, f);
    
    /* Write operations */
    fwrite(operations, sizeof(operation_state_t), operation_count, f);
    
    pthread_mutex_unlock(&operation_mutex);
    
    fclose(f);
    return EB_SUCCESS;
}

/* Load operation states from persistent storage */
eb_status_t load_operation_states(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        /* Not an error if file doesn't exist yet */
        return EB_SUCCESS;
    }
    
    /* Read header */
    char header[32];
    if (fread(header, 18, 1, f) != 1) {
        fclose(f);
        return EB_ERROR_IO;
    }
    
    header[18] = '\0';
    if (strcmp(header, "OPERATION_STATE_V1") != 0) {
        DEBUG_ERROR("Invalid operation state file format");
        fclose(f);
        return EB_ERROR_FORMAT;
    }
    
    pthread_mutex_lock(&operation_mutex);
    
    /* Read count */
    int count;
    if (fread(&count, sizeof(count), 1, f) != 1) {
        pthread_mutex_unlock(&operation_mutex);
        fclose(f);
        return EB_ERROR_IO;
    }
    
    /* Validate count */
    if (count < 0 || count > MAX_OPERATIONS) {
        DEBUG_ERROR("Invalid operation count in state file: %d", count);
        pthread_mutex_unlock(&operation_mutex);
        fclose(f);
        return EB_ERROR_FORMAT;
    }
    
    /* Read operations */
    if (fread(operations, sizeof(operation_state_t), count, f) != count) {
        pthread_mutex_unlock(&operation_mutex);
        fclose(f);
        return EB_ERROR_IO;
    }
    
    operation_count = count;
    
    pthread_mutex_unlock(&operation_mutex);
    
    fclose(f);
    DEBUG_INFO("Loaded %d operation states", count);
    return EB_SUCCESS;
}

/* Resume an interrupted push operation */
eb_status_t eb_remote_resume_push(
    const char *remote_name,
    const void *data,
    size_t size,
    const char *path,
    const char *hash) {
    
    /* Check if we can resume */
    size_t resume_pos = get_resume_position(remote_name, path, 0, data, size);
    
    if (resume_pos == 0) {
        /* Can't resume, start a new operation */
        DEBUG_INFO("Cannot resume push operation, starting new transfer");
        return eb_remote_push(remote_name, data, size, path, hash);
    }
    
    DEBUG_INFO("Resuming push operation from position %zu/%zu (%.1f%%)", 
             resume_pos, size, (float)resume_pos * 100.0f / (float)size);
    
    /* Find the remote configuration */
    pthread_mutex_lock(&remote_mutex);
    
    int remote_index = -1;
    for (int i = 0; i < remote_count; i++) {
        if (strcmp(remotes[i].name, remote_name) == 0) {
            remote_index = i;
            break;
        }
    }
    
    if (remote_index == -1) {
        pthread_mutex_unlock(&remote_mutex);
        DEBUG_ERROR("Remote '%s' not found", remote_name);
        return EB_ERROR_NOT_FOUND;
    }
    
    /* Make a copy of the remote configuration to avoid holding the lock */
    remote_config_t remote_config = remotes[remote_index];
    pthread_mutex_unlock(&remote_mutex);
    
    /* Construct the full URL with resume command */
    char full_url[1024];
    snprintf(full_url, sizeof(full_url), "%s/%s?resume=%zu", 
             remote_config.url, path, resume_pos);
    
    /* Open the transport */
    eb_transport_t *transport = transport_open(full_url);
    if (!transport) {
        DEBUG_ERROR("Failed to open transport for '%s'", full_url);
        return EB_ERROR_TRANSPORT;
    }
    
    /* Connect to the remote */
    int connect_result = transport_connect(transport);
    if (connect_result != EB_SUCCESS) {
        DEBUG_ERROR("Failed to connect to '%s': %s", 
                  full_url, transport_get_error(transport));
        transport_close(transport);
        return connect_result;
    }
    
    /* Find operation index */
    int op_idx = find_operation(remote_name, path, 0);
    
    eb_status_t result = EB_SUCCESS;
    
    /* Skip already transferred data */
    const unsigned char *data_ptr = (const unsigned char *)data + resume_pos;
    size_t remaining = size - resume_pos;
    
    /* For large data, send in batches */
    size_t batch_number = (resume_pos + BATCH_SIZE - 1) / BATCH_SIZE;
    size_t total_batches = (size + BATCH_SIZE - 1) / BATCH_SIZE;
    
    /* Send resume header */
    char resume_header[256];
    snprintf(resume_header, sizeof(resume_header), 
             "RESUME %zu/%zu FROM %zu TOTAL %zu", 
             batch_number, total_batches, resume_pos, size);
    
    result = transport_send_data(transport, resume_header, strlen(resume_header), NULL);
    if (result != EB_SUCCESS) {
        DEBUG_ERROR("Failed to send resume header: %s", 
                  transport_get_error(transport));
        transport_disconnect(transport);
        transport_close(transport);
        return result;
    }
    
    /* Send remaining data in batches */
    while (remaining > 0 && result == EB_SUCCESS) {
        /* Determine current batch size */
        size_t current_batch_size = (remaining > BATCH_SIZE) ? BATCH_SIZE : remaining;
        
        /* Compress the batch */
        void *compressed_batch = NULL;
        size_t compressed_size = 0;
        
        result = compress_buffer(data_ptr, current_batch_size, 
                               remote_config.timeout, 
                               &compressed_batch, &compressed_size);
        if (result != EB_SUCCESS) {
            DEBUG_ERROR("Failed to compress batch %zu/%zu: %d", 
                      batch_number + 1, total_batches, result);
            transport_disconnect(transport);
            transport_close(transport);
            return result;
        }
        
        /* Send batch header with batch info */
        char batch_header[256];
        snprintf(batch_header, sizeof(batch_header), 
                 "BATCH %zu/%zu SIZE %zu COMPRESSED %zu", 
                 batch_number + 1, total_batches, 
                 current_batch_size, compressed_size);
        
        /* Encode batch header length at the start (4 bytes) */
        size_t header_len = strlen(batch_header);
        unsigned char header_len_bytes[4];
        header_len_bytes[0] = (header_len >> 24) & 0xFF;
        header_len_bytes[1] = (header_len >> 16) & 0xFF;
        header_len_bytes[2] = (header_len >> 8) & 0xFF;
        header_len_bytes[3] = header_len & 0xFF;
        
        /* Send header length */
        result = transport_send_data(transport, header_len_bytes, 4, NULL);
        if (result != EB_SUCCESS) {
            DEBUG_ERROR("Failed to send batch header length: %s", 
                      transport_get_error(transport));
            free(compressed_batch);
            transport_disconnect(transport);
            transport_close(transport);
            return result;
        }
        
        /* Send header */
        result = transport_send_data(transport, batch_header, header_len, NULL);
        if (result != EB_SUCCESS) {
            DEBUG_ERROR("Failed to send batch header: %s", 
                      transport_get_error(transport));
            free(compressed_batch);
            transport_disconnect(transport);
            transport_close(transport);
            return result;
        }
        
        /* Send the compressed batch with retries */
        int retry_count = 0;
        while (retry_count < MAX_RETRIES) {
            result = transport_send_data(transport, compressed_batch, compressed_size, NULL);
            if (result == EB_SUCCESS) {
                break;
            }
            
            DEBUG_WARN("Retry %d/%d: Failed to send batch %zu/%zu: %s", 
                        retry_count + 1, MAX_RETRIES, batch_number + 1, total_batches,
                        transport_get_error(transport));
            
            retry_count++;
            if (retry_count < MAX_RETRIES) {
                sleep_ms(RETRY_DELAY_MS);
            }
        }
        
        free(compressed_batch);
        
        if (result != EB_SUCCESS) {
            DEBUG_ERROR("Failed to send batch %zu/%zu after %d retries: %s", 
                      batch_number + 1, total_batches, MAX_RETRIES,
                      transport_get_error(transport));
            transport_disconnect(transport);
            transport_close(transport);
            return result;
        }
        
        /* Update progress */
        size_t total_transferred = resume_pos + (data_ptr - ((const unsigned char *)data - resume_pos)) + current_batch_size;
        update_operation(op_idx, total_transferred);
        
        DEBUG_INFO("Sent batch %zu/%zu (%.1f%%)", 
                 batch_number + 1, total_batches,
                 (float)(batch_number + 1) * 100.0f / (float)total_batches);
        
        /* Move to next batch */
        data_ptr += current_batch_size;
        remaining -= current_batch_size;
        batch_number++;
    }
    
    /* Send end marker */
    const char *end_marker = "END";
    result = transport_send_data(transport, end_marker, strlen(end_marker), NULL);
    if (result != EB_SUCCESS) {
        DEBUG_ERROR("Failed to send end marker: %s", 
                  transport_get_error(transport));
    } else {
        DEBUG_INFO("Resume push completed successfully: %zu bytes in %zu batches", 
                 size, total_batches);
        
        /* Mark operation as completed */
        complete_operation(op_idx);
    }
    
    /* Disconnect and cleanup */
    transport_disconnect(transport);
    transport_close(transport);
    
    return result;
}

/* List of resumable operations */
eb_status_t eb_remote_list_operations(
    char ***operation_list,
    size_t *count) {
    
    if (!operation_list || !count) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    pthread_mutex_lock(&operation_mutex);
    
    if (operation_count == 0) {
        *operation_list = NULL;
        *count = 0;
        pthread_mutex_unlock(&operation_mutex);
        return EB_SUCCESS;
    }
    
    /* Count incomplete operations */
    size_t incomplete_count = 0;
    for (int i = 0; i < operation_count; i++) {
        if (!operations[i].completed) {
            incomplete_count++;
        }
    }
    
    if (incomplete_count == 0) {
        *operation_list = NULL;
        *count = 0;
        pthread_mutex_unlock(&operation_mutex);
        return EB_SUCCESS;
    }
    
    /* Allocate array for operation descriptions */
    char **list = (char **)malloc(incomplete_count * sizeof(char *));
    if (!list) {
        pthread_mutex_unlock(&operation_mutex);
        return EB_ERROR_OUT_OF_MEMORY;
    }
    
    /* Fill the array */
    size_t idx = 0;
    for (int i = 0; i < operation_count; i++) {
        if (!operations[i].completed) {
            /* Format: "type:remote:path:size:transferred:progress%" */
            char *desc = (char *)malloc(512);
            if (!desc) {
                /* Clean up already allocated strings */
                for (size_t j = 0; j < idx; j++) {
                    free(list[j]);
                }
                free(list);
                pthread_mutex_unlock(&operation_mutex);
                return EB_ERROR_OUT_OF_MEMORY;
            }
            
            float progress = 0.0f;
            if (operations[i].total_size > 0) {
                progress = (float)operations[i].transferred * 100.0f / (float)operations[i].total_size;
            }
            
            snprintf(desc, 512, "%s:%s:%s:%zu:%zu:%.1f%%",
                     operations[i].operation_type == 0 ? "push" : "pull",
                     operations[i].remote_name,
                     operations[i].path,
                     operations[i].total_size,
                     operations[i].transferred,
                     progress);
            
            list[idx++] = desc;
        }
    }
    
    pthread_mutex_unlock(&operation_mutex);
    
    *operation_list = list;
    *count = incomplete_count;
    
    return EB_SUCCESS;
}

/* Implementation of the eb_remote_save_config function */
eb_status_t eb_remote_save_config(const char *config_dir) {
    char config_path[1024];
    
    if (config_dir) {
        snprintf(config_path, sizeof(config_path), "%s/config", config_dir);
    } else {
        snprintf(config_path, sizeof(config_path), ".embr/config");
    }
    
    /* Open the config file for writing */
    FILE *config = fopen(config_path, "r");
    char *config_content = NULL;
    size_t config_size = 0;
    
    if (config) {
        /* Read existing config file */
        fseek(config, 0, SEEK_END);
        config_size = ftell(config);
        fseek(config, 0, SEEK_SET);
        
        config_content = (char *)malloc(config_size + 1);
        if (!config_content) {
            fclose(config);
            return EB_ERROR_OUT_OF_MEMORY;
        }
        
        if (fread(config_content, 1, config_size, config) != config_size) {
            free(config_content);
            fclose(config);
            return EB_ERROR_IO;
        }
        
        config_content[config_size] = '\0';
        fclose(config);
    }
    
    /* Open the config file for writing */
    config = fopen(config_path, "w");
    if (!config) {
        free(config_content);
        return EB_ERROR_IO;
    }
    
    /* If we had existing content, write it back preserving non-remote sections */
    if (config_content) {
        char *line = config_content;
        char *next_line;
        bool in_remote_section = false;
        
        while (line && *line) {
            /* Find end of current line */
            next_line = strchr(line, '\n');
            if (next_line) {
                *next_line = '\0';
                next_line++;
            }
            
            /* Check if line is a section header */
            if (line[0] == '[') {
                /* Check if entering/leaving a remote section */
                in_remote_section = (strncmp(line, "[remote ", 8) == 0);
            }
            
            /* Only write non-remote sections */
            if (!in_remote_section) {
                fprintf(config, "%s\n", line);
            }
            
            line = next_line;
        }
        
        free(config_content);
    }
    
    /* Lock the remote registry while we iterate through it */
    pthread_mutex_lock(&remote_mutex);
    
    /* Write all remotes */
    for (int i = 0; i < remote_count; i++) {
        fprintf(config, "[remote \"%s\"]\n", remotes[i].name);
        fprintf(config, "    url = %s\n", remotes[i].url);
        
        if (remotes[i].token[0] != '\0') {
            /* Tokens should be saved in config.local for sensitive data */
            fprintf(config, "    # token is stored in config.local\n");
        }
        
        fprintf(config, "    timeout = %d\n", remotes[i].timeout);
        fprintf(config, "    verify_ssl = %s\n", remotes[i].verify_ssl ? "true" : "false");
        fprintf(config, "    format = %s\n", remotes[i].transformer_name);
        fprintf(config, "\n");
    }
    
    pthread_mutex_unlock(&remote_mutex);
    
    fclose(config);
    
    /* Save sensitive information separately in config.local */
    char config_local_path[1024];
    
    if (config_dir) {
        snprintf(config_local_path, sizeof(config_local_path), "%s/config.local", config_dir);
    } else {
        snprintf(config_local_path, sizeof(config_local_path), ".embr/config.local");
    }
    
    FILE *config_local = fopen(config_local_path, "w");
    if (config_local) {
        pthread_mutex_lock(&remote_mutex);
        
        for (int i = 0; i < remote_count; i++) {
            if (remotes[i].token[0] != '\0') {
                fprintf(config_local, "[remote \"%s\"]\n", remotes[i].name);
                fprintf(config_local, "    token = %s\n", remotes[i].token);
                fprintf(config_local, "\n");
            }
        }
        
        pthread_mutex_unlock(&remote_mutex);
        
        fclose(config_local);
        
        /* Set restrictive permissions on config.local */
        chmod(config_local_path, 0600);
    }
    
    DEBUG_INFO("Saved remote configuration to %s", config_path);
    return EB_SUCCESS;
}

/* Implementation of the eb_remote_load_config function */
eb_status_t eb_remote_load_config(const char *config_dir) {
    char config_path[1024];
    
    if (config_dir) {
        snprintf(config_path, sizeof(config_path), "%s/config", config_dir);
    } else {
        snprintf(config_path, sizeof(config_path), ".embr/config");
    }
    
    /* Open the config file for reading */
    FILE *config = fopen(config_path, "r");
    if (!config) {
        /* Not an error if file doesn't exist yet */
        return EB_SUCCESS;
    }
    
    /* Clear existing remotes */
    pthread_mutex_lock(&remote_mutex);
    remote_count = 0;
    pthread_mutex_unlock(&remote_mutex);
    
    /* Parse the config file */
    char line[1024];
    char current_remote[64] = {0};
    
    while (fgets(line, sizeof(line), config)) {
        /* Remove newline characters */
        size_t len = strlen(line);
        if (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }
        if (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }
        
        /* Skip empty lines */
        if (len == 0) {
            continue;
        }
        
        /* Check if line is a section header */
        if (line[0] == '[') {
            /* Extract section name */
            char *end = strchr(line + 1, ']');
            if (!end) {
                continue;  /* Invalid section header */
            }
            
            *end = '\0';
            
            /* Check if it's a remote section */
            if (strncmp(line + 1, "remote \"", 8) == 0) {
                /* Extract remote name */
                strncpy(current_remote, line + 9, sizeof(current_remote) - 1);
                
                /* Remove trailing quote */
                len = strlen(current_remote);
                if (len > 0 && current_remote[len-1] == '"') {
                    current_remote[len-1] = '\0';
                }
                
                /* Add a new remote entry */
                pthread_mutex_lock(&remote_mutex);
                
                /* Only add if we have room and it doesn't already exist */
                if (remote_count < MAX_REMOTES) {
                    bool found = false;
                    for (int i = 0; i < remote_count; i++) {
                        if (strcmp(remotes[i].name, current_remote) == 0) {
                            found = true;
                            break;
                        }
                    }
                    
                    if (!found) {
                        remote_config_t *remote = &remotes[remote_count++];
                        strncpy(remote->name, current_remote, sizeof(remote->name) - 1);
                        remote->url[0] = '\0';
                        remote->token[0] = '\0';
                        remote->timeout = 30;  /* Default: 30 seconds */
                        remote->verify_ssl = true;
                        strncpy(remote->transformer_name, "json", sizeof(remote->transformer_name) - 1);
                    }
                }
                
                pthread_mutex_unlock(&remote_mutex);
            } else {
                /* Not a remote section */
                current_remote[0] = '\0';
            }
        } else if (current_remote[0] != '\0') {
            /* We're in a remote section, parse the key-value pair */
            char *key = line;
            while (*key == ' ' || *key == '\t') {
                key++;  /* Skip leading whitespace */
            }
            
            char *equals = strchr(key, '=');
            if (!equals) {
                continue;  /* Invalid key-value format */
            }
            
            *equals = '\0';
            char *value = equals + 1;
            
            /* Trim key */
            char *end = equals - 1;
            while (end > key && (*end == ' ' || *end == '\t')) {
                *end-- = '\0';
            }
            
            /* Trim value */
            while (*value == ' ' || *value == '\t') {
                value++;
            }
            
            end = value + strlen(value) - 1;
            while (end > value && (*end == ' ' || *end == '\t')) {
                *end-- = '\0';
            }
            
            /* Find the remote entry */
            pthread_mutex_lock(&remote_mutex);
            
            int remote_index = -1;
            for (int i = 0; i < remote_count; i++) {
                if (strcmp(remotes[i].name, current_remote) == 0) {
                    remote_index = i;
                    break;
                }
            }
            
            if (remote_index != -1) {
                /* Update the remote configuration */
                if (strcmp(key, "url") == 0) {
                    strncpy(remotes[remote_index].url, value, sizeof(remotes[remote_index].url) - 1);
                } else if (strcmp(key, "token") == 0) {
                    strncpy(remotes[remote_index].token, value, sizeof(remotes[remote_index].token) - 1);
                } else if (strcmp(key, "timeout") == 0) {
                    remotes[remote_index].timeout = atoi(value);
                } else if (strcmp(key, "verify_ssl") == 0) {
                    remotes[remote_index].verify_ssl = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
                } else if (strcmp(key, "format") == 0) {
                    strncpy(remotes[remote_index].transformer_name, value, sizeof(remotes[remote_index].transformer_name) - 1);
                }
            }
            
            pthread_mutex_unlock(&remote_mutex);
        }
    }
    
    fclose(config);
    
    /* Load sensitive information from config.local */
    char config_local_path[1024];
    
    if (config_dir) {
        snprintf(config_local_path, sizeof(config_local_path), "%s/config.local", config_dir);
    } else {
        snprintf(config_local_path, sizeof(config_local_path), ".embr/config.local");
    }
    
    FILE *config_local = fopen(config_local_path, "r");
    if (config_local) {
        /* Similar parsing as above but only for tokens */
        current_remote[0] = '\0';
        
        while (fgets(line, sizeof(line), config_local)) {
            /* Remove newline characters */
            size_t len = strlen(line);
            if (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
                line[--len] = '\0';
            }
            if (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
                line[--len] = '\0';
            }
            
            /* Skip empty lines */
            if (len == 0) {
                continue;
            }
            
            /* Check if line is a section header */
            if (line[0] == '[') {
                /* Extract section name */
                char *end = strchr(line + 1, ']');
                if (!end) {
                    continue;  /* Invalid section header */
                }
                
                *end = '\0';
                
                /* Check if it's a remote section */
                if (strncmp(line + 1, "remote \"", 8) == 0) {
                    /* Extract remote name */
                    strncpy(current_remote, line + 9, sizeof(current_remote) - 1);
                    
                    /* Remove trailing quote */
                    len = strlen(current_remote);
                    if (len > 0 && current_remote[len-1] == '"') {
                        current_remote[len-1] = '\0';
                    }
                } else {
                    /* Not a remote section */
                    current_remote[0] = '\0';
                }
            } else if (current_remote[0] != '\0') {
                /* We're in a remote section, parse the key-value pair */
                char *key = line;
                while (*key == ' ' || *key == '\t') {
                    key++;  /* Skip leading whitespace */
                }
                
                char *equals = strchr(key, '=');
                if (!equals) {
                    continue;  /* Invalid key-value format */
                }
                
                *equals = '\0';
                char *value = equals + 1;
                
                /* Trim key */
                char *end = equals - 1;
                while (end > key && (*end == ' ' || *end == '\t')) {
                    *end-- = '\0';
                }
                
                /* Trim value */
                while (*value == ' ' || *value == '\t') {
                    value++;
                }
                
                end = value + strlen(value) - 1;
                while (end > value && (*end == ' ' || *end == '\t')) {
                    *end-- = '\0';
                }
                
                /* Only update tokens */
                if (strcmp(key, "token") == 0) {
                    pthread_mutex_lock(&remote_mutex);
                    
                    for (int i = 0; i < remote_count; i++) {
                        if (strcmp(remotes[i].name, current_remote) == 0) {
                            strncpy(remotes[i].token, value, sizeof(remotes[i].token) - 1);
                            break;
                        }
                    }
                    
                    pthread_mutex_unlock(&remote_mutex);
                }
            }
        }
        
        fclose(config_local);
    }
    
    /* Count remotes loaded */
    pthread_mutex_lock(&remote_mutex);
    int count = remote_count;
    pthread_mutex_unlock(&remote_mutex);
    
    DEBUG_INFO("Loaded %d remotes from configuration", count);
    return EB_SUCCESS;
}

/* Helper function to get the target format for a remote */
static const char* get_remote_target_format(const char* remote_name) {
    if (!remote_name) {
        return NULL;
    }
    
    /* Lock the remote configuration */
    pthread_mutex_lock(&remote_mutex);
    
    const char* result = NULL;
    
    /* Find the remote in the configuration */
    for (int i = 0; i < remote_count; i++) {
        if (strcmp(remotes[i].name, remote_name) == 0) {
            /* Check for a target_format field in the config */
            if (remotes[i].target_format[0] != '\0') {
                result = remotes[i].target_format;
            } else if (remotes[i].transformer_name[0] != '\0') {
                /* Fall back to transformer_name if target_format is not set */
                result = remotes[i].transformer_name;
            } else {
                /* Default to "parquet" if no format is specified */
                result = "parquet";
            }
            break;
        }
    }
    
    /* Unlock the remote configuration */
    pthread_mutex_unlock(&remote_mutex);
    
    return result;
}

// List all files (hashes) in a remote set path
// Returns array of strings (caller must free each string and the array)
eb_status_t eb_remote_list_files(const char *remote_name, const char *set_path, char ***files_out, size_t *count_out) {
    if (!remote_name || !set_path || !files_out || !count_out) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    *files_out = NULL;
    *count_out = 0;
    pthread_mutex_lock(&remote_mutex);
    int remote_index = -1;
    for (int i = 0; i < remote_count; i++) {
        if (strcmp(remotes[i].name, remote_name) == 0) {
            remote_index = i;
            break;
        }
    }
    if (remote_index == -1) {
        pthread_mutex_unlock(&remote_mutex);
        return EB_ERROR_NOT_FOUND;
    }
    remote_config_t remote_config = remotes[remote_index];
    pthread_mutex_unlock(&remote_mutex);
    char full_url[1024];
    snprintf(full_url, sizeof(full_url), "%s/%s", remote_config.url, set_path);
    eb_transport_t *transport = transport_open(full_url);
    if (!transport) {
        return EB_ERROR_TRANSPORT;
    }
    int connect_result = transport_connect(transport);
    if (connect_result != EB_SUCCESS) {
        transport_close(transport);
        return connect_result;
    }
    char **refs = NULL;
    size_t ref_count = 0;
    if (!transport->ops || !transport->ops->list_refs) {
        transport_disconnect(transport);
        transport_close(transport);
        return EB_ERROR_NOT_IMPLEMENTED;
    }
    int list_result = transport->ops->list_refs(transport, &refs, &ref_count);
    transport_disconnect(transport);
    transport_close(transport);
    if (list_result != EB_SUCCESS) {
        return list_result;
    }
    *files_out = refs;
    *count_out = ref_count;
    return EB_SUCCESS;
}

// Delete files from a remote set path
// files: array of file names (relative to set_path), count: number of files
eb_status_t eb_remote_delete_files(const char *remote_name, const char *set_path, const char **files, size_t count) {
    if (!remote_name || !set_path || !files || count == 0) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    pthread_mutex_lock(&remote_mutex);
    int remote_index = -1;
    for (int i = 0; i < remote_count; i++) {
        if (strcmp(remotes[i].name, remote_name) == 0) {
            remote_index = i;
            break;
        }
    }
    if (remote_index == -1) {
        pthread_mutex_unlock(&remote_mutex);
        return EB_ERROR_NOT_FOUND;
    }
    remote_config_t remote_config = remotes[remote_index];
    pthread_mutex_unlock(&remote_mutex);
    char full_url[1024];
    snprintf(full_url, sizeof(full_url), "%s/%s", remote_config.url, set_path);
    eb_transport_t *transport = transport_open(full_url);
    if (!transport) {
        return EB_ERROR_TRANSPORT;
    }
    int connect_result = transport_connect(transport);
    if (connect_result != EB_SUCCESS) {
        transport_close(transport);
        return connect_result;
    }
    if (!transport->ops || !transport->ops->delete_refs) {
        transport_disconnect(transport);
        transport_close(transport);
        return EB_ERROR_NOT_IMPLEMENTED;
    }
    int delete_result = transport->ops->delete_refs(transport, files, count);
    transport_disconnect(transport);
    transport_close(transport);
    return delete_result;
}