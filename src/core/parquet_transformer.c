/*
 * EmbeddingBridge - Parquet Transformer Implementation
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>    /* For getcwd */
#include <sys/stat.h>  /* For mkdir */
#include <limits.h>    /* For PATH_MAX */

#include "transformer.h"
#include "status.h"
#include "debug.h"
#include "compress.h"
#include "types.h"  /* Include types.h for eb_object_header_t */
#include "path_utils.h"

/* Arrow GLib includes */
#include <arrow-glib/arrow-glib.h>
#include <parquet-glib/parquet-glib.h>
#include <glib.h>

/* Forward declarations */
static eb_status_t parquet_transform(struct eb_transformer* transformer, 
                                    const void* source, size_t source_size, 
                                    void** dest, size_t* dest_size);
static eb_status_t parquet_inverse_transform(struct eb_transformer* transformer, 
                                           const void* source, size_t source_size, 
                                           void** dest, size_t* dest_size);
static void parquet_transformer_free(struct eb_transformer* transformer);
static struct eb_transformer* parquet_transformer_clone(const struct eb_transformer* transformer);
static eb_status_t read_metadata_from_meta_file(const char* hash_str, char** source, char** model, char** timestamp);
static void extract_schema_metadata(GArrowSchema* schema, char** hash, char** source, char** model, char** timestamp, char** dimensions);

/* Thread local storage for document text */
static __thread char* tls_document_text = NULL;

/**
 * Set document text for the next parquet transform operation
 * Text will be stored in the 'blob' column and cleared after transform
 *
 * @param text Document text to store in blob column (NULL to clear)
 */
void eb_parquet_set_document_text(const char* text) {
    if (tls_document_text) {
        free(tls_document_text);
        tls_document_text = NULL;
    }
    
    if (text) {
        tls_document_text = strdup(text);
    }
}

/* Configuration structure for the Parquet transformer */
typedef struct {
    int compression_level;
    bool initialized;
} parquet_transformer_config_t;

/* Initialize Arrow */
static bool initialize_arrow() {
    /* No explicit initialization required for Arrow GLib */
    return true;
}

/* Create a new Parquet transformer with the specified compression level */
eb_transformer_t* eb_parquet_transformer_create(int compression_level) {
    eb_transformer_t* transformer = (eb_transformer_t*)malloc(sizeof(eb_transformer_t));
    if (!transformer) {
        DEBUG_ERROR("Failed to allocate memory for Parquet transformer");
        return NULL;
    }

    parquet_transformer_config_t* config = (parquet_transformer_config_t*)malloc(sizeof(parquet_transformer_config_t));
    if (!config) {
        free(transformer);
        DEBUG_ERROR("Failed to allocate memory for Parquet transformer config");
        return NULL;
    }

    config->compression_level = compression_level;
    config->initialized = true;

    transformer->name = strdup("parquet");
    transformer->format_name = strdup("parquet");
    transformer->transform = parquet_transform;
    transformer->inverse = parquet_inverse_transform;
    transformer->free = parquet_transformer_free;
    transformer->clone = parquet_transformer_clone;
    transformer->user_data = config;

    return transformer;
}

/* Get the configuration from the transformer */
static parquet_transformer_config_t* get_config(struct eb_transformer* transformer) {
    if (!transformer || !transformer->user_data) {
        return NULL;
    }
    return (parquet_transformer_config_t*)transformer->user_data;
}

/* 
 * Helper function to read metadata from a .meta file
 * Looks for .meta file in the .embr/objects directory with the given hash
 */
static eb_status_t read_metadata_from_meta_file(const char* hash_str, char** source, char** model, char** timestamp) {
    if (!hash_str || !source || !model || !timestamp) {
        return EB_ERROR_INVALID_INPUT;
    }
    
    /* Initialize outputs to NULL */
    *source = NULL;
    *model = NULL;
    *timestamp = NULL;
    
    /* Get the repository root directory */
    char repo_root[PATH_MAX] = {0};
    char* eb_dir = getenv("EB_DIR");
    if (eb_dir) {
        strncpy(repo_root, eb_dir, PATH_MAX - 1);
    } else {
        /* Use current directory as fallback */
        if (!getcwd(repo_root, PATH_MAX - 1)) {
            DEBUG_ERROR("Failed to get current directory");
            return EB_ERROR_FILE_IO;
        }
    }
    
    /* Construct path to the metadata file */
    char meta_path[PATH_MAX];
    snprintf(meta_path, sizeof(meta_path), "%s/.embr/objects/%s.meta", repo_root, hash_str);
    
    /* Try alternative path if first doesn't exist */
    if (access(meta_path, F_OK) != 0) {
        /* Try without .embr prefix for backward compatibility */
        snprintf(meta_path, sizeof(meta_path), "%s/objects/%s.meta", repo_root, hash_str);
        
        /* If still not found, check current directory */
        if (access(meta_path, F_OK) != 0) {
            snprintf(meta_path, sizeof(meta_path), "%s.meta", hash_str);
            
            /* If still not found, return not found error */
            if (access(meta_path, F_OK) != 0) {
                DEBUG_WARN("Could not find metadata file for hash %s", hash_str);
                return EB_ERROR_NOT_FOUND;
            }
        }
    }
    
    DEBUG_INFO("Found metadata file at %s", meta_path);
    
    /* Read the metadata file */
    FILE* meta_file = fopen(meta_path, "r");
    if (!meta_file) {
        DEBUG_ERROR("Failed to open metadata file %s", meta_path);
        return EB_ERROR_FILE_IO;
    }
    
    char line[1024];
    while (fgets(line, sizeof(line), meta_file)) {
        /* Remove trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }
        
        /* Parse key=value or key: value format */
        char* sep = strchr(line, '=');
        if (!sep) {
            sep = strchr(line, ':');
            if (sep) {
                /* Skip any whitespace after the colon */
                char* value = sep + 1;
                while (*value == ' ' || *value == '\t') {
                    value++;
                }
                sep = value - 1;
            }
        }
        
        if (sep) {
            *sep = '\0';
            char* key = line;
            char* value = sep + 1;
            
            /* Skip whitespace after equals sign */
            while (*value == ' ' || *value == '\t') {
                value++;
            }
            
            if (strcmp(key, "source_file") == 0 || strcmp(key, "source") == 0) {
                *source = strdup(value);
            } else if (strcmp(key, "provider") == 0 || strcmp(key, "model") == 0) {
                *model = strdup(value);
            } else if (strcmp(key, "timestamp") == 0) {
                *timestamp = strdup(value);
            }
        }
    }
    
    fclose(meta_file);
    
    /* Return success even if we didn't find all metadata fields */
    return EB_SUCCESS;
}

/* 
 * Helper function to extract metadata from Parquet schema
 */
static void extract_schema_metadata(GArrowSchema* schema, char** hash, char** source, char** model, char** timestamp, char** dimensions) {
    if (!schema) {
        return;
    }
    
    /* Initialize outputs to NULL */
    if (hash) *hash = NULL;
    if (source) *source = NULL;
    if (model) *model = NULL;
    if (timestamp) *timestamp = NULL;
    if (dimensions) *dimensions = NULL;
    
    /* Get metadata as a GHashTable */
    GHashTable* metadata = garrow_schema_get_metadata(schema);
    if (!metadata) {
        DEBUG_WARN("No metadata found in Parquet schema");
        return;
    }
    
    /* Extract values */
    if (hash) {
        const gchar* hash_str = g_hash_table_lookup(metadata, "hash");
        if (hash_str) {
            *hash = strdup(hash_str);
        }
    }
    
    if (source) {
        const gchar* source_str = g_hash_table_lookup(metadata, "source");
        if (source_str) {
            *source = strdup(source_str);
        }
    }
    
    if (model) {
        const gchar* model_str = g_hash_table_lookup(metadata, "model");
        if (model_str) {
            *model = strdup(model_str);
        }
    }
    
    if (timestamp) {
        const gchar* timestamp_str = g_hash_table_lookup(metadata, "timestamp");
        if (timestamp_str) {
            *timestamp = strdup(timestamp_str);
        }
    }
    
    if (dimensions) {
        const gchar* dimensions_str = g_hash_table_lookup(metadata, "dimensions");
        if (dimensions_str) {
            *dimensions = strdup(dimensions_str);
        }
    }
    
    /* Free the hash table */
    g_hash_table_unref(metadata);
}

/* 
 * Transform source data into Parquet format
 * Converts binary data to Parquet columnar format with ZSTD compression
 */
static eb_status_t parquet_transform(struct eb_transformer* transformer, 
                                    const void* source, size_t source_size, 
                                    void** dest, size_t* dest_size) {
    DEBUG_WARN("Starting parquet_transform. source=%p, source_size=%zu", source, source_size);
    
    /* Add detailed debug output about the source data */
    DEBUG_WARN("First 16 bytes of source data:");
    const unsigned char* src_bytes = (const unsigned char*)source;
    for (size_t i = 0; i < (source_size >= 16 ? 16 : source_size); i++) {
        DEBUG_WARN("  byte[%zu] = 0x%02x (dec: %d, char: %c)", 
                 i, src_bytes[i], src_bytes[i], 
                 (src_bytes[i] >= 32 && src_bytes[i] <= 126) ? src_bytes[i] : '.');
    }
    
    /* Check for ZSTD magic number (0xFD2FB528) at the beginning of the data 
     * which would indicate this is already compressed data */
    if (source_size >= 4 && 
        src_bytes[0] == 0x28 && src_bytes[1] == 0xB5 && 
        src_bytes[2] == 0x2F && src_bytes[3] == 0xFD) {
        DEBUG_ERROR("Source data appears to be ZSTD-compressed (magic number detected)");
        DEBUG_ERROR("Parquet transformer should receive uncompressed data, not compressed");
    }
    
    if (!transformer || !source || !dest || !dest_size) {
        DEBUG_ERROR("Invalid input parameters. transformer=%p, source=%p, dest=%p, dest_size=%p", 
                   transformer, source, dest, dest_size);
        return EB_ERROR_INVALID_INPUT;
    }
    
    if (source_size == 0) {
        DEBUG_ERROR("Source data size is zero");
        return EB_ERROR_INVALID_INPUT;
    }
    
    /* Check if the data is text-based (like JSON) and should be passed through */
    if (source_size > 0) {
        /* Check for JSON beginning with '{' or '[' */
        if ((src_bytes[0] == '{' || src_bytes[0] == '[') && 
            (source_size < 2 || src_bytes[source_size-1] == '}' || src_bytes[source_size-1] == ']')) {
            DEBUG_WARN("Data appears to be JSON or text, using pass-through instead of Parquet transformation");
            
            /* For JSON/text data, just return a copy of it rather than trying to convert it */
            *dest = malloc(source_size);
            if (!*dest) {
                return EB_ERROR_MEMORY_ALLOCATION;
            }
            
            memcpy(*dest, source, source_size);
            *dest_size = source_size;
            return EB_SUCCESS;
        }
    }

    DEBUG_INFO("Transforming data to Parquet format");
    
    GError *error = NULL;
    
    /* Check if the input has MVBE header (standard EmbeddingBridge format) */
    /* This works for both compressed and uncompressed data since header is first */
    bool has_eb_header = false;
    const eb_object_header_t* eb_header = NULL;
    
    /* Check if we have a valid EmbeddingBridge header */
    if (source_size >= sizeof(eb_object_header_t)) {
        eb_header = (const eb_object_header_t*)source;
        has_eb_header = true;
        
        DEBUG_INFO("Found EmbeddingBridge header with hash: ");
        for (int i = 0; i < 8; i++) {
            DEBUG_INFO("  %02x", eb_header->hash[i]);
        }
    }
    
    /* Variables needed for processing the data */
    void* decompressed_data = NULL;
    size_t decompressed_size = 0;
    const void* data_to_process = NULL;
    size_t data_size = 0;
    bool need_to_free_decompressed = false;
    
    /* Extract the hash ID string from the header for Pinecone ID field */
    char id_str[65] = {0};
    if (has_eb_header) {
        for (int i = 0; i < 32; i++) {
            sprintf(id_str + (i * 2), "%02x", eb_header->hash[i]);
        }
        DEBUG_INFO("Using object hash as ID: %s", id_str);
        
        /* Handle compression if needed */
        if (eb_header->flags & EB_FLAG_COMPRESSED) {
            DEBUG_INFO("Object is compressed, decompressing...");
            eb_status_t status = eb_decompress_zstd(
                (const uint8_t*)source + sizeof(eb_object_header_t),
                source_size - sizeof(eb_object_header_t),
                &decompressed_data,
                &decompressed_size
            );
            
            if (status != EB_SUCCESS) {
                DEBUG_ERROR("Failed to decompress data: %d", status);
                return status;
            }
            
            data_to_process = decompressed_data;
            data_size = decompressed_size;
            need_to_free_decompressed = true;
        } else {
            data_to_process = (const uint8_t*)source + sizeof(eb_object_header_t);
            data_size = source_size - sizeof(eb_object_header_t);
        }
    } else {
        /* No EmbeddingBridge header, treat as raw data */
        DEBUG_INFO("No EmbeddingBridge header found, using random ID");
        /* Generate a random ID if we don't have a header */
        char temp_id[33] = {0};
        for (int i = 0; i < 32; i++) {
            temp_id[i] = "0123456789abcdef"[rand() % 16];
        }
        strcpy(id_str, temp_id);
        
        data_to_process = source;
        data_size = source_size;
    }
    
    /* Determine if this is a NumPy array (.npy) or binary data */
    bool is_npy = false;
    uint32_t dimensions = 0;
    const float* values = NULL;
    
    if (data_size > 10) {
        /* Check for NumPy magic string '\x93NUMPY' */
        if (memcmp(data_to_process, "\x93NUMPY", 6) == 0) {
            is_npy = true;
            DEBUG_INFO("Detected NumPy array format (.npy)");
            
        /* Extract header size from NumPy format (stored at offset 8 as uint16) */
            uint16_t header_size = *((const uint16_t*)((const uint8_t*)data_to_process + 8));
                
        /* Calculate data offset */
        size_t data_offset = 10 + header_size;
            
            /* Get dimension from size */
            dimensions = (data_size - data_offset) / sizeof(float);
            values = (const float*)((const uint8_t*)data_to_process + data_offset);
            
            DEBUG_INFO("NumPy array has %u dimensions", dimensions);
        } else {
            /* Assume raw binary format with 4-byte dimension header */
            DEBUG_INFO("Assuming raw binary format with dimension header");
            
            memcpy(&dimensions, data_to_process, sizeof(uint32_t));
            values = (const float*)((const uint8_t*)data_to_process + sizeof(uint32_t));
            
            DEBUG_INFO("Binary data has %u dimensions", dimensions);
            
            /* Validate dimensions make sense with remaining data size */
            if (dimensions * sizeof(float) + sizeof(uint32_t) != data_size) {
                DEBUG_WARN("Dimension header (%u) doesn't match data size, might be incorrect format", 
                          dimensions);
            }
        }
    }
    
    /* Determine file type for metadata */
    const char *file_type = is_npy ? "npy" : "bin";
    
    /* Create Pinecone-compatible Parquet format */
    /* Schema: id (string), values (list of floats) */
    DEBUG_INFO("Creating Pinecone-compatible schema with id and values");
    
    /* Create schema fields */
    GArrowField* id_field = garrow_field_new("id", GARROW_DATA_TYPE(garrow_string_data_type_new()));
    
    /* First create the value field type (float) */
    GArrowDataType* float_type = GARROW_DATA_TYPE(garrow_float_data_type_new());
    
    /* Create the field for the list item type */
    GArrowField* float_field = garrow_field_new("item", float_type);
    
    /* Now create the list data type */
    GArrowDataType* list_type = GARROW_DATA_TYPE(garrow_list_data_type_new(float_field));
    
    /* Create values field */
    GArrowField* values_field = garrow_field_new("values", list_type);
    
    /* Create metadata field (string type) */
    GArrowField* metadata_field = garrow_field_new("metadata", GARROW_DATA_TYPE(garrow_string_data_type_new()));
    
    /* Create blob field (string type) */
    GArrowField* blob_field = garrow_field_new("blob", GARROW_DATA_TYPE(garrow_string_data_type_new()));
    
    /* Create schema with fields */
    GList* fields = NULL;
    fields = g_list_append(fields, id_field);
    fields = g_list_append(fields, values_field);
    fields = g_list_append(fields, metadata_field);
    fields = g_list_append(fields, blob_field);
    
    GArrowSchema* schema = garrow_schema_new(fields);
    g_list_free(fields);

    /* Prepare metadata JSON string */
    char* metadata_json = NULL;
    size_t metadata_json_size = 0;
    
    /* Add metadata to the schema */
    if (has_eb_header) {
        /* Try to read metadata from .meta file using the hash ID */
        char* source_file = NULL;
        char* model = NULL;
        char* timestamp = NULL;
        
        if (read_metadata_from_meta_file(id_str, &source_file, &model, &timestamp) == EB_SUCCESS) {
            DEBUG_INFO("Adding metadata from .meta file to Parquet schema");
            
            /* Create JSON string with metadata */
            metadata_json_size = 1024; /* Initial buffer size */
            metadata_json = (char*)malloc(metadata_json_size);
            if (metadata_json) {
                int written = snprintf(metadata_json, metadata_json_size, 
                                      "{\"hash\":\"%s\"", id_str);
                
                if (source_file) {
                    written += snprintf(metadata_json + written, metadata_json_size - written,
                                      ",\"source\":\"%s\"", source_file);
                    DEBUG_INFO("Source file from metadata: %s", source_file);
                }
                
                if (model) {
                    written += snprintf(metadata_json + written, metadata_json_size - written,
                                      ",\"model\":\"%s\"", model);
                    DEBUG_INFO("Model from metadata: %s", model);
                }
                
                if (timestamp) {
                    written += snprintf(metadata_json + written, metadata_json_size - written,
                                      ",\"timestamp\":\"%s\"", timestamp);
                    DEBUG_INFO("Timestamp from metadata: %s", timestamp);
                }
                
                /* Add dimensions */
                written += snprintf(metadata_json + written, metadata_json_size - written,
                                  ",\"dimensions\":%u", dimensions);
                /* Add file_type */
                written += snprintf(metadata_json + written, metadata_json_size - written,
                                  ",\"file_type\":\"%s\"}", file_type);
                
                DEBUG_INFO("Created metadata JSON: %s", metadata_json);
            }
            
            /* Free the individual strings */
            if (source_file) free(source_file);
            if (model) free(model);
            if (timestamp) free(timestamp);
            
            /* Log embedding info */
            DEBUG_INFO("Embedding hash: %s, dimensions: %u", id_str, dimensions);
        } else {
            DEBUG_WARN("No metadata file found for hash %s", id_str);
            /* Create minimal metadata JSON with just the hash and dimensions */
            metadata_json_size = 256;
            metadata_json = (char*)malloc(metadata_json_size);
            if (metadata_json) {
                snprintf(metadata_json, metadata_json_size, 
                        "{\"hash\":\"%s\",\"dimensions\":%u,\"file_type\":\"%s\"}", id_str, dimensions, file_type);
            }
        }
    } else {
        /* Create minimal metadata JSON with just dimensions */
        metadata_json_size = 64;
        metadata_json = (char*)malloc(metadata_json_size);
        if (metadata_json) {
            snprintf(metadata_json, metadata_json_size, "{\"dimensions\":%u,\"file_type\":\"%s\"}", dimensions, file_type);
        }
    }
    
    /* Default empty JSON if allocation failed */
    if (!metadata_json) {
        metadata_json = strdup("{}");
        metadata_json_size = 2;
    }

    /* Log the metadata_json string before writing to Parquet
    DEBUG_INFO("Final metadata_json to be written to Parquet: %s", metadata_json);

    /* Create list array builder using the list data type */
    GArrowListArrayBuilder* list_builder = NULL;
    GError* list_builder_error = NULL;
    list_builder = garrow_list_array_builder_new(
        GARROW_LIST_DATA_TYPE(list_type),
        &list_builder_error);

    if (!list_builder) {
        DEBUG_ERROR("Failed to create list array builder: %s", 
                   list_builder_error ? list_builder_error->message : "Unknown error");
        if (list_builder_error) g_error_free(list_builder_error);
        g_object_unref(float_field);
        g_object_unref(float_type);
        g_object_unref(list_type);
        g_object_unref(values_field);
        g_object_unref(id_field);
        g_object_unref(metadata_field);
        g_object_unref(blob_field);
        g_object_unref(schema);
        if (metadata_json) free(metadata_json);
        if (need_to_free_decompressed) free(decompressed_data);
        return EB_ERROR_IO;
    }

    /* Get the value builder from the list builder */
    GArrowArrayBuilder* value_builder = garrow_list_array_builder_get_value_builder(list_builder);
    GArrowFloatArrayBuilder* float_builder = GARROW_FLOAT_ARRAY_BUILDER(value_builder);
    
    g_object_unref(float_field);
    g_object_unref(float_type);
    g_object_unref(list_type);
    g_object_unref(values_field);
    g_object_unref(id_field);
    g_object_unref(metadata_field);
    g_object_unref(blob_field);
    
    /* Create arrays for each column */
    /* ID array (single string - the hash) */
    GArrowStringArrayBuilder* id_builder = garrow_string_array_builder_new();
    garrow_string_array_builder_append_string(id_builder, id_str, &error);
        if (error) {
        DEBUG_ERROR("Failed to append ID value: %s", error->message);
        g_object_unref(id_builder);
        g_object_unref(schema);
        g_error_free(error);
        if (metadata_json) free(metadata_json);
        if (need_to_free_decompressed) free(decompressed_data);
        return EB_ERROR_IO;
        }
        
    GArrowArray* id_array = GARROW_ARRAY(garrow_array_builder_finish(
        GARROW_ARRAY_BUILDER(id_builder), &error));
    g_object_unref(id_builder);
    if (error) {
        DEBUG_ERROR("Failed to finish ID array: %s", error->message);
        g_object_unref(schema);
        g_error_free(error);
        if (metadata_json) free(metadata_json);
        if (need_to_free_decompressed) free(decompressed_data);
        return EB_ERROR_IO;
        }
        
    /* Start a new list element */
    garrow_list_array_builder_append_value(list_builder, &error);
        if (error) {
        DEBUG_ERROR("Failed to append list start: %s", error->message);
        g_object_unref(id_array);
        g_object_unref(float_builder);
        g_object_unref(list_builder);
        g_object_unref(schema);
        g_error_free(error);
        if (metadata_json) free(metadata_json);
        if (need_to_free_decompressed) free(decompressed_data);
        return EB_ERROR_IO;
        }
        
    /* Add each float to the list */
    for (uint32_t i = 0; i < dimensions; i++) {
        garrow_float_array_builder_append_value(float_builder, values[i], &error);
        if (error) {
            DEBUG_ERROR("Failed to append float value at index %u: %s", i, error->message);
            g_object_unref(id_array);
            g_object_unref(float_builder);
            g_object_unref(list_builder);
            g_object_unref(schema);
            g_error_free(error);
            if (metadata_json) free(metadata_json);
            if (need_to_free_decompressed) free(decompressed_data);
            return EB_ERROR_IO;
        }
    }
    
    GArrowArray* values_array = GARROW_ARRAY(garrow_array_builder_finish(
        GARROW_ARRAY_BUILDER(list_builder), &error));
    g_object_unref(list_builder);
    g_object_unref(float_builder);
    
    if (error) {
        DEBUG_ERROR("Failed to finish values array: %s", error->message);
        g_object_unref(id_array);
        g_object_unref(schema);
        g_error_free(error);
        if (metadata_json) free(metadata_json);
        if (need_to_free_decompressed) free(decompressed_data);
        return EB_ERROR_IO;
    }
    
    /* Create metadata array (single string) */
    GArrowStringArrayBuilder* metadata_builder = garrow_string_array_builder_new();
    garrow_string_array_builder_append_string(metadata_builder, metadata_json, &error);
    if (error) {
        DEBUG_ERROR("Failed to append metadata value: %s", error->message);
        g_object_unref(id_array);
        g_object_unref(values_array);
        g_object_unref(metadata_builder);
        g_object_unref(schema);
        g_error_free(error);
        if (metadata_json) free(metadata_json);
        if (need_to_free_decompressed) free(decompressed_data);
        return EB_ERROR_IO;
    }
    
    GArrowArray* metadata_array = GARROW_ARRAY(garrow_array_builder_finish(
        GARROW_ARRAY_BUILDER(metadata_builder), &error));
    g_object_unref(metadata_builder);
    if (error) {
        DEBUG_ERROR("Failed to finish metadata array: %s", error->message);
        g_object_unref(id_array);
        g_object_unref(values_array);
        g_object_unref(schema);
        g_error_free(error);
        if (metadata_json) free(metadata_json);
        if (need_to_free_decompressed) free(decompressed_data);
        return EB_ERROR_IO;
    }
    
    /* Free metadata JSON string */
    if (metadata_json) free(metadata_json);
    
    /* Create blob array (single string) */
    GArrowStringArrayBuilder* blob_builder = garrow_string_array_builder_new();

    /* Create blob JSON */
    char* blob_json = NULL;
    if (tls_document_text) {
        /* Create JSON with document text */
        size_t blob_size = strlen(tls_document_text) + 50; /* Extra space for JSON format */
        blob_json = (char*)malloc(blob_size);
        if (blob_json) {
            snprintf(blob_json, blob_size, "{\"text\":\"%s\"}", tls_document_text);
        }
        
        /* Clear TLS after use */
        free(tls_document_text);
        tls_document_text = NULL;
    } else {
        /* No document text available */
        blob_json = strdup("{}");
    }

    if (!blob_json) {
        blob_json = strdup("{}");
    }

    garrow_string_array_builder_append_string(blob_builder, blob_json, &error);
    if (error) {
        DEBUG_ERROR("Failed to append blob value: %s", error->message);
        g_object_unref(blob_builder);
        free(blob_json);
        g_object_unref(id_array);
        g_object_unref(values_array);
        g_object_unref(metadata_array);
        g_object_unref(schema);
        g_error_free(error);
        if (need_to_free_decompressed) free(decompressed_data);
        return EB_ERROR_IO;
    }

    GArrowArray* blob_array = GARROW_ARRAY(garrow_array_builder_finish(
        GARROW_ARRAY_BUILDER(blob_builder), &error));
    g_object_unref(blob_builder);
    free(blob_json);

    if (error) {
        DEBUG_ERROR("Failed to finish blob array: %s", error->message);
        g_object_unref(id_array);
        g_object_unref(values_array);
        g_object_unref(metadata_array);
        g_object_unref(schema);
        g_error_free(error);
        if (need_to_free_decompressed) free(decompressed_data);
        return EB_ERROR_IO;
    }
    
    /* Create record batch from arrays */
    GList* arrays = NULL;
    arrays = g_list_append(arrays, id_array);
    arrays = g_list_append(arrays, values_array);
    arrays = g_list_append(arrays, metadata_array);
    arrays = g_list_append(arrays, blob_array);
    
    GArrowRecordBatch* record_batch = garrow_record_batch_new(schema, 1, arrays, &error);
    if (error) {
        DEBUG_ERROR("Failed to create record batch: %s", error->message);
        g_list_free_full(arrays, g_object_unref);
        g_object_unref(schema);
        g_error_free(error);
        if (need_to_free_decompressed) free(decompressed_data);
        return EB_ERROR_IO;
    }
    g_list_free(arrays);
    
    /* Create table from record batch */
    GList* batches = g_list_append(NULL, record_batch);
    GArrowTable* table = garrow_table_new_record_batches(schema, &record_batch, 1, &error);
    g_object_unref(record_batch);
    g_list_free(batches);
    
    if (error) {
        DEBUG_ERROR("Failed to create table: %s", error->message);
        g_object_unref(schema);
        g_error_free(error);
        if (need_to_free_decompressed) free(decompressed_data);
        return EB_ERROR_IO;
    }
    
    /* Create file output stream for the result - we'll write to a temporary file */
    const gchar *temp_filename = "temp_parquet_data.parquet";
    DEBUG_INFO("Opening Parquet output file for writing: %s", temp_filename);
    GArrowFileOutputStream *output_stream = garrow_file_output_stream_new(temp_filename, FALSE, &error);
    if (!output_stream) {
        DEBUG_ERROR("Failed to create file output stream: %s", error ? error->message : "Unknown error");
        g_object_unref(table);
        g_object_unref(schema);
        if (error) g_error_free(error);
        if (need_to_free_decompressed) free(decompressed_data);
        return EB_ERROR_IO;
    }
    
    /* Set up Parquet writer options */
    GArrowCompressionType compression_type = GARROW_COMPRESSION_TYPE_ZSTD;
    GParquetWriterProperties *writer_props = gparquet_writer_properties_new();
    gparquet_writer_properties_set_compression(writer_props, compression_type, "*");
    
    /* Create Parquet writer and write table */
    GParquetArrowFileWriter *writer = gparquet_arrow_file_writer_new_arrow(
        schema,
                                             GARROW_OUTPUT_STREAM(output_stream),
                                             writer_props,
                                             &error);
    
    g_object_unref(writer_props);
    g_object_unref(output_stream);
    g_object_unref(schema);
    
    if (!writer) {
        DEBUG_ERROR("Failed to create Parquet writer: %s", error ? error->message : "Unknown error");
        g_object_unref(table);
        if (error) g_error_free(error);
        if (need_to_free_decompressed) free(decompressed_data);
        return EB_ERROR_IO;
    }
    
    gboolean success = gparquet_arrow_file_writer_write_table(
        writer, table, 1024, &error);
    DEBUG_INFO("Finished writing Parquet table to file: %s", temp_filename);
    g_object_unref(table);
    
    if (!success) {
        DEBUG_ERROR("Failed to write table: %s", error ? error->message : "Unknown error");
        g_object_unref(writer);
        if (error) g_error_free(error);
        if (need_to_free_decompressed) free(decompressed_data);
        return EB_ERROR_IO;
    }
    
    success = gparquet_arrow_file_writer_close(writer, &error);
    DEBUG_INFO("Closed Parquet writer for file: %s", temp_filename);
    g_object_unref(writer);
    
    /* Read the temporary Parquet file into memory */
    DEBUG_INFO("Opening Parquet file for reading: %s", temp_filename);
    FILE *parquet_file = fopen(temp_filename, "rb");
    if (!parquet_file) {
        DEBUG_ERROR("Failed to open temporary Parquet file");
        if (need_to_free_decompressed) free(decompressed_data);
        return EB_ERROR_IO;
    }
    
    /* Get file size */
    fseek(parquet_file, 0, SEEK_END);
    *dest_size = ftell(parquet_file);
    fseek(parquet_file, 0, SEEK_SET);
    
    /* Allocate memory for the Parquet data */
    *dest = malloc(*dest_size);
    if (!*dest) {
        DEBUG_ERROR("Failed to allocate memory for Parquet data");
        fclose(parquet_file);
        if (need_to_free_decompressed) free(decompressed_data);
        return EB_ERROR_MEMORY;
    }
    
    /* Read the Parquet data */
    DEBUG_INFO("Reading Parquet file into memory: %s", temp_filename);
    if (fread(*dest, 1, *dest_size, parquet_file) != *dest_size) {
        DEBUG_ERROR("Failed to read temporary Parquet file");
        fclose(parquet_file);
        free(*dest);
        *dest = NULL;
        if (need_to_free_decompressed) free(decompressed_data);
        return EB_ERROR_IO;
    }
    
    fclose(parquet_file);
    DEBUG_INFO("Closed Parquet file after reading: %s", temp_filename);
    
    /* Clean up */
    if (need_to_free_decompressed) {
        free(decompressed_data);
    }
    
    DEBUG_INFO("Removing temporary Parquet file: %s", temp_filename);
    remove(temp_filename);
    
    DEBUG_INFO("Successfully transformed to Pinecone-compatible Parquet format, size: %zu bytes", *dest_size);
    return EB_SUCCESS;
}

/* 
 * Inverse transform Parquet data back to original format
 * Converts Parquet columnar format back to the original binary or NumPy data
 */
static eb_status_t parquet_inverse_transform(struct eb_transformer* transformer, 
                                           const void* source, size_t source_size, 
                                           void** dest, size_t* dest_size) {
    if (!transformer || !source || !dest || !dest_size) {
        return EB_ERROR_INVALID_PARAMETER;
    }

    parquet_transformer_config_t* config = get_config(transformer);
    if (!config) {
        return EB_ERROR_CONFIG;
    }

    DEBUG_INFO("Inverse transforming data from Parquet format");
    
    GError *error = NULL;
    
    /* Write Parquet data to a temporary file */
    const gchar *temp_filename = "temp_parquet_data.parquet";
    FILE *temp_file = fopen(temp_filename, "wb");
    if (!temp_file) {
        DEBUG_ERROR("Failed to create temporary file");
        return EB_ERROR_IO;
    }
    
    /* Write data to temporary file */
    size_t bytes_written = fwrite(source, 1, source_size, temp_file);
    fclose(temp_file);
    
    if (bytes_written != source_size) {
        DEBUG_ERROR("Failed to write to temporary file");
        return EB_ERROR_IO;
    }
    
    /* Open file as Arrow file input stream */
    GArrowMemoryMappedInputStream *input_stream = 
        garrow_memory_mapped_input_stream_new(temp_filename, &error);
    
    if (!input_stream) {
        DEBUG_ERROR("Failed to create input stream: %s", error ? error->message : "Unknown error");
        if (error) g_error_free(error);
        remove(temp_filename);
        return EB_ERROR_IO;
    }
    
    /* Create Parquet reader */
    GParquetArrowFileReader *reader = 
        gparquet_arrow_file_reader_new_arrow(GARROW_SEEKABLE_INPUT_STREAM(input_stream), &error);
    
    if (!reader) {
        DEBUG_ERROR("Failed to create Parquet reader: %s", error ? error->message : "Unknown error");
        g_object_unref(input_stream);
        if (error) g_error_free(error);
        remove(temp_filename);
        return EB_ERROR_IO;
    }
        
    /* Read the table */
    GArrowTable *table = gparquet_arrow_file_reader_read_table(reader, &error);
    g_object_unref(reader);
    g_object_unref(input_stream);
    
    /* Delete temporary file */
    remove(temp_filename);
    
    if (!table) {
        DEBUG_ERROR("Failed to read Parquet table: %s", error ? error->message : "Unknown error");
        if (error) g_error_free(error);
        return EB_ERROR_IO;
    }
        
    /* Get the 'values' column for raw payload (second column) */
    GArrowChunkedArray *chunked_array = garrow_table_get_column_data(table, 1);
    if (!chunked_array) {
        DEBUG_ERROR("Failed to get column from table");
        g_object_unref(table);
        return EB_ERROR_IO;
    }
        
    /* For simplicity, assume only one chunk */
    guint n_chunks = garrow_chunked_array_get_n_chunks(chunked_array);
    if (n_chunks == 0) {
        DEBUG_ERROR("No chunks in array");
        g_object_unref(chunked_array);
        g_object_unref(table);
        return EB_ERROR_IO;
    }
    
    GArrowArray *array = garrow_chunked_array_get_chunk(chunked_array, 0);
    g_object_unref(chunked_array);
    if (!array) {
        DEBUG_ERROR("Failed to get chunk from chunked array");
        g_object_unref(table);
        return EB_ERROR_IO;
    }
    
    /* Get the 'values' field from schema (index 1) */
    GArrowSchema *schema = garrow_table_get_schema(table);
    if (!schema) {
        DEBUG_ERROR("Failed to get schema from table");
        if (array && G_IS_OBJECT(array)) g_object_unref(array);
        if (table && G_IS_OBJECT(table)) g_object_unref(table);
        return EB_ERROR_IO;
    }
    
    GArrowField *field = garrow_schema_get_field(schema, 1);
    if (!field) {
        DEBUG_ERROR("Failed to get field from schema");
        if (schema && G_IS_OBJECT(schema)) g_object_unref(schema);
        if (array && G_IS_OBJECT(array)) g_object_unref(array);
        if (table && G_IS_OBJECT(table)) g_object_unref(table);
        return EB_ERROR_IO;
    }
    
    GArrowDataType *data_type = garrow_field_get_data_type(field);
    if (!data_type) {
        DEBUG_ERROR("Failed to get data type from field");
        if (field && G_IS_OBJECT(field)) g_object_unref(field);
        if (schema && G_IS_OBJECT(schema)) g_object_unref(schema);
        if (array && G_IS_OBJECT(array)) g_object_unref(array);
        if (table && G_IS_OBJECT(table)) g_object_unref(table);
        return EB_ERROR_IO;
    }
    
    GArrowType type = garrow_data_type_get_id(data_type);
    
    /* Debug: log which column and type id we're processing */
    DEBUG_INFO("InverseTransform: column='%s', type_id=%d", garrow_field_get_name(field), type);

    /* Process based on type */
    if (type == GARROW_TYPE_FLOAT) {
        /* This was likely a NumPy array - need to reconstruct it */
        GArrowFloatArray *float_array = GARROW_FLOAT_ARRAY(array);
        if (!float_array) {
            DEBUG_ERROR("Failed to cast to float array");
            if (data_type && G_IS_OBJECT(data_type)) g_object_unref(data_type);
            if (field && G_IS_OBJECT(field)) g_object_unref(field);
            if (schema && G_IS_OBJECT(schema)) g_object_unref(schema);
            if (array && G_IS_OBJECT(array)) g_object_unref(array);
            if (table && G_IS_OBJECT(table)) g_object_unref(table);
            return EB_ERROR_IO;
        }
        int64_t length = garrow_array_get_length(array);
        DEBUG_INFO("About to call garrow_float_array_get_values");
        const gfloat *all_values = garrow_float_array_get_values(float_array, NULL);
        DEBUG_INFO("Returned from garrow_float_array_get_values");
        DEBUG_INFO("values pointer: %p", (void*)all_values);
        if (!all_values) {
            DEBUG_ERROR("Failed to get values from float array");
            if (data_type && G_IS_OBJECT(data_type)) g_object_unref(data_type);
            if (field && G_IS_OBJECT(field)) g_object_unref(field);
            if (schema && G_IS_OBJECT(schema)) g_object_unref(schema);
            if (array && G_IS_OBJECT(array)) g_object_unref(array);
            if (table && G_IS_OBJECT(table)) g_object_unref(table);
            return EB_ERROR_IO;
        }
        gsize data_size = length * sizeof(gfloat);
            
        /* Build and pad NumPy header per spec (magic+ver+len+header)%64==0 */
        char header_buf[256];
        memset(header_buf, ' ', sizeof(header_buf));
        int header_text_len = snprintf(header_buf, sizeof(header_buf),
            "{'descr': '<f4', 'fortran_order': False, 'shape': (%ld,)}",
            (long)length);
        int header_len = header_text_len;
        int pad = (64 - ((10 + header_len + 1) % 64)) % 64;
        memset(header_buf + header_text_len, ' ', pad);
        header_len += pad;
        header_buf[header_len] = '\n';
        header_len += 1;
        /* DEBUG: inspect header padding */
        DEBUG_INFO("NumPy header build: text_len=%d, pad=%d, header_len=%d", header_text_len, pad, header_len);
        for (int di = 0; di < 16 && di < header_len; ++di) {
            DEBUG_INFO(" header_buf[%d] = 0x%02x", di, (unsigned char)header_buf[di]);
        }
        DEBUG_INFO(" header_buf[%d] = 0x%02x (newline)", header_len - 1, (unsigned char)header_buf[header_len - 1]);
        /* Allocate and write output: magic (8) + hdrlen (2) + header + data */
        *dest_size = 10 + header_len + data_size;
        *dest = malloc(*dest_size);
        if (!*dest) {
            DEBUG_ERROR("Failed to allocate memory for NumPy output");
            if (data_type && G_IS_OBJECT(data_type)) g_object_unref(data_type);
            if (field && G_IS_OBJECT(field)) g_object_unref(field);
            if (schema && G_IS_OBJECT(schema)) g_object_unref(schema);
            if (array && G_IS_OBJECT(array)) g_object_unref(array);
            if (table && G_IS_OBJECT(table)) g_object_unref(table);
            return EB_ERROR_MEMORY;
        }
        /* Write magic and version */
        memcpy(*dest, "\x93NUMPY\x01\x00", 8);
        /* Header length field */
        guint16* header_len_ptr = (guint16*)((guint8*)*dest + 8);
        *header_len_ptr = (guint16)header_len;
        /* Copy header content */
        memcpy((guint8*)*dest + 10, header_buf, header_len);
        /* Copy data values */
        memcpy((guint8*)*dest + 10 + header_len, all_values, data_size);
            
        DEBUG_INFO("Reconstructed NumPy array with %ld elements", (long)length);
    } else if (type == GARROW_TYPE_BINARY) {
        /* This was binary data */
        GArrowBinaryArray *binary_array = GARROW_BINARY_ARRAY(array);
        if (!binary_array) {
            DEBUG_ERROR("Failed to cast to binary array");
            if (data_type && G_IS_OBJECT(data_type)) g_object_unref(data_type);
            if (field && G_IS_OBJECT(field)) g_object_unref(field);
            if (schema && G_IS_OBJECT(schema)) g_object_unref(schema);
            if (array && G_IS_OBJECT(array)) g_object_unref(array);
            if (table && G_IS_OBJECT(table)) g_object_unref(table);
            return EB_ERROR_IO;
        }
            
        /* Get the data from the binary array */
        guint32 length = garrow_array_get_length(array);
        if (length == 0) {
            DEBUG_ERROR("Empty binary array in Parquet file");
            if (data_type && G_IS_OBJECT(data_type)) g_object_unref(data_type);
            if (field && G_IS_OBJECT(field)) g_object_unref(field);
            if (schema && G_IS_OBJECT(schema)) g_object_unref(schema);
            if (array && G_IS_OBJECT(array)) g_object_unref(array);
            if (table && G_IS_OBJECT(table)) g_object_unref(table);
            return EB_ERROR_IO;
        }
            
        /* Get the data from the first binary value */
        GBytes *value = garrow_binary_array_get_value(binary_array, 0);
        if (!value) {
            DEBUG_ERROR("Failed to get binary value");
            if (data_type && G_IS_OBJECT(data_type)) g_object_unref(data_type);
            if (field && G_IS_OBJECT(field)) g_object_unref(field);
            if (schema && G_IS_OBJECT(schema)) g_object_unref(schema);
            if (array && G_IS_OBJECT(array)) g_object_unref(array);
            if (table && G_IS_OBJECT(table)) g_object_unref(table);
            return EB_ERROR_IO;
        }
        
        /* Get size and data */
        gsize value_size;
        const guint8 *value_data = g_bytes_get_data(value, &value_size);
        if (!value_data) {
            DEBUG_ERROR("Failed to get data from GBytes");
            g_bytes_unref(value);
            if (data_type && G_IS_OBJECT(data_type)) g_object_unref(data_type);
            if (field && G_IS_OBJECT(field)) g_object_unref(field);
            if (schema && G_IS_OBJECT(schema)) g_object_unref(schema);
            if (array && G_IS_OBJECT(array)) g_object_unref(array);
            if (table && G_IS_OBJECT(table)) g_object_unref(table);
            return EB_ERROR_IO;
        }
            
        /* Allocate memory for result */
        *dest_size = value_size;
        *dest = malloc(*dest_size);
        if (!*dest) {
            DEBUG_ERROR("Failed to allocate memory for binary output");
            g_bytes_unref(value);
            if (data_type && G_IS_OBJECT(data_type)) g_object_unref(data_type);
            if (field && G_IS_OBJECT(field)) g_object_unref(field);
            if (schema && G_IS_OBJECT(schema)) g_object_unref(schema);
            if (array && G_IS_OBJECT(array)) g_object_unref(array);
            if (table && G_IS_OBJECT(table)) g_object_unref(table);
            return EB_ERROR_MEMORY;
        }
            
        /* Copy binary data */
        memcpy(*dest, value_data, *dest_size);
        g_bytes_unref(value);
            
        DEBUG_INFO("Extracted binary data (%zu bytes)", *dest_size);
    } else if (type == GARROW_TYPE_LIST) {
        DEBUG_INFO("Entering GARROW_TYPE_LIST branch");
        GArrowListArray *list_array = GARROW_LIST_ARRAY(array);
        DEBUG_INFO("list_array pointer: %p", (void*)list_array);
        if (!list_array) {
            DEBUG_ERROR("Failed to cast to list array");
            if (data_type && G_IS_OBJECT(data_type)) g_object_unref(data_type);
            if (field && G_IS_OBJECT(field)) g_object_unref(field);
            if (schema && G_IS_OBJECT(schema)) g_object_unref(schema);
            if (array && G_IS_OBJECT(array)) g_object_unref(array);
            if (table && G_IS_OBJECT(table)) g_object_unref(table);
            return EB_ERROR_IO;
        }
        gint64 n_list = garrow_array_get_length(GARROW_ARRAY(list_array));
        DEBUG_INFO("list_array length: %ld", (long)n_list);
        GArrowArray *elem_array = garrow_list_array_get_value(list_array, 0);
        DEBUG_INFO("elem_array pointer: %p", (void*)elem_array);
        if (!elem_array) {
            DEBUG_ERROR("Failed to get list element array");
            if (data_type && G_IS_OBJECT(data_type)) g_object_unref(data_type);
            if (field && G_IS_OBJECT(field)) g_object_unref(field);
            if (schema && G_IS_OBJECT(schema)) g_object_unref(schema);
            if (array && G_IS_OBJECT(array)) g_object_unref(array);
            if (table && G_IS_OBJECT(table)) g_object_unref(table);
            return EB_ERROR_IO;
        }
        gint64 elem_length = garrow_array_get_length(elem_array);
        DEBUG_INFO("elem_array length: %ld", (long)elem_length);
        GArrowFloatArray *float_array = GARROW_FLOAT_ARRAY(elem_array);
        DEBUG_INFO("float_array pointer: %p", (void*)float_array);
        if (!float_array) {
            DEBUG_ERROR("Failed to cast element to float array");
            g_object_unref(elem_array);
            if (data_type && G_IS_OBJECT(data_type)) g_object_unref(data_type);
            if (field && G_IS_OBJECT(field)) g_object_unref(field);
            if (schema && G_IS_OBJECT(schema)) g_object_unref(schema);
            if (array && G_IS_OBJECT(array)) g_object_unref(array);
            if (table && G_IS_OBJECT(table)) g_object_unref(table);
            return EB_ERROR_IO;
        }
        // Instead of using garrow_float_array_get_values (which may not be safe), extract values one by one.
        float *values_buf = malloc(elem_length * sizeof(float));
        if (!values_buf) {
            DEBUG_ERROR("Failed to allocate memory for float values buffer");
            g_object_unref(elem_array);
            if (data_type && G_IS_OBJECT(data_type)) g_object_unref(data_type);
            if (field && G_IS_OBJECT(field)) g_object_unref(field);
            if (schema && G_IS_OBJECT(schema)) g_object_unref(schema);
            if (array && G_IS_OBJECT(array)) g_object_unref(array);
            if (table && G_IS_OBJECT(table)) g_object_unref(table);
            return EB_ERROR_MEMORY;
        }
        for (gint64 i = 0; i < elem_length; i++) {
            values_buf[i] = garrow_float_array_get_value(float_array, i);
        }
        // ... use values_buf instead of values ...
        /* Build and pad NumPy header per spec (magic+ver+len+header)%64==0 */
        char header_buf2[256];
        memset(header_buf2, ' ', sizeof(header_buf2));
        int header_text_len2 = snprintf(header_buf2, sizeof(header_buf2),
            "{'descr': '<f4', 'fortran_order': False, 'shape': (%ld,)}",
            (long)elem_length);
        int header_len2 = header_text_len2;
        int pad2 = (64 - ((10 + header_len2 + 1) % 64)) % 64;
        memset(header_buf2 + header_text_len2, ' ', pad2);
        header_len2 += pad2;
        header_buf2[header_len2] = '\n';
        header_len2 += 1;
        /* DEBUG: inspect header2 padding */
        DEBUG_INFO("NumPy header2 build: text_len2=%d, pad2=%d, header_len2=%d", header_text_len2, pad2, header_len2);
        for (int di2 = 0; di2 < 16 && di2 < header_len2; ++di2) {
            DEBUG_INFO(" header_buf2[%d] = 0x%02x", di2, (unsigned char)header_buf2[di2]);
        }
        DEBUG_INFO(" header_buf2[%d] = 0x%02x (newline)", header_len2 - 1, (unsigned char)header_buf2[header_len2 - 1]);
        /* Allocate and write output: magic (8) + hdrlen (2) + header + data */
        gsize data_size2 = elem_length * sizeof(float);
        *dest_size = 10 + header_len2 + data_size2;
        *dest = malloc(*dest_size);
        if (!*dest) {
            DEBUG_ERROR("Failed to allocate memory for NumPy output");
            free(values_buf);
            g_object_unref(elem_array);
            return EB_ERROR_MEMORY;
        }
        /* Write magic and version */
        memcpy(*dest, "\x93NUMPY\x01\x00", 8);
        /* Header length field */
        guint16 *hdr_len_ptr = (guint16*)((guint8*)*dest + 8);
        *hdr_len_ptr = (guint16)header_len2;
        /* Copy header content */
        memcpy((guint8*)*dest + 10, header_buf2, header_len2);
        /* Copy data values */
        memcpy((guint8*)*dest + 10 + header_len2, values_buf, data_size2);
        DEBUG_INFO("Reconstructed NumPy array from list with %ld elements", (long)elem_length);
        free(values_buf);
        g_object_unref(elem_array);
    } else {
        DEBUG_ERROR("Unsupported data type in Parquet file: %d", type);
        if (data_type && G_IS_OBJECT(data_type)) g_object_unref(data_type);
        if (field && G_IS_OBJECT(field)) g_object_unref(field);
        if (schema && G_IS_OBJECT(schema)) g_object_unref(schema);
        if (array && G_IS_OBJECT(array)) g_object_unref(array);
        if (table && G_IS_OBJECT(table)) g_object_unref(table);
        return EB_ERROR_IO;
    }
    
    /* Extract metadata from schema if present */
    char* hash_str = NULL;
    char* source_str = NULL;
    char* model_str = NULL;
    char* timestamp_str = NULL;
    char* dimensions_str = NULL;
    
    /* First try to extract from metadata column if present */
    gint metadata_col_index = garrow_table_get_n_columns(table) > 2 ? 2 : -1;
    if (metadata_col_index >= 0) {
        DEBUG_INFO("Found metadata column, extracting values");
        GArrowChunkedArray *metadata_chunked = garrow_table_get_column_data(table, metadata_col_index);
        if (metadata_chunked && garrow_chunked_array_get_n_chunks(metadata_chunked) > 0) {
            GArrowArray *metadata_array = garrow_chunked_array_get_chunk(metadata_chunked, 0);
            if (metadata_array) {
                GArrowStringArray *metadata_string_array = GARROW_STRING_ARRAY(metadata_array);
                if (metadata_string_array) {
                    const gchar *metadata_json = garrow_string_array_get_string(metadata_string_array, 0);
                    if (metadata_json) {
                        DEBUG_INFO("Metadata JSON: %s", metadata_json);
                        
                        /* Parse JSON-like string to extract values */
                        /* This is a very simple parser, just to extract key-value pairs */
                        const char* hash_start = strstr(metadata_json, "\"hash\":\"");
                        if (hash_start) {
                            hash_start += 8; /* Skip "hash":"" */
                            const char* hash_end = strchr(hash_start, '\"');
                            if (hash_end) {
                                size_t hash_len = hash_end - hash_start;
                                hash_str = (char*)malloc(hash_len + 1);
                                if (hash_str) {
                                    strncpy(hash_str, hash_start, hash_len);
                                    hash_str[hash_len] = '\0';
                                }
                            }
                        }
                        
                        const char* source_start = strstr(metadata_json, "\"source\":\"");
                        if (source_start) {
                            source_start += 10; /* Skip "source":"" */
                            const char* source_end = strchr(source_start, '\"');
                            if (source_end) {
                                size_t source_len = source_end - source_start;
                                source_str = (char*)malloc(source_len + 1);
                                if (source_str) {
                                    strncpy(source_str, source_start, source_len);
                                    source_str[source_len] = '\0';
                                }
                            }
                        }
                        
                        const char* model_start = strstr(metadata_json, "\"model\":\"");
                        if (model_start) {
                            model_start += 9; /* Skip "model":"" */
                            const char* model_end = strchr(model_start, '\"');
                            if (model_end) {
                                size_t model_len = model_end - model_start;
                                model_str = (char*)malloc(model_len + 1);
                                if (model_str) {
                                    strncpy(model_str, model_start, model_len);
                                    model_str[model_len] = '\0';
                                }
                            }
                        }
                        
                        const char* timestamp_start = strstr(metadata_json, "\"timestamp\":\"");
                        if (timestamp_start) {
                            timestamp_start += 13; /* Skip "timestamp":"" */
                            const char* timestamp_end = strchr(timestamp_start, '\"');
                            if (timestamp_end) {
                                size_t timestamp_len = timestamp_end - timestamp_start;
                                timestamp_str = (char*)malloc(timestamp_len + 1);
                                if (timestamp_str) {
                                    strncpy(timestamp_str, timestamp_start, timestamp_len);
                                    timestamp_str[timestamp_len] = '\0';
                                }
                            }
                        }
                        
                        const char* dimensions_start = strstr(metadata_json, "\"dimensions\":");
                        if (dimensions_start) {
                            dimensions_start += 13; /* Skip "dimensions": */
                            const char* dimensions_end = strchr(dimensions_start, '}');
                            if (!dimensions_end) dimensions_end = strchr(dimensions_start, ',');
                            if (dimensions_end) {
                                size_t dimensions_len = dimensions_end - dimensions_start;
                                dimensions_str = (char*)malloc(dimensions_len + 1);
                                if (dimensions_str) {
                                    strncpy(dimensions_str, dimensions_start, dimensions_len);
                                    dimensions_str[dimensions_len] = '\0';
                                }
                            }
                        }
                    }
                }
                g_object_unref(metadata_array);
            }
            g_object_unref(metadata_chunked);
        }
    } else {
        /* Fall back to schema metadata if no metadata column is found */
        extract_schema_metadata(schema, &hash_str, &source_str, &model_str, &timestamp_str, &dimensions_str);
    }
    
    if (hash_str) {
        DEBUG_INFO("Extracted metadata - hash: %s", hash_str);
    }
    if (source_str) {
        DEBUG_INFO("Extracted metadata - source: %s", source_str);
    }
    if (model_str) {
        DEBUG_INFO("Extracted metadata - model: %s", model_str);
    }
    
    /* Extract blob if present */
    gint blob_col_index = garrow_table_get_n_columns(table) > 3 ? 3 : -1;
    if (blob_col_index >= 0) {
        DEBUG_INFO("Found blob column, extracting values");
        GArrowChunkedArray *blob_chunked = garrow_table_get_column_data(table, blob_col_index);
        if (blob_chunked && garrow_chunked_array_get_n_chunks(blob_chunked) > 0) {
            GArrowArray *blob_array = garrow_chunked_array_get_chunk(blob_chunked, 0);
            if (blob_array) {
                GArrowStringArray *blob_string_array = GARROW_STRING_ARRAY(blob_array);
                if (blob_string_array) {
                    const gchar *blob_json = garrow_string_array_get_string(blob_string_array, 0);
                    if (blob_json) {
                        DEBUG_INFO("Blob JSON: %s", blob_json);
                        
                        /* Parse JSON to extract text if needed */
                        const char* text_start = strstr(blob_json, "\"text\":\"");
                        if (text_start) {
                            text_start += 8; /* Skip "text":"" */
                            const char* text_end = strchr(text_start, '\"');
                            if (text_end) {
                                size_t text_len = text_end - text_start;
                                
                                /* Store the document text for potential future use */
                                char* extracted_text = (char*)malloc(text_len + 1);
                                if (extracted_text) {
                                    strncpy(extracted_text, text_start, text_len);
                                    extracted_text[text_len] = '\0';
                                    
                                    /* Set as TLS for next operation if needed */
                                    eb_parquet_set_document_text(extracted_text);
                                    free(extracted_text);
                                }
                            }
                        }
                    }
                }
                g_object_unref(blob_array);
            }
            g_object_unref(blob_chunked);
        }
    }
    
    /* Free all objects in reverse order of creation, with proper NULL checks */
    if (data_type && G_IS_OBJECT(data_type)) g_object_unref(data_type);
    if (field && G_IS_OBJECT(field)) g_object_unref(field);
    if (schema && G_IS_OBJECT(schema)) g_object_unref(schema);
    if (array && G_IS_OBJECT(array)) g_object_unref(array);
    if (table && G_IS_OBJECT(table)) g_object_unref(table);
    
    /* Create or update metadata file if we have a hash */
    if (hash_str) {
        DEBUG_INFO("Writing metadata to .meta file for hash %s", hash_str);
        
        /* Write metadata file under .embr/objects in the repo root */
        char meta_path[PATH_MAX] = {0};
        char *repo_root = find_repo_root(NULL);
        if (repo_root) {
            char objects_dir[PATH_MAX];
            snprintf(objects_dir, sizeof(objects_dir), "%s/.embr/objects", repo_root);
            mkdir(objects_dir, 0755); /* ignore errors */
            snprintf(meta_path, sizeof(meta_path), "%s/%s.meta", objects_dir, hash_str);
            free(repo_root);
        } else {
            /* Fallback to current directory if repo root not found */
            snprintf(meta_path, sizeof(meta_path), "%s.meta", hash_str);
        }
        FILE *meta_file = fopen(meta_path, "w");
        if (!meta_file) {
            DEBUG_WARN("Failed to open metadata file for writing at %s", meta_path);
            goto cleanup_metadata;
        }
        
        /* Write metadata */
        if (source_str) {
            fprintf(meta_file, "source_file=%s\n", source_str);
        }
        
        if (timestamp_str) {
            fprintf(meta_file, "timestamp=%s\n", timestamp_str);
        } else {
            /* Add current timestamp if none exists */
            time_t now = time(NULL);
            fprintf(meta_file, "timestamp=%ld\n", now);
        }
        
        if (model_str) {
            fprintf(meta_file, "model=%s\n", model_str);
        }
        
        if (dimensions_str) {
            fprintf(meta_file, "dimensions=%s\n", dimensions_str);
        }
        
        fclose(meta_file);
        DEBUG_INFO("Metadata saved to %s", meta_path);
    }
    
cleanup_metadata:
    /* Free metadata strings */
    if (hash_str) free(hash_str);
    if (source_str) free(source_str);
    if (model_str) free(model_str);
    if (timestamp_str) free(timestamp_str);
    if (dimensions_str) free(dimensions_str);
    
    DEBUG_INFO("Successfully inverse transformed data from Parquet format");
    return EB_SUCCESS;
}

/* Free resources associated with the transformer */
static void parquet_transformer_free(struct eb_transformer* transformer) {
    if (!transformer) {
        return;
    }

    if (transformer->user_data) {
        free(transformer->user_data);
        transformer->user_data = NULL;
    }

    if (transformer->name) {
        free((void*)transformer->name);
        transformer->name = NULL;
    }

    if (transformer->format_name) {
        free((void*)transformer->format_name);
        transformer->format_name = NULL;
    }

    /* Don't free the transformer itself here, it will be freed by eb_transformer_free */
}

/* Clone an existing transformer */
static struct eb_transformer* parquet_transformer_clone(const struct eb_transformer* transformer) {
    if (!transformer) {
        return NULL;
    }

    parquet_transformer_config_t* config = (parquet_transformer_config_t*)transformer->user_data;
    if (!config) {
        return NULL;
    }

    return eb_parquet_transformer_create(config->compression_level);
}

/* Register the Parquet transformer */
eb_status_t eb_register_parquet_transformer(void) {
    eb_transformer_t* transformer = eb_parquet_transformer_create(9); /* Default compression level */
    if (!transformer) {
        DEBUG_ERROR("Failed to create Parquet transformer");
        return EB_ERROR_GENERIC;
    }

    eb_status_t status = eb_register_transformer(transformer);
    if (status != EB_SUCCESS) {
        DEBUG_ERROR("Failed to register Parquet transformer: %d", status);
        eb_transformer_free(transformer);
        return status;
    }
    
    DEBUG_INFO("Parquet transformer registered successfully");
    return EB_SUCCESS;
}

/*
 * Extract the metadata JSON string from the 'metadata' column of a Parquet file buffer.
 * Returns a malloc'd string (caller must free), or NULL on error.
 */
char *eb_parquet_extract_metadata_json(const void *parquet_data, size_t parquet_size) {
    if (!parquet_data || parquet_size == 0) return NULL;
    // Log the first and last 4 bytes (Parquet magic bytes)
    if (parquet_size >= 8) {
        const unsigned char *bytes = (const unsigned char *)parquet_data;
        DEBUG_INFO("Parquet buffer first 4 bytes: %02x %02x %02x %02x", bytes[0], bytes[1], bytes[2], bytes[3]);
        DEBUG_INFO("Parquet buffer last 4 bytes: %02x %02x %02x %02x", bytes[parquet_size-4], bytes[parquet_size-3], bytes[parquet_size-2], bytes[parquet_size-1]);
    } else {
        DEBUG_WARN("Parquet buffer too small to check magic bytes (size=%zu)", parquet_size);
    }
    GError *error = NULL;
    const gchar *temp_filename = "temp_extract_metadata.parquet";
    FILE *temp_file = fopen(temp_filename, "wb");
    if (!temp_file) return NULL;
    size_t written = fwrite(parquet_data, 1, parquet_size, temp_file);
    fclose(temp_file);
    if (written != parquet_size) { remove(temp_filename); return NULL; }
    GArrowMemoryMappedInputStream *input_stream = garrow_memory_mapped_input_stream_new(temp_filename, &error);
    if (!input_stream) { remove(temp_filename); return NULL; }
    GParquetArrowFileReader *reader = gparquet_arrow_file_reader_new_arrow(GARROW_SEEKABLE_INPUT_STREAM(input_stream), &error);
    if (!reader) { g_object_unref(input_stream); remove(temp_filename); return NULL; }
    GArrowTable *table = gparquet_arrow_file_reader_read_table(reader, &error);
    g_object_unref(reader); g_object_unref(input_stream); remove(temp_filename);
    if (!table) return NULL;
    gint metadata_col_index = garrow_table_get_n_columns(table) > 2 ? 2 : -1;
    char *result = NULL;
    if (metadata_col_index >= 0) {
        GArrowChunkedArray *metadata_chunked = garrow_table_get_column_data(table, metadata_col_index);
        if (metadata_chunked && garrow_chunked_array_get_n_chunks(metadata_chunked) > 0) {
            GArrowArray *metadata_array = garrow_chunked_array_get_chunk(metadata_chunked, 0);
            if (metadata_array) {
                GArrowStringArray *metadata_string_array = GARROW_STRING_ARRAY(metadata_array);
                if (metadata_string_array) {
                    const gchar *metadata_json = garrow_string_array_get_string(metadata_string_array, 0);
                    if (metadata_json) result = strdup(metadata_json);
                }
                g_object_unref(metadata_array);
            }
            g_object_unref(metadata_chunked);
        }
    }
    g_object_unref(table);
    return result;
} 