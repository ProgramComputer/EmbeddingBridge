/*
 * EmbeddingBridge - Parquet Transformer Implementation
 * Copyright (C) 2024 EmbeddingBridge Contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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

#include "transformer.h"
#include "status.h"
#include "debug.h"
#include "compress.h"

/* Arrow GLib includes */
#include <arrow-glib/arrow-glib.h>
#include <parquet-glib/parquet-glib.h>

/* Forward declarations */
static eb_status_t parquet_transform(struct eb_transformer* transformer, 
                                    const void* source, size_t source_size, 
                                    void** dest, size_t* dest_size);
static eb_status_t parquet_inverse_transform(struct eb_transformer* transformer, 
                                           const void* source, size_t source_size, 
                                           void** dest, size_t* dest_size);
static void parquet_transformer_free(struct eb_transformer* transformer);
static struct eb_transformer* parquet_transformer_clone(const struct eb_transformer* transformer);

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
    for (int i = 0; i < (source_size >= 16 ? 16 : source_size); i++) {
        DEBUG_WARN("  byte[%d] = 0x%02x (dec: %d, char: %c)", 
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
    
    /* Determine if this is a NumPy array (.npy) or binary data */
    gboolean is_npy = FALSE;
    if (source_size > 10) {
        /* Check for NumPy magic string '\x93NUMPY' */
        if (memcmp(source, "\x93NUMPY", 6) == 0) {
            is_npy = TRUE;
            DEBUG_INFO("Detected NumPy array format (.npy)");
        }
    }
        
    /* Create Arrow buffer from source data */
    GArrowBuffer *buffer = garrow_buffer_new((const guint8 *)source, source_size);
    if (!buffer) {
        DEBUG_ERROR("Failed to create Arrow buffer");
        return EB_ERROR_MEMORY;
    }
    
    /* Create input stream from buffer */
    GArrowBufferInputStream *input_stream = garrow_buffer_input_stream_new(buffer);
    g_object_unref(buffer);
    if (!input_stream) {
        DEBUG_ERROR("Failed to create buffer input stream");
        return EB_ERROR_IO;
    }
    
    /* Create file output stream for the result - we'll write to a temporary file */
    const gchar *temp_filename = "temp_parquet_data.parquet";
    GArrowFileOutputStream *output_stream = garrow_file_output_stream_new(temp_filename, FALSE, &error);
    if (!output_stream) {
        DEBUG_ERROR("Failed to create file output stream: %s", error ? error->message : "Unknown error");
        g_object_unref(input_stream);
        if (error) g_error_free(error);
        return EB_ERROR_IO;
    }
    
    /* Create arrays and schema */
    GArrowArray *array = NULL;
    GArrowSchema *schema = NULL;
    GList *fields = NULL;
        
    if (is_npy) {
        /* Handle NumPy array */
        /* Extract header size from NumPy format (stored at offset 8 as uint16) */
        uint16_t header_size = *((const uint16_t*)((const uint8_t*)source + 8));
                
        /* Calculate data offset */
        size_t data_offset = 10 + header_size;
            
        /* Assume float32 data - for real implementation, parse the NumPy header */
        const float *data_ptr = (const float*)((const uint8_t*)source + data_offset);
        gint64 num_values = (source_size - data_offset) / sizeof(float);
        
        /* Create float array */
        GArrowFloatArrayBuilder *builder = garrow_float_array_builder_new();
        
        garrow_float_array_builder_append_values(builder, data_ptr, num_values, NULL, 0, &error);
        if (error) {
            DEBUG_ERROR("Failed to append values: %s", error->message);
            g_object_unref(builder);
            g_object_unref(input_stream);
            g_object_unref(output_stream);
            g_error_free(error);
            return EB_ERROR_IO;
        }
        
        array = GARROW_ARRAY(garrow_array_builder_finish(GARROW_ARRAY_BUILDER(builder), &error));
        g_object_unref(builder);
        if (error) {
            DEBUG_ERROR("Failed to finish array: %s", error->message);
            g_object_unref(input_stream);
            g_object_unref(output_stream);
            g_error_free(error);
            return EB_ERROR_IO;
        }
        
        /* Check if the array is NULL */
        if (!array) {
            DEBUG_ERROR("Array is NULL after creation but no error reported");
            g_object_unref(input_stream);
            g_object_unref(output_stream);
            return EB_ERROR_IO;
        }
        
        /* Create schema for float array */
        GArrowField *field = garrow_field_new("values", GARROW_DATA_TYPE(garrow_float_data_type_new()));
        fields = g_list_append(NULL, field);
        schema = garrow_schema_new(fields);
        g_list_free(fields);
        g_object_unref(field);
        
        DEBUG_INFO("Created Arrow array from NumPy data with %ld elements", (long)num_values);
    } else {
        /* Handle raw binary data */
        GArrowBinaryArrayBuilder *builder = garrow_binary_array_builder_new();
        garrow_binary_array_builder_append_value(builder, 
                                                 (const guint8 *)source, 
                                                 source_size, 
                                                 &error);
        if (error) {
            DEBUG_ERROR("Failed to append binary value: %s", error->message);
            g_object_unref(builder);
            g_object_unref(input_stream);
            g_object_unref(output_stream);
            g_error_free(error);
            return EB_ERROR_IO;
        }
        
        array = GARROW_ARRAY(garrow_array_builder_finish(GARROW_ARRAY_BUILDER(builder), &error));
        g_object_unref(builder);
        if (error) {
            DEBUG_ERROR("Failed to finish array: %s", error->message);
            g_object_unref(input_stream);
            g_object_unref(output_stream);
            g_error_free(error);
            return EB_ERROR_IO;
        }
        
        /* Check if the array is NULL */
        if (!array) {
            DEBUG_ERROR("Array is NULL after creation but no error reported");
            g_object_unref(input_stream);
            g_object_unref(output_stream);
            return EB_ERROR_IO;
        }
        
        /* Create schema for binary data */
        DEBUG_INFO("About to create schema for binary data. source_size=%zu", source_size);
        GArrowField *field = garrow_field_new("data", GARROW_DATA_TYPE(garrow_binary_data_type_new()));
        DEBUG_INFO("Created field. field=%p", field);
        fields = g_list_append(NULL, field);
        DEBUG_INFO("Created fields list. fields=%p", fields);
        schema = garrow_schema_new(fields);
        DEBUG_INFO("Created schema. schema=%p", schema);
        g_list_free(fields);
        DEBUG_INFO("Freed fields list");
        g_object_unref(field);
        DEBUG_INFO("Unreferenced field");
        
        DEBUG_INFO("Created Arrow array from binary data. array=%p", array);
    }
    
    /* Create chunked array from array */
    DEBUG_INFO("About to create chunked array from array. array=%p", array);
    GList *arrays = g_list_append(NULL, array);
    DEBUG_INFO("Created arrays list. arrays=%p, array=%p", arrays, array);
    
    /* Check if list creation failed */
    if (!arrays) {
        DEBUG_ERROR("Failed to create arrays list");
        g_object_unref(array);
        g_object_unref(schema);
        g_object_unref(input_stream);
        g_object_unref(output_stream);
        return EB_ERROR_MEMORY;
    }
    
    GArrowChunkedArray *chunked_array = NULL;
    DEBUG_INFO("About to call garrow_chunked_array_new. arrays=%p", arrays);
    
    if (!array) {
        DEBUG_ERROR("Array is NULL before creating chunked array");
        g_list_free(arrays);
        g_object_unref(schema);
        g_object_unref(input_stream);
        g_object_unref(output_stream);
        return EB_ERROR_MEMORY;
    }
    
    chunked_array = garrow_chunked_array_new(arrays, &error);
    DEBUG_INFO("Called garrow_chunked_array_new. Result=%p, error=%p", chunked_array, error);
    g_list_free(arrays);
    DEBUG_INFO("Freed arrays list");
    g_object_unref(array);
    DEBUG_INFO("Unreferenced array");
    
    if (error) {
        DEBUG_ERROR("Failed to create chunked array: %s", error->message);
        g_object_unref(schema);
        g_object_unref(input_stream);
        g_object_unref(output_stream);
        g_error_free(error);
        return EB_ERROR_MEMORY;
    }
    
    /* Check if chunked_array is NULL despite no error */
    if (!chunked_array) {
        DEBUG_ERROR("Chunked array is NULL after creation but no error reported");
        g_object_unref(schema);
        g_object_unref(input_stream);
        g_object_unref(output_stream);
        return EB_ERROR_MEMORY;
    }
    
    /* Create table from chunked array */
    DEBUG_INFO("About to create table from chunked array. chunked_array=%p", chunked_array);
    GList *chunked_arrays = g_list_append(NULL, chunked_array);
    DEBUG_INFO("Created chunked_arrays list. chunked_arrays=%p", chunked_arrays);
    GArrowChunkedArray *chunked_array_ptr = chunked_array;
    
    if (!chunked_array) {
        DEBUG_ERROR("Chunked array is NULL before creating table");
        g_list_free(chunked_arrays);
        g_object_unref(schema);
        g_object_unref(input_stream);
        g_object_unref(output_stream);
        return EB_ERROR_IO;
    }
    
    DEBUG_INFO("About to create table. schema=%p, chunked_array_ptr=%p", schema, chunked_array_ptr);
    GArrowTable *table = garrow_table_new_chunked_arrays(schema, 
                                                      &chunked_array_ptr, 
                                                      1, 
                                                      &error);
    DEBUG_INFO("Created table. table=%p, error=%p", table, error);
    g_list_free(chunked_arrays);
    DEBUG_INFO("Freed chunked_arrays list");
    g_object_unref(chunked_array);
    DEBUG_INFO("Unreferenced chunked_array");
    
    if (error) {
        DEBUG_ERROR("Failed to create table: %s", error->message);
        g_object_unref(input_stream);
        g_object_unref(output_stream);
        g_object_unref(schema);
        g_error_free(error);
        return EB_ERROR_MEMORY;
    }
    
    /* Set up writer properties */
    GParquetWriterProperties *writer_props = gparquet_writer_properties_new();
    gparquet_writer_properties_set_compression(writer_props, 
                                              GARROW_COMPRESSION_TYPE_ZSTD, 
                                              "*");
    /* Note: compression_level is not directly exposed in the GLib API */
    
    /* Create and write to Parquet file */
    GParquetArrowFileWriter *writer = 
        gparquet_arrow_file_writer_new_arrow(schema,
                                             GARROW_OUTPUT_STREAM(output_stream),
                                             writer_props,
                                             &error);
    g_object_unref(writer_props);
    g_object_unref(output_stream);
    g_object_unref(schema);
    DEBUG_INFO("Unreferenced schema");
    
    if (error) {
        DEBUG_ERROR("Failed to create Parquet writer: %s", error->message);
        g_object_unref(table);
        g_object_unref(input_stream);
        g_error_free(error);
        return EB_ERROR_IO;
    }
    
    gboolean success = gparquet_arrow_file_writer_write_table(writer, 
                                                             table, 
                                                             1024, /* chunk size */
                                                             &error);
    g_object_unref(table);
    
    if (!success) {
        DEBUG_ERROR("Failed to write table: %s", error->message);
        g_object_unref(writer);
        g_object_unref(input_stream);
        g_error_free(error);
        return EB_ERROR_IO;
    }
    
    success = gparquet_arrow_file_writer_close(writer, &error);
    g_object_unref(writer);
    g_object_unref(input_stream);
    
    if (!success) {
        DEBUG_ERROR("Failed to close writer: %s", error->message);
        g_error_free(error);
        return EB_ERROR_IO;
    }
    
    /* Read the temporary file contents */
    FILE *temp_file = fopen(temp_filename, "rb");
    if (!temp_file) {
        DEBUG_ERROR("Failed to open temporary file for reading");
        return EB_ERROR_IO;
    }
    
    /* Get file size */
    fseek(temp_file, 0, SEEK_END);
    long file_size = ftell(temp_file);
    fseek(temp_file, 0, SEEK_SET);
    
    /* Allocate memory for file contents */
    *dest_size = file_size;
    *dest = malloc(*dest_size);
    if (!*dest) {
        DEBUG_ERROR("Failed to allocate memory for Parquet output");
        fclose(temp_file);
        return EB_ERROR_MEMORY;
    }
    
    /* Read file contents */
    size_t bytes_read = fread(*dest, 1, *dest_size, temp_file);
    fclose(temp_file);
    
    if (bytes_read != *dest_size) {
        DEBUG_ERROR("Failed to read temporary Parquet file");
        free(*dest);
        *dest = NULL;
        *dest_size = 0;
        return EB_ERROR_IO;
    }
    
    /* Delete temporary file */
    remove(temp_filename);
    
    DEBUG_INFO("Successfully transformed data to Parquet format (%zu bytes)", *dest_size);
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
        
    /* Get the first column (we only store one column) */
    GArrowChunkedArray *chunked_array = garrow_table_get_column_data(table, 0);
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
    
    /* Get the column/field type */
    GArrowSchema *schema = garrow_table_get_schema(table);
    if (!schema) {
        DEBUG_ERROR("Failed to get schema from table");
        if (array && G_IS_OBJECT(array)) g_object_unref(array);
        if (table && G_IS_OBJECT(table)) g_object_unref(table);
        return EB_ERROR_IO;
    }
    
    GArrowField *field = garrow_schema_get_field(schema, 0);
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
        
        /* Get the data */
        int64_t length = garrow_array_get_length(array);
        const gfloat *values = garrow_float_array_get_values(float_array, NULL);
        if (!values) {
            DEBUG_ERROR("Failed to get values from float array");
            if (data_type && G_IS_OBJECT(data_type)) g_object_unref(data_type);
            if (field && G_IS_OBJECT(field)) g_object_unref(field);
            if (schema && G_IS_OBJECT(schema)) g_object_unref(schema);
            if (array && G_IS_OBJECT(array)) g_object_unref(array);
            if (table && G_IS_OBJECT(table)) g_object_unref(table);
            return EB_ERROR_IO;
        }
        gsize data_size = length * sizeof(gfloat);
            
        /* Create NumPy header - this is a simplified version */
        const char* numpy_magic = "\x93NUMPY\x01\x00";
        guint16 header_len = 128;  /* Fixed size for simplicity */
            
        /* Format header with shape and dtype information */
        char header[128];
        snprintf(header, sizeof(header),
                 "{'descr': '<f4', 'fortran_order': False, 'shape': (%ld,), }", 
                 (long)length);
                    
        /* Pad header with spaces to reach header_len - 10 */
        size_t header_text_len = strlen(header);
        for (size_t i = header_text_len; i < (size_t)(header_len - 10); i++) {
            header[i] = ' ';
        }
        header[header_len - 10 - 1] = '\n';
            
        /* Calculate total size needed */
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
            
        /* Write magic string and version */
        memcpy(*dest, numpy_magic, 8);
            
        /* Write header length as uint16 */
        guint16* header_len_ptr = (guint16*)((guint8*)*dest + 8);
        *header_len_ptr = header_len;
            
        /* Write header content */
        memcpy((guint8*)*dest + 10, header, header_len);
            
        /* Write data */
        memcpy((guint8*)*dest + 10 + header_len, values, data_size);
            
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
    } else {
        DEBUG_ERROR("Unsupported data type in Parquet file: %d", type);
        if (data_type && G_IS_OBJECT(data_type)) g_object_unref(data_type);
        if (field && G_IS_OBJECT(field)) g_object_unref(field);
        if (schema && G_IS_OBJECT(schema)) g_object_unref(schema);
        if (array && G_IS_OBJECT(array)) g_object_unref(array);
        if (table && G_IS_OBJECT(table)) g_object_unref(table);
        return EB_ERROR_IO;
    }
    
    /* Free all objects in reverse order of creation, with proper NULL checks */
    if (data_type && G_IS_OBJECT(data_type)) g_object_unref(data_type);
    if (field && G_IS_OBJECT(field)) g_object_unref(field);
    if (schema && G_IS_OBJECT(schema)) g_object_unref(schema);
    if (array && G_IS_OBJECT(array)) g_object_unref(array);
    if (table && G_IS_OBJECT(table)) g_object_unref(table);
    
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