/*
 * EmbeddingBridge - S3 Transport Implementation
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
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>
#include <time.h>
#include <jansson.h>
#include <pthread.h>
#include <dirent.h>

#include "transport.h"
#include "error.h"
#include "debug.h"
#include "transformer.h"
#include "path_utils.h"
#include "json_transformer.h"

/* AWS SDK includes */
#include <aws/common/common.h>
#include <aws/auth/credentials.h>
#include <aws/auth/signing.h>
#include <aws/s3/s3.h>
#include <aws/s3/s3_client.h>
#include <aws/s3/private/s3_list_objects.h>
#include <aws/http/http.h>
#include <aws/io/uri.h>
#include <aws/io/event_loop.h>
#include <aws/io/host_resolver.h>
#include <aws/io/channel_bootstrap.h>
#include <aws/io/tls_channel_handler.h>
#include <aws/common/condition_variable.h>
#include <aws/common/mutex.h>
#include <aws/common/zero.h>
#include <aws/http/request_response.h>
#include <aws/http/connection.h>
#include <aws/io/stream.h>
#include <aws/common/array_list.h>

/* Add EB_ERROR_CONNECTION definition if not defined */
#ifndef EB_ERROR_CONNECTION
#define EB_ERROR_CONNECTION EB_ERROR_GENERIC
#endif

/* Add GNU stack attribute to prevent executable stack warning */
#if defined(__linux__) && defined(__ELF__)
__asm__(".section .note.GNU-stack,\"\",%progbits");
#endif

/* Helper function to get minimum of two values */
#ifndef min
#define min(a,b) (((a)<(b))?(a):(b))
#endif

#ifdef EB_HAVE_AWS

/* S3 transport data structure */
struct s3_data {
    /* AWS SDK variables */
    struct aws_allocator *allocator;
    struct aws_s3_client *s3_client;
    struct aws_string *endpoint;
    struct aws_tls_connection_options *tls_connection_options;
    struct aws_client_bootstrap *bootstrap;
    struct aws_host_resolver *resolver; 
    struct aws_event_loop_group *event_loop_group;
    
    /* S3 specific variables */
    char *bucket;
    char *prefix;
    char *region;
    /* Add signing config storage to support paginator API */
    struct aws_signing_config_aws signing_config;
    
    bool is_connected;
};

/* Initialize S3 for the transport layer */
struct aws_mutex s_mutex = AWS_MUTEX_INIT;
struct aws_condition_variable s_cvar = AWS_CONDITION_VARIABLE_INIT;

/* Helper function to process events during waits */
static void process_events_while_waiting(struct s3_data *s3, int milliseconds) {
    /* Sleep briefly to allow other events to be processed */
    (void)s3; /* Unused parameter */
    DEBUG_INFO("Processing events for %d ms", milliseconds);
    usleep(milliseconds * 1000);
}

/* S3 operation completion and data context */
struct s3_operation_context {
    struct aws_mutex *lock;
    struct aws_condition_variable *signal;
    int error_code;
    bool is_done;
};

/* Check if an operation is done */
static bool s_is_operation_done(void *arg) {
    struct s3_operation_context *context = arg;
    return context->is_done;
}

/* Callback for S3 operations completion */
static void s3_on_s3_operation_finished(
    struct aws_s3_meta_request *meta_request,
    const struct aws_s3_meta_request_result *meta_request_result,
    void *user_data) 
{
    (void)meta_request;
    struct s3_operation_context *context = (struct s3_operation_context *)user_data;
    
    /* Extract and log more diagnostic info */
    int error_code = meta_request_result->error_code;
    uint32_t response_status = 0;
    
    if (meta_request_result->response_status) {
        response_status = meta_request_result->response_status;
    }
    
    DEBUG_INFO("S3 operation finished with error code: %d, HTTP status: %u", 
               error_code, response_status);
    
    if (error_code != AWS_ERROR_SUCCESS) {
        DEBUG_ERROR("S3 error: %s", aws_error_debug_str(error_code));
    }
    
    aws_mutex_lock(context->lock);
    context->error_code = error_code;
    context->is_done = true;
    aws_condition_variable_notify_one(context->signal);
    aws_mutex_unlock(context->lock);
    
    DEBUG_INFO("S3 operation context updated and signaled");
}

/* S3 List Objects callback - handles callback for each object found during listing */
static int s3_on_list_object(const struct aws_s3_object_info *info, void *user_data) {
    // We don't need to do anything with each individual object for now
    (void)info;
    (void)user_data;
    return AWS_OP_SUCCESS;
}

/* S3 List Objects completion callback - handles completion of a listing operation */
static void s3_on_list_finished(struct aws_s3_paginator *paginator, int error_code, void *user_data) {
    struct s3_operation_context *context = (struct s3_operation_context *)user_data;
    
    aws_mutex_lock(context->lock);
    context->error_code = error_code;
    context->is_done = true;
    aws_condition_variable_notify_one(context->signal);
    aws_mutex_unlock(context->lock);
}

/* S3 send data implementation using proper AWS S3 SDK patterns */
static int s3_send_data(eb_transport_t *transport, const void *data, size_t size, const char *hash) {
    DEBUG_INFO("s3_send_data called with transport=%p, data=%p, size=%zu, hash=%s", 
              transport, data, size, hash ? hash : "(null)");
    
    if (!transport) {
        DEBUG_ERROR("s3_send_data: transport parameter is NULL");
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    if (!transport->data) {
        DEBUG_ERROR("s3_send_data: transport->data is NULL");
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    if (!data) {
        DEBUG_ERROR("s3_send_data: data parameter is NULL");
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    if (size == 0) {
        DEBUG_ERROR("s3_send_data: size parameter is 0");
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    if (!hash || !*hash) {
        DEBUG_ERROR("s3_send_data: hash parameter is NULL or empty");
        eb_set_error(EB_ERROR_INVALID_PARAMETER, "No hash provided for S3 upload");
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    struct s3_data *s3 = (struct s3_data *)transport->data;
    
    if (!s3->is_connected) {
        DEBUG_ERROR("s3_send_data: Not connected to S3");
        snprintf(transport->error_msg, sizeof(transport->error_msg),
                "Not connected to S3");
        return EB_ERROR_NOT_CONNECTED;
    }
    
    DEBUG_INFO("Sending %zu bytes to S3 bucket '%s' with prefix '%s' and hash '%s'", 
               size, s3->bucket, s3->prefix, hash);
    
    // Add debug log for target_path
    DEBUG_INFO("s3_send_data: transport->target_path = '%s'", transport->target_path ? transport->target_path : "(null)");
    
    /* Check the content of the first few bytes for debugging */
    const unsigned char *bytes = (const unsigned char *)data;
    if (size >= 8) {
        DEBUG_INFO("First 8 bytes: %02x %02x %02x %02x %02x %02x %02x %02x",
                bytes[0], bytes[1], bytes[2], bytes[3], 
                bytes[4], bytes[5], bytes[6], bytes[7]);
    }
    
    /* Determine set name from transport->target_path, defaulting to "main" */
    char set_name_buf[256] = {0};
    if (transport->target_path) {
        const char *tp = transport->target_path;
        const char *last = strrchr(tp, '/');
        if (last && last[1]) {
            strncpy(set_name_buf, last + 1, sizeof(set_name_buf) - 1);
        } else {
            strncpy(set_name_buf, tp, sizeof(set_name_buf) - 1);
        }
    }
    if (set_name_buf[0] == '\0') {
        strncpy(set_name_buf, "main", sizeof(set_name_buf) - 1);
    }
    
    
    /* Transform the data from .embr format to parquet format using the transformer */
    void *transformed_data = NULL;
    size_t transformed_size = 0;
    bool need_to_free_transformed = false;
    
    /* Check if data is already pre-compressed */
    if (transport->data_is_precompressed) {
        DEBUG_WARN("Data is already pre-compressed, skipping transformation");
        DEBUG_WARN("data_is_precompressed flag is set to TRUE");
        
        /* Check for ZSTD magic number */
        const unsigned char* bytes = (const unsigned char *)data;
        if (size >= 4 && 
            bytes[0] == 0x28 && bytes[1] == 0xB5 && 
            bytes[2] == 0x2F && bytes[3] == 0xFD) {
            DEBUG_INFO("ZSTD magic number detected in pre-compressed data");
        }
        
        /* Use the pre-compressed data directly */
        transformed_data = (void *)data;
        transformed_size = size;
        need_to_free_transformed = false;
    } else {
        /* Transform using the proper format transformer */
        eb_transformer_t *transformer = eb_find_transformer_by_format("parquet");
        if (!transformer) {
            DEBUG_ERROR("Failed to find Parquet transformer");
            snprintf(transport->error_msg, sizeof(transport->error_msg),
                    "Failed to find Parquet transformer");
            return EB_ERROR_TRANSFORMER;
        }
        
        eb_status_t transform_result = eb_transform(
            transformer,
            data, size, 
            &transformed_data, &transformed_size);
            
        if (transform_result != EB_SUCCESS) {
            DEBUG_ERROR("Failed to transform data to Parquet format: %d", transform_result);
            snprintf(transport->error_msg, sizeof(transport->error_msg),
                    "Failed to transform data to Parquet format: %d", transform_result);
            return transform_result;
        }
        
        need_to_free_transformed = true;
        DEBUG_INFO("Transformed data from %zu to %zu bytes", size, transformed_size);
    }
    
    /* For the S3 keys, we need both data and metadata paths */
    char s3_data_key[1024];
    char s3_metadata_key[1024];
    
    /* Get current time for use as fallback */
    time_t now = time(NULL);
    
    /* Extract the original document name from the path */
    char document_name[128] = {0};
    const char *last_slash = NULL;
    
    if (transport->target_path) {
        last_slash = strrchr(transport->target_path, '/');
        if (last_slash) {
            strncpy(document_name, last_slash + 1, sizeof(document_name) - 1);
        } else {
            strncpy(document_name, transport->target_path, sizeof(document_name) - 1);
        }
    } else {
        strcpy(document_name, "unknown-document");
    }
    
    /* Clean the document name of characters that shouldn't be in filenames */
    for (char *p = document_name; *p; p++) {
        if (*p == ' ' || *p == '/' || *p == '\\' || *p == ':' || *p == '*' || 
            *p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|') {
            *p = '_';
        }
    }
    
    /* Get the model name */
    char model_name[128] = "unknown-model";
    
    /* Try to extract from local metadata */
    if (transport->target_path) {
        /* Look in .embr/objects directory for metadata files */
        DIR *dir;
        struct dirent *entry;
        
        dir = opendir(".embr/objects");
        if (dir) {
            DEBUG_INFO("Looking for metadata files in .embr/objects directory");
            
            /* Extract document name from target path */
            const char *target_filename = strrchr(transport->target_path, '/');
            if (target_filename) {
                target_filename++; /* Skip the slash */
            } else {
                target_filename = transport->target_path;
            }
            
            DEBUG_INFO("Looking for metadata for file: %s", target_filename);
            
            char* index_path = get_current_set_index_path();
            FILE *index_file = index_path ? fopen(index_path, "r") : NULL;
            if (index_file) {
                char line[1024];
                while (fgets(line, sizeof(line), index_file)) {
                    /* Remove newline */
                    size_t len = strlen(line);
                    if (len > 0 && line[len-1] == '\n') {
                        line[len-1] = '\0';
                    }
                    
                    char file_hash[128] = {0};
                    char file_path[896] = {0};
                    
                    /* Format is: hash filename */
                    if (sscanf(line, "%127s %895s", file_hash, file_path) != 2) {
                        continue;
                    }
                    
                    /* Find the hash we're currently processing based on the raw file */
                    char raw_path[1024];
                    snprintf(raw_path, sizeof(raw_path), ".embr/objects/%s.raw", file_hash);
                    
                    struct stat st;
                    if (stat(raw_path, &st) == 0) {
                        /* This is a valid file, check if our current data matches the size */
                        if (st.st_size == size) {
                            /* This is most likely our file, store its hash for metadata lookup */
                            strncpy(hash, file_hash, sizeof(hash) - 1);
                            DEBUG_INFO("Found likely matching hash in index: %s for file %s (size: %zu)", 
                                       hash, file_path, (size_t)st.st_size);
                            
                            /* Store the original source file path */
                            char original_source_file[PATH_MAX] = {0};
                            strncpy(original_source_file, file_path, sizeof(original_source_file) - 1);
                            
                            /* Extract the original document name from the source file */
                            if (original_source_file[0] != '\0') {
                                const char *src_basename = strrchr(original_source_file, '/');
                                if (src_basename) {
                                    strncpy(document_name, src_basename + 1, sizeof(document_name) - 1);
                                } else {
                                    strncpy(document_name, original_source_file, sizeof(document_name) - 1);
                                }
                                
                                /* Clean document name */
                                for (char *p = document_name; *p; p++) {
                                    if (*p == ' ' || *p == '/' || *p == '\\' || *p == ':' || 
                                        *p == '*' || *p == '?' || *p == '"' || *p == '<' || 
                                        *p == '>' || *p == '|') {
                                        *p = '_';
                                    }
                                }
                                
                                DEBUG_INFO("Using document name from index: %s", document_name);
                            }
                            
                            /* Now load the metadata for this hash */
                            char meta_path[PATH_MAX];
                            snprintf(meta_path, sizeof(meta_path), ".embr/objects/%s.meta", hash);
                            
                            DEBUG_INFO("Reading metadata from: %s", meta_path);
                            FILE *meta_file = fopen(meta_path, "r");
                            if (meta_file) {
                                time_t file_timestamp = 0;
                                char provider[128] = {0};
                                char source_file_path[PATH_MAX] = {0}; // Store the source file path
                                
                                char meta_line[1024];
                                while (fgets(meta_line, sizeof(meta_line), meta_file)) {
                                    /* Look for provider field */
                                    if (strncmp(meta_line, "model=", 6) == 0) {
                                        strncpy(provider, meta_line + 6, sizeof(provider) - 1);
                                        
                                        /* Remove newline if present */
                                        char *newline = strchr(provider, '\n');
                                        if (newline) *newline = '\0';
                                        
                                        DEBUG_INFO("Found provider in metadata: %s", provider);
                                    }
                                    
                                    /* Look for timestamp field */
                                    if (strncmp(meta_line, "timestamp=", 10) == 0) {
                                        file_timestamp = atol(meta_line + 10);
                                        DEBUG_INFO("Found timestamp in metadata: %ld", (long)file_timestamp);
                                    }
                                    
                                    /* Look for source file field */
                                    if (strncmp(meta_line, "source_file=", 12) == 0) {
                                        strncpy(source_file_path, meta_line + 12, sizeof(source_file_path) - 1);
                                        
                                        /* Remove newline if present */
                                        char *newline = strchr(source_file_path, '\n');
                                        if (newline) *newline = '\0';
                                        
                                        DEBUG_INFO("Found source file in metadata: %s", source_file_path);
                                    }
                                }
                                
                                fclose(meta_file);
                                
                                /* Load source document text for blob field if source file exists */
                                if (source_file_path[0] != '\0') {
                                    FILE *source_file = fopen(source_file_path, "r");
                                    if (source_file) {
                                        DEBUG_INFO("Reading document text from source: %s", source_file_path);
                                        
                                        /* Determine file size */
                                        fseek(source_file, 0, SEEK_END);
                                        long source_size = ftell(source_file);
                                        fseek(source_file, 0, SEEK_SET);
                                        
                                        /* Allocate buffer for document text */
                                        char *document_text = (char*)malloc(source_size + 1);
                                        if (document_text) {
                                            size_t bytes_read = fread(document_text, 1, source_size, source_file);
                                            document_text[bytes_read] = '\0';
                                            
                                            /* Set document text for Parquet transformer */
                                            extern void eb_parquet_set_document_text(const char* text);
                                            DEBUG_INFO("Setting document text for blob field (%zu bytes)", bytes_read);
                                            eb_parquet_set_document_text(document_text);
                                            
                                            free(document_text);
                                        } else {
                                            DEBUG_ERROR("Failed to allocate memory for document text");
                                        }
                                        
                                        fclose(source_file);
                                    } else {
                                        DEBUG_WARN("Could not open source file: %s", source_file_path);
                                    }
                                }
                                
                                /* Use the metadata information */
                                if (file_timestamp > 0) {
                                    now = file_timestamp;
                                    DEBUG_INFO("Using local storage timestamp: %ld", (long)now);
                                } else {
                                    DEBUG_ERROR("Unable to find local storage timestamp for document embedding");
                                    snprintf(transport->error_msg, sizeof(transport->error_msg),
                                            "Unable to find local storage timestamp for document embedding");
                                    
                                    /* Free transformed data if needed */
                                    if (need_to_free_transformed) {
                                        free(transformed_data);
                                    }
                                    
                                    if (index_file) {
                                        fclose(index_file);
                                    }
                                    if (index_path) free(index_path);
                                    closedir(dir);
                                    return EB_ERROR_INVALID_DATA;
                                }
                                
                                if (provider[0] != '\0') {
                                    strncpy(model_name, provider, sizeof(model_name) - 1);
                                    
                                    /* Clean model name */
                                    for (char *p = model_name; *p; p++) {
                                        if (*p == ' ' || *p == '/' || *p == '\\' || *p == ':' || 
                                            *p == '*' || *p == '?' || *p == '"' || *p == '<' || 
                                            *p == '>' || *p == '|') {
                                            *p = '_';
                                        }
                                    }
                                    
                                    DEBUG_INFO("Using model name from metadata: %s", model_name);
                                }
                                
                                /* We've processed this file, can break the loop */
                                break;
                            }
                        }
                    }
                }
                
                fclose(index_file);
            }
            if (index_path) free(index_path);
            
            closedir(dir);
        } else {
            DEBUG_WARN("Couldn't open .embr/objects directory");
        }
    }
    
    /* Construct the key with format: <hash>.parquet */
    char base_path[512] = {0};
    if (s3->prefix && s3->prefix[0]) {
        strncpy(base_path, s3->prefix, sizeof(base_path) - 1);
    }
    /* Normalize base path (remove trailing slash) */
    size_t base_len = strlen(base_path);
    if (base_len > 0 && base_path[base_len - 1] == '/') {
        base_path[base_len - 1] = '\0';
    }
    /* Hash-based naming of Parquet data and metadata */
    if (base_path[0]) {
        if (strstr(base_path, "/documents") != NULL) {
            snprintf(s3_data_key, sizeof(s3_data_key), "%s/%s.parquet", base_path, hash);
        } else {
            snprintf(s3_data_key, sizeof(s3_data_key), "%s/documents/%s.parquet", base_path, hash);
        }
        snprintf(s3_metadata_key, sizeof(s3_metadata_key), "%s/metadata.json", base_path);
    } else {
        snprintf(s3_data_key, sizeof(s3_data_key), "sets/%s/documents/%s.parquet",
                 set_name_buf, hash);
        snprintf(s3_metadata_key, sizeof(s3_metadata_key), "sets/%s/metadata.json",
                 set_name_buf);
    }
    DEBUG_INFO("S3 key for data: %s", s3_data_key);
    DEBUG_INFO("S3 key for metadata: %s", s3_metadata_key);
    DEBUG_INFO("Full S3 location: s3://%s/%s (for data), s3://%s/%s (for metadata)",
             s3->bucket, s3_data_key, s3->bucket, s3_metadata_key);
    
    /* S3 doesn't require directory checks before upload */
    DEBUG_INFO("Directories will be created implicitly by S3 during uploads");
    
    /* Create temporary file for data */
    char temp_file_path[PATH_MAX];
    snprintf(temp_file_path, sizeof(temp_file_path), "/tmp/eb_s3_upload_data_XXXXXX");
    int tmp_fd = mkstemp(temp_file_path);
    if (tmp_fd < 0) {
            snprintf(transport->error_msg, sizeof(transport->error_msg),
                "Failed to create temporary file");
        return EB_ERROR_IO;
    }

    /* Write the transformed data to the temporary file */
    ssize_t write_result = write(tmp_fd, transformed_data, transformed_size);
    close(tmp_fd);

    if (write_result != (ssize_t)transformed_size) {
        DEBUG_ERROR("Failed to write to temporary file: %s", strerror(errno));
        snprintf(transport->error_msg, sizeof(transport->error_msg),
                "Failed to write to temporary file: %s", strerror(errno));
        unlink(temp_file_path);
        if (need_to_free_transformed) {
            free(transformed_data);
        }
        return EB_ERROR_FILE_IO;
    }

    DEBUG_INFO("Wrote %zd bytes to temporary file", write_result);

    /* We can free the transformed data now as it's been written to disk */
    if (need_to_free_transformed) {
        free(transformed_data);
    }
    
    /* Create a HTTP PUT message for the data object */
    struct aws_http_message *data_message = aws_http_message_new_request(aws_default_allocator());
    if (!data_message) {
        DEBUG_ERROR("Failed to create PUT message for data object");
        snprintf(transport->error_msg, sizeof(transport->error_msg),
                "Failed to create PUT message for data object");
        unlink(temp_file_path);
        return EB_ERROR_GENERIC;
    }
    
    /* Setup the PUT request */
    aws_http_message_set_request_method(data_message, aws_http_method_put);
    
    /* Set the full path in the URI including bucket name */
    char uri_buffer[2048];
    snprintf(uri_buffer, sizeof(uri_buffer), "/%s", s3_data_key);
    struct aws_byte_cursor uri_cursor = aws_byte_cursor_from_c_str(uri_buffer);
    aws_http_message_set_request_path(data_message, uri_cursor);
    
    /* Set up host header in format: BUCKET.s3.REGION.amazonaws.com */
    char host_header_value[256];
    snprintf(host_header_value, sizeof(host_header_value), "%s.s3.%s.amazonaws.com", 
            s3->bucket, s3->region);
    
    struct aws_http_header host_header = {
        .name = aws_byte_cursor_from_c_str("Host"),
        .value = aws_byte_cursor_from_c_str(host_header_value),
    };
    aws_http_message_add_header(data_message, host_header);
    
    /* Add content type header */
    struct aws_http_header content_type_header = {
        .name = aws_byte_cursor_from_c_str("Content-Type"),
        .value = aws_byte_cursor_from_c_str("application/octet-stream"),
    };
    aws_http_message_add_header(data_message, content_type_header);
    
    /* Log the full request details */
    DEBUG_INFO("Sending PUT request to Host: %s, Path: %s", host_header_value, uri_buffer);
    
    /* Setup synchronization primitives for upload operation */
    struct s3_operation_context upload_context = {
        .lock = &s_mutex,
        .signal = &s_cvar,
        .error_code = 0,
        .is_done = false
    };
    
    /* Create the options for the data upload */
    struct aws_s3_meta_request_options data_options = {
        .type = AWS_S3_META_REQUEST_TYPE_PUT_OBJECT,
        .message = data_message, 
        .send_filepath = aws_byte_cursor_from_c_str(temp_file_path),
        .user_data = &upload_context,
        .finish_callback = s3_on_s3_operation_finished
    };

    DEBUG_INFO("Preparing to upload data file to S3: %s", s3_data_key);

    /* Send the data to S3 */
    struct aws_s3_meta_request *data_request = aws_s3_client_make_meta_request(s3->s3_client, &data_options);
    if (!data_request) {
        DEBUG_ERROR("Failed to create S3 meta request for data upload: %s", aws_error_str(aws_last_error()));
        snprintf(transport->error_msg, sizeof(transport->error_msg),
                "Failed to create S3 meta request for data upload: %s", aws_error_str(aws_last_error()));
        aws_http_message_release(data_message);
        unlink(temp_file_path);
        return EB_ERROR_GENERIC;
    }

    DEBUG_INFO("Waiting for data upload to complete...");

    /* Wait for the upload to complete with a timeout */
    aws_mutex_lock(&s_mutex);
    struct timespec start_time, current_time;
    clock_gettime(CLOCK_REALTIME, &start_time);
    bool timed_out = false;
    int max_wait_seconds = 60; /* Extend timeout to 60 seconds */
    
    DEBUG_INFO("Waiting for S3 upload to complete (timeout: %d secs)", max_wait_seconds);
    
    while (!upload_context.is_done) {
        /* Check if the upload has completed */
        if (upload_context.is_done) {
            break;
        }
        
        /* Check for timeout */
        clock_gettime(CLOCK_REALTIME, &current_time);
        int elapsed_seconds = (current_time.tv_sec - start_time.tv_sec);
        
        if (elapsed_seconds >= max_wait_seconds) {
            DEBUG_ERROR("S3 upload operation timed out after %d seconds", max_wait_seconds);
            timed_out = true;
            break;
        }
        
        /* Log progress periodically */
        if (elapsed_seconds % 5 == 0 && elapsed_seconds > 0) {
            DEBUG_INFO("Still waiting for upload to complete... (%d seconds elapsed)", 
                     elapsed_seconds);
        }
        
        /* Temporarily release the mutex to allow the S3 client to process callbacks */
        aws_mutex_unlock(&s_mutex);
        
        /* Process events while waiting */
        process_events_while_waiting(s3, 100); /* Wait 100ms */
        
        /* Re-acquire the mutex before checking the condition again */
        aws_mutex_lock(&s_mutex);
        
        /* Use condition variable with a short timeout */
        int64_t wait_timeout_ns = 1000000000; /* 1 second in nanoseconds */
        
        aws_condition_variable_wait_for_pred(
            &s_cvar, 
            &s_mutex, 
            wait_timeout_ns, 
            s_is_operation_done, 
            &upload_context);
    }

    /* Check if we timed out */
    if (!upload_context.is_done) {
        DEBUG_ERROR("S3 upload operation timed out");
        timed_out = true;
    }
    aws_mutex_unlock(&s_mutex);

    /* Clean up data request */
    DEBUG_INFO("Releasing S3 data request");
    aws_s3_meta_request_release(data_request);
    aws_http_message_release(data_message);
    unlink(temp_file_path);

    /* Check for upload errors */
    if (timed_out) {
        DEBUG_ERROR("S3 data upload operation timed out");
        snprintf(transport->error_msg, sizeof(transport->error_msg),
                "S3 data upload operation timed out");
        return EB_ERROR_TIMEOUT;
    } else if (upload_context.error_code != AWS_ERROR_SUCCESS) {
        DEBUG_ERROR("Failed to upload data to S3: error code %d (%s)", 
                  upload_context.error_code,
                  aws_error_str(upload_context.error_code));
        snprintf(transport->error_msg, sizeof(transport->error_msg),
                "Failed to upload data to S3: error code %d (%s)", 
                upload_context.error_code,
                aws_error_str(upload_context.error_code));
        return EB_ERROR_CONNECTION;
    }

    DEBUG_INFO("Data upload completed successfully");

    /* Now upload the metadata file */
    /* Build expanded metadata JSON using Jansson */
    json_t *root = json_object();
    json_object_set_new(root, "timestamp", json_integer((json_int_t)now));
    json_object_set_new(root, "size", json_integer((json_int_t)transformed_size));
    json_object_set_new(root, "set", json_string(set_name_buf));

    // --- Gather objects from per-set log ---
    json_t *objects_arr = json_array();
    {
        char *log_path = get_current_set_log_path();
        if (log_path) {
            FILE *log_file = fopen(log_path, "r");
            if (log_file) {
                char line[PATH_MAX + 128];
                while (fgets(line, sizeof(line), log_file)) {
                    char timestamp_str[32], hash[65], src_path[PATH_MAX], model[64];
                    if (sscanf(line, "%31s %64s %s %63s", timestamp_str, hash, src_path, model) == 4) {
                        json_t *obj = json_object();
                        json_object_set_new(obj, "hash", json_string(hash));
                        json_object_set_new(obj, "path", json_string(src_path));
                        json_object_set_new(obj, "created", json_integer(atoll(timestamp_str)));
                        json_object_set_new(obj, "model", json_string(model));
                        json_array_append_new(objects_arr, obj);
                    }
                }
                fclose(log_file);
            }
            free(log_path);
        }
    }
    json_object_set_new(root, "objects", objects_arr);

    // --- Gather index from per-set index ---
    json_t *index_arr = json_array();
    {
        char *index_path = get_current_set_index_path();
        if (index_path) {
            FILE *idx_file = fopen(index_path, "r");
            if (idx_file) {
                char hash[65], src_path[PATH_MAX];
                while (fscanf(idx_file, "%64s %s", hash, src_path) == 2) {
                    json_t *idx_obj = json_object();
                    json_object_set_new(idx_obj, "hash", json_string(hash));
                    json_object_set_new(idx_obj, "path", json_string(src_path));
                    json_array_append_new(index_arr, idx_obj);
                }
                fclose(idx_file);
            }
            free(index_path);
        }
    }
    json_object_set_new(root, "index", index_arr);

    // --- Gather refs ---
    json_t *refs_obj = json_object();
    char* model_refs_dir = get_current_set_model_refs_dir();
    if (model_refs_dir) {
        DIR *refs_dir = opendir(model_refs_dir);
        if (refs_dir) {
            struct dirent *entry;
            while ((entry = readdir(refs_dir)) != NULL) {
                if (entry->d_type != DT_REG) continue;
                char ref_path[512];
                snprintf(ref_path, sizeof(ref_path), "%s/%s", model_refs_dir, entry->d_name);
                FILE *ref = fopen(ref_path, "r");
                if (ref) {
                    char hash[256];
                    if (fgets(hash, sizeof(hash), ref)) {
                        char *nl = strchr(hash, '\n'); if (nl) *nl = '\0';
                        json_object_set_new(refs_obj, entry->d_name, json_string(hash));
                    }
                    fclose(ref);
                }
            }
            closedir(refs_dir);
        }
        free(model_refs_dir);
    }
    json_object_set_new(root, "refs", refs_obj);

    // --- Set head ---
    char head_val[128] = "main";
    FILE *head = fopen(".embr/HEAD", "r");
    if (head) {
        if (fgets(head_val, sizeof(head_val), head)) {
            char *nl = strchr(head_val, '\n'); if (nl) *nl = '\0';
        }
        fclose(head);
    }
    json_object_set_new(root, "head", json_string(head_val));

    // --- Serialize JSON ---
    char *metadata_content = json_dumps(root, JSON_INDENT(2));
    json_decref(root);
    
    /* Create a temporary file for the metadata */
    char meta_temp_filename[256];
    snprintf(meta_temp_filename, sizeof(meta_temp_filename), "/tmp/eb_s3_upload_meta_XXXXXX");
    int meta_temp_fd = mkstemp(meta_temp_filename);
    if (meta_temp_fd < 0) {
        DEBUG_ERROR("Failed to create temporary file for metadata: %s", strerror(errno));
        snprintf(transport->error_msg, sizeof(transport->error_msg),
                "Failed to create temporary file for metadata: %s", strerror(errno));
        return EB_ERROR_FILE_IO;
    }
    
    /* Write the metadata to the temporary file */
    size_t metadata_length = strlen(metadata_content);
    write_result = write(meta_temp_fd, metadata_content, metadata_length);
    close(meta_temp_fd);
    
    if (write_result != (ssize_t)metadata_length) {
        DEBUG_ERROR("Failed to write metadata to temporary file: %s", strerror(errno));
        snprintf(transport->error_msg, sizeof(transport->error_msg),
                "Failed to write metadata to temporary file: %s", strerror(errno));
        unlink(meta_temp_filename);
        return EB_ERROR_FILE_IO;
    }
    
    /* Create a HTTP PUT message for the metadata object */
    struct aws_http_message *meta_message = aws_http_message_new_request(aws_default_allocator());
    if (!meta_message) {
        DEBUG_ERROR("Failed to create PUT message for metadata object");
        snprintf(transport->error_msg, sizeof(transport->error_msg),
                "Failed to create PUT message for metadata object");
        unlink(meta_temp_filename);
        return EB_ERROR_GENERIC;
    }
    
    /* Setup the PUT request for metadata */
    aws_http_message_set_request_method(meta_message, aws_http_method_put);
    
    /* Set the full path in the URI */
    char meta_uri_buffer[2048];
    snprintf(meta_uri_buffer, sizeof(meta_uri_buffer), "/%s", s3_metadata_key);
    struct aws_byte_cursor meta_uri_cursor = aws_byte_cursor_from_c_str(meta_uri_buffer);
    aws_http_message_set_request_path(meta_message, meta_uri_cursor);
    
    /* Add content type header for JSON metadata */
    struct aws_http_header meta_content_type_header = {
        .name = aws_byte_cursor_from_c_str("Content-Type"),
        .value = aws_byte_cursor_from_c_str("application/json"),
    };
    aws_http_message_add_header(meta_message, meta_content_type_header);
    
    /* Add host header */
    aws_http_message_add_header(meta_message, host_header);
    
    /* Log the full request details */
    DEBUG_INFO("Sending PUT request to Host: %s, Path: %s", host_header_value, meta_uri_buffer);
    
    /* Setup synchronization primitives for metadata upload operation */
    struct s3_operation_context meta_context = {
        .lock = &s_mutex,
        .signal = &s_cvar,
        .error_code = 0,
        .is_done = false
    };
    
    /* Create the options for the metadata upload */
    struct aws_s3_meta_request_options meta_options = {
        .type = AWS_S3_META_REQUEST_TYPE_PUT_OBJECT,
        .message = meta_message,
        .send_filepath = aws_byte_cursor_from_c_str(meta_temp_filename),
        .user_data = &meta_context,
        .finish_callback = s3_on_s3_operation_finished
    };

    DEBUG_INFO("Preparing to upload metadata file to S3: %s", s3_metadata_key);

    /* Send the metadata to S3 */
    struct aws_s3_meta_request *meta_request = aws_s3_client_make_meta_request(s3->s3_client, &meta_options);
    if (!meta_request) {
        DEBUG_ERROR("Failed to create S3 meta request for metadata upload: %s", aws_error_str(aws_last_error()));
        snprintf(transport->error_msg, sizeof(transport->error_msg),
                "Failed to create S3 meta request for metadata upload: %s", aws_error_str(aws_last_error()));
        aws_http_message_release(meta_message);
        unlink(meta_temp_filename);
        return EB_ERROR_GENERIC;
    }

    DEBUG_INFO("Waiting for metadata upload to complete...");

    /* Wait for the metadata upload to complete with a timeout */
    aws_mutex_lock(&s_mutex);
    struct timespec meta_start_time, meta_current_time;
    clock_gettime(CLOCK_REALTIME, &meta_start_time);
    bool meta_timed_out = false;
    int max_meta_wait_seconds = 60; /* 60 seconds timeout */
    
    DEBUG_INFO("Waiting for S3 metadata upload to complete (timeout: %d secs)", max_meta_wait_seconds);
    
    while (!meta_context.is_done) {
        /* Check if the upload has completed */
        if (meta_context.is_done) {
            break;
        }
        
        /* Check for timeout */
        clock_gettime(CLOCK_REALTIME, &meta_current_time);
        int elapsed_seconds = (meta_current_time.tv_sec - meta_start_time.tv_sec);
        
        if (elapsed_seconds >= max_meta_wait_seconds) {
            DEBUG_ERROR("S3 metadata upload operation timed out after %d seconds", max_meta_wait_seconds);
            meta_timed_out = true;
            break;
        }
        
        /* Log progress periodically */
        if (elapsed_seconds % 5 == 0 && elapsed_seconds > 0) {
            DEBUG_INFO("Still waiting for metadata upload to complete... (%d seconds elapsed)", 
                     elapsed_seconds);
        }
        
        /* Temporarily release the mutex to allow the S3 client to process callbacks */
        aws_mutex_unlock(&s_mutex);
        
        /* Process events while waiting */
        process_events_while_waiting(s3, 100); /* Wait 100ms */
        
        /* Re-acquire the mutex before checking the condition again */
        aws_mutex_lock(&s_mutex);
        
        /* Use condition variable with a short timeout */
        int64_t wait_timeout_ns = 1000000000; /* 1 second in nanoseconds */
        
        aws_condition_variable_wait_for_pred(
            &s_cvar, 
            &s_mutex, 
            wait_timeout_ns, 
            s_is_operation_done, 
            &meta_context);
    }

    /* Check if we timed out */
    if (!meta_context.is_done) {
        DEBUG_ERROR("S3 metadata upload operation timed out");
        meta_timed_out = true;
    }
    aws_mutex_unlock(&s_mutex);

    /* Clean up metadata request */
    DEBUG_INFO("Releasing S3 metadata request");
    aws_s3_meta_request_release(meta_request);
    aws_http_message_release(meta_message);
    unlink(meta_temp_filename);

    /* Check for upload errors */
    if (meta_timed_out) {
        DEBUG_ERROR("S3 metadata upload operation timed out");
        snprintf(transport->error_msg, sizeof(transport->error_msg),
                "S3 metadata upload operation timed out");
        return EB_ERROR_TIMEOUT;
    } else if (meta_context.error_code != AWS_ERROR_SUCCESS) {
        DEBUG_ERROR("Failed to upload metadata to S3: error code %d (%s)", 
                  meta_context.error_code,
                  aws_error_str(meta_context.error_code));
        snprintf(transport->error_msg, sizeof(transport->error_msg),
                "Failed to upload metadata to S3: error code %d (%s)", 
                meta_context.error_code,
                aws_error_str(meta_context.error_code));
        return EB_ERROR_CONNECTION;
    }

    DEBUG_INFO("Successfully uploaded data and metadata to S3");
    return EB_SUCCESS;
}

/* Connect to S3 */
static int s3_connect(eb_transport_t *transport) {
    if (!transport)
        return EB_ERROR_INVALID_PARAMETER;
    
    char *aws_key = NULL;
    char *aws_secret = NULL;
    
    /* Check for AWS environment variables first */
    aws_key = getenv("AWS_ACCESS_KEY_ID");
    aws_secret = getenv("AWS_SECRET_ACCESS_KEY");
    const char *session_token = getenv("AWS_SESSION_TOKEN");
    
    /* If not found in environment, try to read from ~/.aws/credentials */
    if (!aws_key || !aws_secret) {
        char credentials_path[1024];
        char *home = getenv("HOME");
        FILE *cred_file = NULL;
        
        if (home) {
            snprintf(credentials_path, sizeof(credentials_path), "%s/.aws/credentials", home);
            DEBUG_PRINT("Looking for AWS credentials in %s", credentials_path);
            cred_file = fopen(credentials_path, "r");
            
            if (cred_file) {
                DEBUG_PRINT("Opened AWS credentials file %s", credentials_path);
                char line[1024];
                char key_buf[256] = {0};
                char secret_buf[256] = {0};
                bool in_default_section = false;
                
                while (fgets(line, sizeof(line), cred_file)) {
                    /* Remove newline */
                    char *nl = strchr(line, '\n');
                    if (nl) *nl = '\0';
                    
                    DEBUG_PRINT("Read line: %s", line);
                    
                    /* Check for section header */
                    if (line[0] == '[' && line[strlen(line)-1] == ']') {
                        line[strlen(line)-1] = '\0';
                        char *section_name = line + 1;
                        DEBUG_PRINT("Found section: %s, in_default_section=%d", section_name, strcmp(section_name, "default") == 0);
                        in_default_section = (strcmp(section_name, "default") == 0);
                        continue;
                    }
                    
                    if (in_default_section) {
                        char *eq = strchr(line, '=');
                        if (eq) {
                            *eq = '\0';
                            char *key_name = line;
                            char *value = eq + 1;
                            
                            /* Trim whitespace */
                            while (*key_name && isspace(*key_name)) key_name++;
                            while (*value && isspace(*value)) value++;
                            
                            char *end = key_name + strlen(key_name) - 1;
                            while (end > key_name && isspace(*end)) *end-- = '\0';
                            
                            end = value + strlen(value) - 1;
                            while (end > value && isspace(*end)) *end-- = '\0';
                            
                            DEBUG_PRINT("Found key-value pair: %s=%s", key_name, value);
                            
                            if (strcmp(key_name, "aws_access_key_id") == 0 || 
                                strcmp(key_name, "access_key_id") == 0) {
                                strncpy(key_buf, value, sizeof(key_buf)-1);
                                DEBUG_PRINT("Found access key ID");
                            } else if (strcmp(key_name, "aws_secret_access_key") == 0 || 
                                       strcmp(key_name, "secret_access_key") == 0) {
                                strncpy(secret_buf, value, sizeof(secret_buf)-1);
                                DEBUG_PRINT("Found secret access key");
                            }
                        }
                    }
                }
                
                fclose(cred_file);
                
                /* Use credentials from file if found */
                if (key_buf[0] && secret_buf[0]) {
                    static char saved_key[256];
                    static char saved_secret[256];
                    
                    strncpy(saved_key, key_buf, sizeof(saved_key));
                    strncpy(saved_secret, secret_buf, sizeof(saved_secret));
                    
                    aws_key = saved_key;
                    aws_secret = saved_secret;
                    
                    DEBUG_PRINT("Using AWS credentials from ~/.aws/credentials");
                } else {
                    DEBUG_PRINT("No valid credentials found in ~/.aws/credentials");
                }
            } else {
                DEBUG_PRINT("Could not open AWS credentials file %s", credentials_path);
            }
        } else {
            DEBUG_PRINT("HOME environment variable not set, cannot locate ~/.aws/credentials");
        }
    }
    
    if (!aws_key || !aws_secret) {
        snprintf(transport->error_msg, sizeof(transport->error_msg),
                "AWS credentials not found in environment or ~/.aws/credentials");
        return EB_ERROR_AUTHENTICATION;
    }
    
    /* Initialize S3 client data */
    struct s3_data *s3 = calloc(1, sizeof(*s3));
    if (!s3) {
        snprintf(transport->error_msg, sizeof(transport->error_msg),
                "Failed to allocate memory for S3 transport");
        return EB_ERROR_MEMORY;
    }
    
    /* Extract region from URL */
    s3->region = get_url_param(transport->url, "region");
    if (!s3->region) {
        /* If not in URL, try environment or config */
        char *env_region = getenv("AWS_REGION");
        if (env_region) {
            s3->region = strdup(env_region);
        } else {
            /* Default to us-east-1 */
            s3->region = strdup("us-east-1");
        }
        
        if (!s3->region) {
            free(s3);
            snprintf(transport->error_msg, sizeof(transport->error_msg),
                    "Failed to allocate memory for region");
            return EB_ERROR_MEMORY;
        }
    }
    
    /* Get a clean URL without query parameters */
    char *clean_url = get_url_without_params(transport->url);
    if (!clean_url) {
        free(s3->region);
        free(s3);
        snprintf(transport->error_msg, sizeof(transport->error_msg),
                "Failed to process S3 URL: %s", transport->url);
        return EB_ERROR_MEMORY;
    }
    
    /* Parse S3 URL with clean version */
    if (parse_s3_url(clean_url, &s3->bucket, &s3->prefix, NULL) < 0) {
        free(clean_url);
        free(s3->region);
        free(s3);
        snprintf(transport->error_msg, sizeof(transport->error_msg),
                "Failed to parse S3 URL: %s", transport->url);
        return EB_ERROR_INVALID_URL;
    }
    free(clean_url);
    
    /* Ensure prefix doesn't contain query parameters */
    char *query = strchr(s3->prefix, '?');
    if (query) {
        /* Temporarily null-terminate at the question mark */
        *query = '\0';
    }
    
    DEBUG_PRINT("Connecting to S3 bucket '%s' with prefix '%s' in region '%s'",
              s3->bucket, s3->prefix, s3->region);
    
    /* Initialize the AWS SDK */
    s3->allocator = aws_default_allocator();
    aws_auth_library_init(s3->allocator);
    aws_s3_library_init(s3->allocator);
    
    /* Create event loop group with a single thread to match minimal sample */
    uint16_t num_event_loop_threads = 1; /* Single-threaded event loop group */
    DEBUG_INFO("Creating event loop group with %d threads", num_event_loop_threads);
    s3->event_loop_group = aws_event_loop_group_new_default(s3->allocator, num_event_loop_threads, NULL);
    if (!s3->event_loop_group) {
        snprintf(transport->error_msg, sizeof(transport->error_msg),
                "Failed to create event loop group: %s", aws_error_debug_str(aws_last_error()));
        free(s3->bucket);
        free(s3->prefix);
        free(s3->region);
        free(s3);
        return EB_ERROR_INITIALIZATION;
    }
    
    /* Create host resolver */
    struct aws_host_resolver_default_options resolver_options = {
        .max_entries = 8,
        .el_group = s3->event_loop_group,
        .shutdown_options = NULL,
        .system_clock_override_fn = NULL
    };
    
    s3->resolver = aws_host_resolver_new_default(s3->allocator, &resolver_options);
    if (!s3->resolver) {
        snprintf(transport->error_msg, sizeof(transport->error_msg),
                "Failed to create host resolver: %s", aws_error_debug_str(aws_last_error()));
        aws_event_loop_group_release(s3->event_loop_group);
        free(s3->bucket);
        free(s3->prefix);
        free(s3->region);
        free(s3);
        return EB_ERROR_INITIALIZATION;
    }
    
    /* Create client bootstrap */
    struct aws_client_bootstrap_options bootstrap_options = {
        .event_loop_group = s3->event_loop_group,
        .host_resolver = s3->resolver,
        .user_data = NULL
    };
    
    s3->bootstrap = aws_client_bootstrap_new(s3->allocator, &bootstrap_options);
    if (!s3->bootstrap) {
        snprintf(transport->error_msg, sizeof(transport->error_msg),
                "Failed to create client bootstrap: %s", aws_error_debug_str(aws_last_error()));
        aws_host_resolver_release(s3->resolver);
        aws_event_loop_group_release(s3->event_loop_group);
        free(s3->bucket);
        free(s3->prefix);
        free(s3->region);
        free(s3);
        return EB_ERROR_INITIALIZATION;
    }
    
    /* Set up credentials */
    struct aws_byte_cursor access_key = aws_byte_cursor_from_c_str(aws_key);
    struct aws_byte_cursor secret_key = aws_byte_cursor_from_c_str(aws_secret);
    struct aws_byte_cursor session_token_cursor = 
        session_token ? aws_byte_cursor_from_c_str(session_token) : aws_byte_cursor_from_array(NULL, 0);
    
    /* Create AWS static credentials provider */
    struct aws_credentials_provider_static_options static_options = {
        .access_key_id = access_key,
        .secret_access_key = secret_key,
        .session_token = session_token_cursor
    };
    
    struct aws_credentials_provider *credentials_provider = aws_credentials_provider_new_static(
        s3->allocator, &static_options);
    
    if (!credentials_provider) {
        snprintf(transport->error_msg, sizeof(transport->error_msg),
                "Failed to create AWS credentials provider");
        aws_client_bootstrap_release(s3->bootstrap);
        aws_host_resolver_release(s3->resolver);
        aws_event_loop_group_release(s3->event_loop_group);
        free(s3->bucket);
        free(s3->prefix);
        free(s3->region);
        free(s3);
        return EB_ERROR_AUTHENTICATION;
    }
    
    /* Set up signing config */
    struct aws_signing_config_aws signing_config = {
        .algorithm = AWS_SIGNING_ALGORITHM_V4,
        .signature_type = AWS_ST_HTTP_REQUEST_HEADERS,
        .service = aws_byte_cursor_from_c_str("s3"),
        .region = aws_byte_cursor_from_c_str(s3->region),
        .credentials_provider = credentials_provider
    };
    /* Store signing config in s3 data */
    s3->signing_config = signing_config;
    
    /* Create S3 client configuration */
    struct aws_s3_client_config client_config = {
        .region = aws_byte_cursor_from_c_str(s3->region),
        .client_bootstrap = s3->bootstrap,
        .tls_mode = AWS_MR_TLS_ENABLED, /* Re-enable TLS with proper configuration */
        .signing_config = &s3->signing_config,
        .compute_content_md5 = AWS_MR_CONTENT_MD5_ENABLED,
        .part_size = 5 * 1024 * 1024, /* 5MB default part size */
        .multipart_upload_threshold = 8 * 1024 * 1024, /* Use multipart for files larger than 8MB */
        .max_part_size = 128 * 1024 * 1024, /* 128MB max part size */
        .throughput_target_gbps = 1.0, /* 1 Gbps target throughput */
    };
    
    /* Initialize TLS context options */
    struct aws_tls_ctx_options tls_ctx_options;
    aws_tls_ctx_options_init_default_client(&tls_ctx_options, s3->allocator);
    
    /* Create TLS context */
    struct aws_tls_ctx *tls_ctx = aws_tls_client_ctx_new(s3->allocator, &tls_ctx_options);
    if (!tls_ctx) {
        DEBUG_PRINT("Failed to create TLS context: %s", aws_error_debug_str(aws_last_error()));
        aws_credentials_provider_release(credentials_provider);
        aws_client_bootstrap_release(s3->bootstrap);
        aws_host_resolver_release(s3->resolver);
        aws_event_loop_group_release(s3->event_loop_group);
        free(s3->bucket);
        free(s3->prefix);
        free(s3->region);
        free(s3);
        return EB_ERROR_INITIALIZATION;
    }
    
    /* Initialize TLS connection options */
    s3->tls_connection_options = aws_mem_calloc(s3->allocator, 1, sizeof(struct aws_tls_connection_options));
    aws_tls_connection_options_init_from_ctx(s3->tls_connection_options, tls_ctx);
    
    /* Set TLS timeout - 10 seconds should be plenty for the handshake */
    s3->tls_connection_options->timeout_ms = 10000;
    
    /* Set server name for SNI */
    char server_name[256];
    snprintf(server_name, sizeof(server_name), "s3.%s.amazonaws.com", s3->region);
    struct aws_byte_cursor server_name_cursor = aws_byte_cursor_from_c_str(server_name);
    aws_tls_connection_options_set_server_name(
        s3->tls_connection_options, s3->allocator, &server_name_cursor);
    
    /* Clean up TLS context after initialization */
    aws_tls_ctx_release(tls_ctx);
    
    /* Set TLS connection options in the client config */
    client_config.tls_connection_options = s3->tls_connection_options;
    
    /* Set custom endpoint if provided */
    if (transport->url && *transport->url) {
    struct aws_string *endpoint_string = NULL;
        bool use_custom_endpoint = false;
        
        /* Check if URL includes an endpoint override */
        const char *endpoint_param = strstr(transport->url, "endpoint=");
        if (endpoint_param) {
            endpoint_param += 9; /* Skip "endpoint=" */
            const char *endpoint_end = strchr(endpoint_param, '&');
            if (!endpoint_end) {
                endpoint_end = endpoint_param + strlen(endpoint_param);
            }
            
            char endpoint_buf[256];
            size_t endpoint_len = endpoint_end - endpoint_param;
            if (endpoint_len < sizeof(endpoint_buf)) {
                memcpy(endpoint_buf, endpoint_param, endpoint_len);
                endpoint_buf[endpoint_len] = '\0';
                
                DEBUG_INFO("Using custom endpoint: %s", endpoint_buf);
                endpoint_string = aws_string_new_from_c_str(s3->allocator, endpoint_buf);
        if (endpoint_string) {
                    use_custom_endpoint = true;
                    
                    /* Use regional endpoint pattern if not specified otherwise */
                    client_config.region = aws_byte_cursor_from_c_str(s3->region);
                }
            }
        }
        
        /* If custom endpoint is specified, set it using the current AWS SDK structure pattern */
        if (use_custom_endpoint && endpoint_string) {
            /* For this version of AWS SDK, we need to use a different approach since endpoint field isn't available */
            DEBUG_INFO("Using custom endpoint: %s", aws_string_c_str(endpoint_string));
            s3->endpoint = endpoint_string; // Store for later use
            
            /* Create a struct with the endpoint details */
            char endpoint_host[256];
            snprintf(endpoint_host, sizeof(endpoint_host), "%s", aws_string_c_str(endpoint_string));
            
            /* Create TLS options when needed */
            if (client_config.tls_mode == AWS_MR_TLS_ENABLED) {
                DEBUG_INFO("TLS is enabled, setting custom endpoint in TLS options");
                
                /* In this version, custom endpoints may be specified through host resolution 
                   or by explicitly setting region to match endpoint */
                client_config.region = aws_byte_cursor_from_c_str(endpoint_host);
            }
            
            DEBUG_INFO("Set custom endpoint resolution for S3 client");
        } else if (endpoint_string) {
            /* Clean up if not used */
        aws_string_destroy(endpoint_string);
        }
    }
    
    /* Create the S3 client */
    s3->s3_client = aws_s3_client_new(s3->allocator, &client_config);
    
    if (!s3->s3_client) {
        snprintf(transport->error_msg, sizeof(transport->error_msg),
                "Failed to create S3 client for region %s: %s", 
                s3->region, aws_error_debug_str(aws_last_error()));
        aws_credentials_provider_release(credentials_provider);
        aws_client_bootstrap_release(s3->bootstrap);
        aws_host_resolver_release(s3->resolver);
        aws_event_loop_group_release(s3->event_loop_group);
        free(s3->bucket);
        free(s3->prefix);
        free(s3->region);
        free(s3);
        return EB_ERROR_INITIALIZATION;
    }
    
    DEBUG_PRINT("Created S3 client: %p", (void*)s3->s3_client);
    
    s3->is_connected = true;
    transport->connected = true;
    transport->data = s3;
    
    DEBUG_PRINT("Successfully connected to S3 bucket '%s' with prefix '%s'", s3->bucket, s3->prefix);
    return EB_SUCCESS;
}

/* Disconnect from S3 */
static int s3_disconnect(eb_transport_t *transport) {
    if (!transport || !transport->data)
        return EB_ERROR_INVALID_PARAMETER;
    
    struct s3_data *s3 = (struct s3_data *)transport->data;
    
    if (s3->s3_client) {
        aws_s3_client_release(s3->s3_client);
        s3->s3_client = NULL;
    }
    
    s3->is_connected = false;
    transport->connected = false;
    
    DEBUG_PRINT("Disconnected from S3");
    return EB_SUCCESS;
}

/* Free the transport instance and its internal data */
static void s3_free(eb_transport_t *transport) {
    if (!transport) {
        return;
    }
    
    struct s3_data *s3 = (struct s3_data *)transport->data;
    if (!s3) {
        return;
    }
    
    DEBUG_PRINT("Freeing S3 transport: bucket=%s, prefix=%s", 
               s3->bucket ? s3->bucket : "(null)", 
               s3->prefix ? s3->prefix : "(null)");
    
    /* Release all AWS SDK resources */
    if (s3->s3_client) {
        aws_s3_client_release(s3->s3_client);
        s3->s3_client = NULL;
    }
    
    /* Clean up TLS connection options */
    if (s3->tls_connection_options) {
        aws_tls_connection_options_clean_up(s3->tls_connection_options);
        aws_mem_release(s3->allocator, s3->tls_connection_options);
        s3->tls_connection_options = NULL;
    }
    
    if (s3->bootstrap) {
        aws_client_bootstrap_release(s3->bootstrap);
        s3->bootstrap = NULL;
    }
    
    if (s3->resolver) {
        aws_host_resolver_release(s3->resolver);
        s3->resolver = NULL;
    }
    
    if (s3->event_loop_group) {
        aws_event_loop_group_release(s3->event_loop_group);
        s3->event_loop_group = NULL;
    }
    
    /* Free allocated strings */
    if (s3->bucket) free(s3->bucket);
    if (s3->prefix) free(s3->prefix);
    if (s3->region) free(s3->region);
    if (s3->endpoint) aws_string_destroy(s3->endpoint);
    
    /* Shutdown AWS libraries */
    aws_s3_library_clean_up();
    aws_auth_library_clean_up();
    
    /* Free the s3 data structure itself */
    free(s3);
    transport->data = NULL;
}

// Add a named struct for the list context at the top of the file (after includes):
typedef struct s3_list_context {
    bool is_done;
    int error_code;
    struct aws_array_list *keys;
    struct aws_mutex *lock;
    struct aws_condition_variable *signal;
} s3_list_context;

/* Predicate to wait for list operation to complete */
static bool s3_list_done_pred(void *arg) {
    s3_list_context *ctx = (s3_list_context *)arg;
    return ctx->is_done;
}

/* File-scope callbacks for s3_list_refs to avoid nested function trampolines */
static int s3_list_refs_object_cb(const struct aws_s3_object_info *info, void *user_data) {
    s3_list_context *ctx = (s3_list_context *)user_data;
    char *key = malloc(info->key.len + 1);
    memcpy(key, info->key.ptr, info->key.len);
    key[info->key.len] = '\0';
    aws_array_list_push_back(ctx->keys, &key);
    DEBUG_INFO("s3_list_refs_object_cb: key = %s, size = %zu", key, info->size);
    return AWS_OP_SUCCESS;
}

static void s3_list_refs_finished_cb(struct aws_s3_paginator *paginator, int error_code, void *user_data) {
    s3_list_context *ctx = (s3_list_context *)user_data;
    aws_mutex_lock(ctx->lock);
    ctx->error_code = error_code;
    DEBUG_INFO("s3_list_refs_finished_cb: error_code = %d", error_code);
    ctx->is_done = true;
    aws_condition_variable_notify_one(ctx->signal);
    aws_mutex_unlock(ctx->lock);
}

/* List refs in the S3 bucket */
static int s3_list_refs(eb_transport_t *transport, 
                      char ***refs_out, size_t *count_out) {
    if (!transport || !transport->data) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    struct s3_data *s3 = (struct s3_data *)transport->data;
    if (!s3->is_connected) {
        return EB_ERROR_NOT_CONNECTED;
    }

    // Set up paginator, context, etc.
    struct aws_mutex *mutex = malloc(sizeof(struct aws_mutex));
    struct aws_condition_variable *completion_signal = malloc(sizeof(struct aws_condition_variable));
    aws_mutex_init(mutex);
    aws_condition_variable_init(completion_signal);
    s3_list_context *ctx = malloc(sizeof(s3_list_context));
    ctx->lock = mutex;
    ctx->signal = completion_signal;
    ctx->is_done = false;
    // Initialize array list for keys
    struct aws_array_list *keys = malloc(sizeof(struct aws_array_list));
    aws_array_list_init_dynamic(keys, s3->allocator, 32, sizeof(char*));
    ctx->keys = keys;

    char endpoint[1024];
    snprintf(endpoint, sizeof(endpoint), "s3.%s.amazonaws.com", s3->region);
    struct aws_byte_cursor prefix_cursor;
    if (s3->prefix && s3->prefix[0]) {
        DEBUG_PRINT("s3_list_refs: using prefix: '%s'", s3->prefix);
        prefix_cursor = aws_byte_cursor_from_c_str(s3->prefix);
    } else {
        DEBUG_PRINT("s3_list_refs: using default prefix: 'sets/'");
        prefix_cursor = aws_byte_cursor_from_c_str("sets/");
    }
    struct aws_s3_list_objects_params params = {
        .client = s3->s3_client,
        .bucket_name = aws_byte_cursor_from_c_str(s3->bucket),
        .prefix = prefix_cursor,
        .endpoint = aws_byte_cursor_from_c_str(endpoint),
        .user_data = ctx,
        .on_object = s3_list_refs_object_cb,
        .on_list_finished = s3_list_refs_finished_cb
    };
    struct aws_s3_paginator *paginator = aws_s3_initiate_list_objects(s3->allocator, &params);
    if (!paginator) {
        aws_mutex_clean_up(mutex);
        aws_condition_variable_clean_up(completion_signal);
        free(mutex);
        free(completion_signal);
        free(ctx);
        return EB_ERROR_CONNECTION;
    }
    if (aws_s3_paginator_continue(paginator, &s3->signing_config)) {
        aws_s3_paginator_release(paginator);
        aws_mutex_clean_up(mutex);
        aws_condition_variable_clean_up(completion_signal);
        free(mutex);
        free(completion_signal);
        free(ctx);
        return EB_ERROR_CONNECTION;
    }
    aws_mutex_lock(mutex);
    aws_condition_variable_wait_pred(ctx->signal, ctx->lock, s3_list_done_pred, ctx);
    aws_mutex_unlock(mutex);
    // DEBUG: listing completed for prefix
    DEBUG_INFO("s3_list_refs: listing done, prefix='%s', error_code=%d", s3->prefix, ctx->error_code);
    aws_s3_paginator_release(paginator);
    // After paginator, copy keys to refs_out/count_out
    size_t n = aws_array_list_length(ctx->keys);
    if (refs_out && count_out) {
        *refs_out = malloc(n * sizeof(char*));
        for (size_t i = 0; i < n; ++i) {
            char *key;
            aws_array_list_get_at(ctx->keys, &key, i);
            (*refs_out)[i] = key;
        }
        *count_out = n;
    }
    aws_array_list_clean_up(ctx->keys);
    free(ctx->keys);
    aws_mutex_clean_up(mutex);
    aws_condition_variable_clean_up(completion_signal);
    free(mutex);
    free(completion_signal);
    free(ctx);
    return EB_SUCCESS;
}

/* State tracking for the download operation */
struct download_context {
    int error_code;
    bool is_done;
    struct aws_mutex *lock;
    struct aws_condition_variable *signal;
    void *data;
    size_t data_size;
    size_t capacity;
    size_t current_pos;
};

/* Check if a download operation is done */
static bool s_is_download_done(void *arg) {
    struct download_context *context = arg;
    return context->is_done;
}

/* Callbacks for S3 metadata download */
static int s3_metadata_body_cb(
    struct aws_s3_meta_request *meta_request,
    const struct aws_byte_cursor *body,
    uint64_t range_start,
    void *user_data)
{
    (void)meta_request;
    (void)range_start;
    
    struct download_context *context = user_data;
    
    DEBUG_INFO("s3_on_metadata_body: received %zu bytes at range start %llu", 
              body->len, (unsigned long long)range_start);
    
    /* Ensure we have enough space */
    if (context->current_pos + body->len > context->capacity) {
        DEBUG_WARN("s3_on_metadata_body: buffer capacity exceeded (%zu + %zu > %zu)",
                  context->current_pos, body->len, context->capacity);
        
        size_t bytes_to_copy = context->capacity - context->current_pos;
        if (bytes_to_copy > 0) {
            DEBUG_INFO("s3_on_metadata_body: copying partial data (%zu bytes)", bytes_to_copy);
            memcpy((char*)context->data + context->current_pos, body->ptr, bytes_to_copy);
            context->current_pos += bytes_to_copy;
            context->data_size = context->current_pos;
        }
        
        /* Signal that we've received more data than we can store */
        DEBUG_WARN("s3_on_metadata_body: discarding excess data");
        return AWS_OP_SUCCESS; /* Continue receiving but discard excess */
    }
    
    /* Copy the data */
    DEBUG_INFO("s3_on_metadata_body: copying %zu bytes at offset %zu", 
              body->len, context->current_pos);
    memcpy((char*)context->data + context->current_pos, body->ptr, body->len);
    context->current_pos += body->len;
    context->data_size = context->current_pos;
    
    return AWS_OP_SUCCESS;
}

static void s3_metadata_complete_cb(
    struct aws_s3_meta_request *meta_request,
    const struct aws_s3_meta_request_result *meta_request_result,
    void *user_data)
{
    (void)meta_request;
    
    struct download_context *context = user_data;
    
    DEBUG_INFO("s3_on_metadata_complete: download finished with error code %d", 
              meta_request_result->error_code);
    
    if (meta_request_result->error_code != AWS_ERROR_SUCCESS) {
        DEBUG_ERROR("s3_on_metadata_complete: AWS error: %s", 
                  aws_error_debug_str(meta_request_result->error_code));
    }
    
    if (meta_request_result->response_status) {
        DEBUG_INFO("s3_on_metadata_complete: HTTP status code: %d", 
                  meta_request_result->response_status);
    }
    
    aws_mutex_lock(context->lock);
    context->error_code = meta_request_result->error_code;
    context->is_done = true;
    DEBUG_INFO("s3_on_metadata_complete: signaling completion");
    aws_condition_variable_notify_one(context->signal);
    aws_mutex_unlock(context->lock);
}

/* Callbacks for S3 data download */
static int s3_data_body_cb(
    struct aws_s3_meta_request *meta_request,
    const struct aws_byte_cursor *body,
    uint64_t range_start,
    void *user_data)
{
    (void)meta_request;
    (void)range_start;
    
    struct download_context *context = user_data;
    
    DEBUG_INFO("s3_on_data_body: received %zu bytes at range start %llu", 
              body->len, (unsigned long long)range_start);
    
    /* Ensure we have enough space */
    if (context->current_pos + body->len > context->capacity) {
        DEBUG_WARN("s3_on_data_body: buffer capacity exceeded (%zu + %zu > %zu)",
                  context->current_pos, body->len, context->capacity);
        
        /* We can't expand the buffer, so just copy what we can */
        size_t bytes_to_copy = context->capacity - context->current_pos;
        if (bytes_to_copy > 0) {
            DEBUG_INFO("s3_on_data_body: copying partial data (%zu bytes)", bytes_to_copy);
            memcpy((char*)context->data + context->current_pos, body->ptr, bytes_to_copy);
            context->current_pos += bytes_to_copy;
            context->data_size = context->current_pos;
        }
        
        /* Signal that we've received more data than we can store */
        DEBUG_WARN("s3_on_data_body: discarding excess data");
        return AWS_OP_SUCCESS; /* Continue receiving but discard excess */
    }
    
    /* Copy the data */
    DEBUG_INFO("s3_on_data_body: copying %zu bytes at offset %zu", 
              body->len, context->current_pos);
    memcpy((char*)context->data + context->current_pos, body->ptr, body->len);
    context->current_pos += body->len;
    context->data_size = context->current_pos;
    
    return AWS_OP_SUCCESS;
}

static void s3_data_complete_cb(
    struct aws_s3_meta_request *meta_request,
    const struct aws_s3_meta_request_result *meta_request_result,
    void *user_data)
{
    (void)meta_request;
    
    struct download_context *context = user_data;
    
    DEBUG_INFO("s3_on_data_complete: download finished with error code %d", 
              meta_request_result->error_code);
    
    if (meta_request_result->error_code != AWS_ERROR_SUCCESS) {
        DEBUG_ERROR("s3_on_data_complete: AWS error: %s", 
                  aws_error_debug_str(meta_request_result->error_code));
    }
    
    if (meta_request_result->response_status) {
        DEBUG_INFO("s3_on_data_complete: HTTP status code: %d", 
                  meta_request_result->response_status);
    }
    
    aws_mutex_lock(context->lock);
    context->error_code = meta_request_result->error_code;
    context->is_done = true;
    DEBUG_INFO("s3_on_data_complete: signaling completion");
    aws_condition_variable_notify_one(context->signal);
    aws_mutex_unlock(context->lock);
}

/* Receive data from S3 */
static int s3_receive_data(eb_transport_t *transport, void *buffer, size_t size, size_t *received) {
    if (!transport || !transport->data || !buffer || size == 0 || !received)
        return EB_ERROR_INVALID_PARAMETER;
    
    struct s3_data *s3 = (struct s3_data *)transport->data;
    
    if (!s3->is_connected) {
        snprintf(transport->error_msg, sizeof(transport->error_msg),
                "Not connected to S3");
        return EB_ERROR_NOT_CONNECTED;
    }
    
    DEBUG_INFO("Downloading from S3 bucket '%s' with prefix '%s'", 
               s3->bucket, s3->prefix);

    // Always use direct-download logic for any key
    char s3_data_key[1024] = {0};
    snprintf(s3_data_key, sizeof(s3_data_key), "%s", transport->target_path);
    DEBUG_INFO("Direct data download: S3 key = %s", s3_data_key);
    // Setup synchronization primitives
    struct aws_mutex mutex = AWS_MUTEX_INIT;
    struct aws_condition_variable completion_signal = AWS_CONDITION_VARIABLE_INIT;
    void *data_buffer = malloc(size);
    if (!data_buffer) {
        snprintf(transport->error_msg, sizeof(transport->error_msg),
                "Failed to allocate memory for data download");
        return EB_ERROR_MEMORY;
    }
    struct download_context data_context = {
        .error_code = 0,
        .is_done = false,
        .lock = &mutex,
        .signal = &completion_signal,
        .data = data_buffer,
        .data_size = 0,
        .capacity = size,
        .current_pos = 0
    };
    struct aws_http_message *data_request = aws_http_message_new_request(s3->allocator);
    if (!data_request) {
        DEBUG_ERROR("Failed to create HTTP request for data download");
        free(data_buffer);
        return EB_ERROR_MEMORY;
    }
    char host_header_value[256];
    snprintf(host_header_value, sizeof(host_header_value), "%s.s3.%s.amazonaws.com", 
            s3->bucket, s3->region);
    struct aws_http_header host_header = {
        .name = aws_byte_cursor_from_c_str("Host"),
        .value = aws_byte_cursor_from_c_str(host_header_value),
        .compression = AWS_HTTP_HEADER_COMPRESSION_USE_CACHE
    };
    if (aws_http_message_add_header(data_request, host_header) != AWS_OP_SUCCESS) {
        DEBUG_ERROR("Failed to add Host header to data request");
        aws_http_message_release(data_request);
        free(data_buffer);
        return EB_ERROR_GENERIC;
    }
    if (aws_http_message_set_request_method(data_request, aws_http_method_get) != AWS_OP_SUCCESS) {
        DEBUG_ERROR("Failed to set GET method on data request");
        aws_http_message_release(data_request);
        free(data_buffer);
        return EB_ERROR_GENERIC;
    }
    // Make sure the path starts with a slash for S3 requests
    char s3_data_request_path[1024];
    if (s3_data_key[0] != '/') {
        snprintf(s3_data_request_path, sizeof(s3_data_request_path), "/%s", s3_data_key);
    } else {
        strncpy(s3_data_request_path, s3_data_key, sizeof(s3_data_request_path)-1);
        s3_data_request_path[sizeof(s3_data_request_path)-1] = '\0';
    }
    DEBUG_INFO("S3 data request path: %s", s3_data_request_path);
    if (aws_http_message_set_request_path(data_request, 
            aws_byte_cursor_from_c_str(s3_data_request_path)) != AWS_OP_SUCCESS) {
        DEBUG_ERROR("Failed to set path on data request: %s", s3_data_request_path);
        aws_http_message_release(data_request);
        free(data_buffer);
        return EB_ERROR_GENERIC;
    }
    struct aws_s3_meta_request_options data_options = {
        .type = AWS_S3_META_REQUEST_TYPE_GET_OBJECT,
        .message = data_request,
        .user_data = &data_context,
        .body_callback = s3_data_body_cb,
        .finish_callback = s3_data_complete_cb
    };
    struct aws_s3_meta_request *data_meta_request = 
        aws_s3_client_make_meta_request(s3->s3_client, &data_options);
    if (!data_meta_request) {
        DEBUG_ERROR("Failed to create data download request: %s (AWS error: %d)",
                   aws_error_debug_str(aws_last_error()), aws_last_error());
        aws_http_message_release(data_request);
        free(data_buffer);
        return EB_ERROR_CONNECTION;
    }
    aws_mutex_lock(&mutex);
    aws_condition_variable_wait_pred(
        &completion_signal, &mutex, s_is_download_done, &data_context);
    aws_mutex_unlock(&mutex);
    if (data_context.error_code != AWS_ERROR_SUCCESS) {
        DEBUG_ERROR("Failed to download data: %s",
                   aws_error_debug_str(data_context.error_code));
        aws_s3_meta_request_release(data_meta_request);
        aws_http_message_release(data_request);
        free(data_buffer);
        snprintf(transport->error_msg, sizeof(transport->error_msg),
                "Failed to download data: %s",
                aws_error_debug_str(data_context.error_code));
        return EB_ERROR_CONNECTION;
    }
    {
        /* Return raw Parquet buffer to CLI for inverse-transform + metadata extraction */
        size_t copy_size = (data_context.data_size > size) ? size : data_context.data_size;
        memcpy(buffer, data_buffer, copy_size);
        *received = copy_size;
        free(data_buffer);
        DEBUG_INFO("Successfully downloaded %zu bytes from S3", *received);
        return EB_SUCCESS;
    }
}

/* Delete refs/objects in the S3 bucket (stub for now) */
static int s3_delete_refs(eb_transport_t *transport, const char **refs, size_t count) {
    if (!transport || !transport->data || !refs || count == 0)
        return EB_ERROR_INVALID_PARAMETER;

    struct s3_data *s3 = (struct s3_data *)transport->data;
    if (!s3->is_connected) {
        snprintf(transport->error_msg, sizeof(transport->error_msg),
                "Not connected to S3");
        return EB_ERROR_NOT_CONNECTED;
    }

    char host_header_value[256];
    snprintf(host_header_value, sizeof(host_header_value), "%s.s3.%s.amazonaws.com", 
            s3->bucket, s3->region);
    struct aws_http_header host_header = {
        .name = aws_byte_cursor_from_c_str("Host"),
        .value = aws_byte_cursor_from_c_str(host_header_value),
    };

    for (size_t i = 0; i < count; ++i) {
        const char *key = refs[i];
        if (!key || !*key) continue;

        struct aws_http_message *del_message = aws_http_message_new_request(s3->allocator);
        if (!del_message) {
            snprintf(transport->error_msg, sizeof(transport->error_msg),
                    "Failed to create DELETE message for object %s", key);
            return EB_ERROR_GENERIC;
        }

        aws_http_message_set_request_method(del_message, aws_http_method_delete);

        char uri_buffer[2048];
        if (key[0] != '/') {
            snprintf(uri_buffer, sizeof(uri_buffer), "/%s", key);
        } else {
            strncpy(uri_buffer, key, sizeof(uri_buffer)-1);
            uri_buffer[sizeof(uri_buffer)-1] = '\0';
        }
        struct aws_byte_cursor uri_cursor = aws_byte_cursor_from_c_str(uri_buffer);
        aws_http_message_set_request_path(del_message, uri_cursor);
        aws_http_message_add_header(del_message, host_header);

        struct s3_operation_context del_context = {
            .lock = &s_mutex,
            .signal = &s_cvar,
            .error_code = 0,
            .is_done = false
        };

        struct aws_s3_meta_request_options del_options = {
            .message = del_message,
            .user_data = &del_context,
            .finish_callback = s3_on_s3_operation_finished
        };

        struct aws_s3_meta_request *del_request = aws_s3_client_make_meta_request(s3->s3_client, &del_options);
        if (!del_request) {
            snprintf(transport->error_msg, sizeof(transport->error_msg),
                    "Failed to create S3 meta request for delete: %s", aws_error_str(aws_last_error()));
            aws_http_message_release(del_message);
            return EB_ERROR_GENERIC;
        }

        aws_mutex_lock(&s_mutex);
        struct timespec start_time, current_time;
        clock_gettime(CLOCK_REALTIME, &start_time);
        bool timed_out = false;
        int max_wait_seconds = 30;
        while (!del_context.is_done) {
            clock_gettime(CLOCK_REALTIME, &current_time);
            int elapsed_seconds = (current_time.tv_sec - start_time.tv_sec);
            if (elapsed_seconds >= max_wait_seconds) {
                timed_out = true;
                break;
            }
            aws_mutex_unlock(&s_mutex);
            process_events_while_waiting(s3, 100);
            aws_mutex_lock(&s_mutex);
            int64_t wait_timeout_ns = 1000000000;
            aws_condition_variable_wait_for_pred(
                &s_cvar, 
                &s_mutex, 
                wait_timeout_ns, 
                s_is_operation_done, 
                &del_context);
        }
        aws_mutex_unlock(&s_mutex);
        aws_s3_meta_request_release(del_request);
        aws_http_message_release(del_message);
        if (timed_out) {
            snprintf(transport->error_msg, sizeof(transport->error_msg),
                    "S3 delete operation timed out for key: %s", key);
            return EB_ERROR_TIMEOUT;
        } else if (del_context.error_code != AWS_ERROR_SUCCESS) {
            snprintf(transport->error_msg, sizeof(transport->error_msg),
                    "Failed to delete object %s: error code %d (%s)", 
                    key, del_context.error_code, aws_error_str(del_context.error_code));
            return EB_ERROR_CONNECTION;
        }
    }
    return EB_SUCCESS;
}

/* Operations table for S3 transport */
struct transport_ops s3_ops = {
    .connect = s3_connect,
    .send_data = s3_send_data,
    .receive_data = s3_receive_data,
    .disconnect = s3_disconnect,
    .list_refs = s3_list_refs,
    .delete_refs = s3_delete_refs // New: delete operation
};

int s3_transport_init(void) {
    DEBUG_INFO("S3 transport module initialized");
    return EB_SUCCESS;
}

#endif /* EB_HAVE_AWS */ 