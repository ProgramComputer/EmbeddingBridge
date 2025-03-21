/*
 * EmbeddingBridge - JSON Transformer Implementation
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/types.h>
#include <jansson.h>
#include "transformer.h"
#include "debug.h"
#include "json_transformer.h"

/* Initial implementation uses minimal JSON format conversion */
/* In a future version, we will integrate a proper JSON library */

typedef struct {
    int indent_level;
    bool pretty_print;
} json_transformer_config_t;

/* Forward declarations */
static eb_status_t json_transform(
    struct eb_transformer *transformer,
    const void *src,
    size_t src_size,
    void **dst_out,
    size_t *dst_size_out);

static eb_status_t json_inverse_transform(
    struct eb_transformer *transformer,
    const void *src,
    size_t src_size,
    void **dst_out,
    size_t *dst_size_out);

static void json_transformer_free(
    struct eb_transformer *transformer);

static struct eb_transformer *json_transformer_clone(
    const struct eb_transformer *transformer);

/**
 * Create a new JSON transformer
 *
 * @param pretty_print Whether to pretty-print the JSON output
 * @param indent_level Indentation level for pretty printing
 * @return New transformer instance
 */
struct eb_transformer *eb_json_transformer_create(
    bool pretty_print,
    int indent_level
) {
    json_transformer_config_t *config = malloc(sizeof(json_transformer_config_t));
    if (!config) {
        return NULL;
    }
    
    config->pretty_print = pretty_print;
    config->indent_level = indent_level;
    
    /* Create and initialize a transformer */
    eb_transformer_t *transformer = eb_transformer_create(
        "json",                  /* name */
        "json",                  /* format_name */
        json_transform,          /* transform */
        json_inverse_transform,  /* inverse_transform */
        json_transformer_free,   /* free */
        json_transformer_clone,  /* clone */
        config);                 /* user_data */
    
    return transformer;
}

/**
 * Get configuration from transformer
 *
 * @param transformer Transformer instance
 * @return Configuration or NULL if invalid
 */
static json_transformer_config_t *get_config(struct eb_transformer *transformer) {
    if (!transformer || !transformer->user_data) {
        return NULL;
    }
    
    return (json_transformer_config_t *)transformer->user_data;
}

/**
 * Escape a string for JSON output
 *
 * @param src Source string
 * @param src_len Length of source string
 * @param dst Destination buffer
 * @param dst_size Size of destination buffer
 * @return Number of bytes written or -1 on error
 */
static ssize_t escape_json_string(
    const char *src,
    size_t src_len,
    char *dst,
    size_t dst_size) {
    
    if (!src || !dst || dst_size == 0) {
        return -1;
    }
    
    size_t i, j;
    for (i = 0, j = 0; i < src_len && j < dst_size - 1; i++) {
        switch (src[i]) {
            case '\"':
                if (j + 2 >= dst_size) return -1;
                dst[j++] = '\\';
                dst[j++] = '\"';
                break;
            case '\\':
                if (j + 2 >= dst_size) return -1;
                dst[j++] = '\\';
                dst[j++] = '\\';
                break;
            case '\b':
                if (j + 2 >= dst_size) return -1;
                dst[j++] = '\\';
                dst[j++] = 'b';
                break;
            case '\f':
                if (j + 2 >= dst_size) return -1;
                dst[j++] = '\\';
                dst[j++] = 'f';
                break;
            case '\n':
                if (j + 2 >= dst_size) return -1;
                dst[j++] = '\\';
                dst[j++] = 'n';
                break;
            case '\r':
                if (j + 2 >= dst_size) return -1;
                dst[j++] = '\\';
                dst[j++] = 'r';
                break;
            case '\t':
                if (j + 2 >= dst_size) return -1;
                dst[j++] = '\\';
                dst[j++] = 't';
                break;
            default:
                if ((unsigned char)src[i] < 32) {
                    /* Control character */
                    if (j + 6 >= dst_size) return -1;
                    snprintf(&dst[j], dst_size - j, "\\u%04x", (unsigned char)src[i]);
                    j += 6;
                } else {
                    dst[j++] = src[i];
                }
                break;
        }
    }
    
    dst[j] = '\0';
    return j;
}

/**
 * Unescape a JSON string
 *
 * @param src Source string
 * @param src_len Length of source string
 * @param dst Destination buffer
 * @param dst_size Size of destination buffer
 * @return Number of bytes written or -1 on error
 */
static ssize_t unescape_json_string(
    const char *src,
    size_t src_len,
    char *dst,
    size_t dst_size) {
    
    if (!src || !dst || dst_size == 0) {
        return -1;
    }
    
    size_t i, j;
    for (i = 0, j = 0; i < src_len && j < dst_size - 1; i++) {
        if (src[i] == '\\' && i + 1 < src_len) {
            switch (src[++i]) {
                case '\"':
                    dst[j++] = '\"';
                    break;
                case '\\':
                    dst[j++] = '\\';
                    break;
                case '/':
                    dst[j++] = '/';
                    break;
                case 'b':
                    dst[j++] = '\b';
                    break;
                case 'f':
                    dst[j++] = '\f';
                    break;
                case 'n':
                    dst[j++] = '\n';
                    break;
                case 'r':
                    dst[j++] = '\r';
                    break;
                case 't':
                    dst[j++] = '\t';
                    break;
                case 'u':
                    /* Unicode escape sequence */
                    if (i + 4 < src_len) {
                        char hex[5] = {src[i+1], src[i+2], src[i+3], src[i+4], 0};
                        unsigned int codepoint;
                        if (sscanf(hex, "%x", &codepoint) == 1) {
                            /* Basic ASCII handling for now */
                            if (codepoint < 128) {
                                dst[j++] = (char)codepoint;
                            } else {
                                /* Just output a placeholder for now */
                                dst[j++] = '?';
                            }
                            i += 4;
                        } else {
                            dst[j++] = '?';
                        }
                    } else {
                        dst[j++] = '?';
                    }
                    break;
                default:
                    dst[j++] = src[i];
                    break;
            }
        } else {
            dst[j++] = src[i];
        }
    }
    
    dst[j] = '\0';
    return j;
}

/**
 * Check if a buffer contains valid JSON
 *
 * This is a very basic check that looks for matching braces and basic syntax.
 * A proper implementation would use a full JSON parser.
 *
 * @param data Data to check
 * @param size Size of data
 * @return true if looks like JSON, false otherwise
 */
static bool is_json(const char *data, size_t size) {
    if (!data || size == 0) {
        return false;
    }
    
    /* Trim leading whitespace */
    size_t start = 0;
    while (start < size && 
          (data[start] == ' ' || data[start] == '\t' || 
           data[start] == '\n' || data[start] == '\r')) {
        start++;
    }
    
    if (start >= size) {
        return false;
    }
    
    /* Check for JSON object or array start */
    return (data[start] == '{' || data[start] == '[');
}

/**
 * Transform binary data to JSON
 *
 * This basic implementation wraps binary data in a JSON object with base64 encoding.
 * A more advanced implementation would handle structured data appropriately.
 */
static eb_status_t json_transform(
    struct eb_transformer *transformer,
    const void *src,
    size_t src_size,
    void **dst_out,
    size_t *dst_size_out) {
    
    if (!transformer || !src || !dst_out || !dst_size_out) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    json_transformer_config_t *config = get_config(transformer);
    if (!config) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    /* Check if the input is already JSON */
    if (src_size > 0 && is_json((const char *)src, src_size)) {
        /* Data is already JSON, just copy it */
        char *dst = malloc(src_size + 1);
        if (!dst) {
            return EB_ERROR_MEMORY;
        }
        
        memcpy(dst, src, src_size);
        dst[src_size] = '\0';
        
        *dst_out = dst;
        *dst_size_out = src_size;
        return EB_SUCCESS;
    }
    
    /* 
     * For non-JSON input, we'll create a simple JSON object with a data field
     * In a real implementation, we would perform proper base64 encoding of binary data
     * For this example, we'll just create a simple escaped string
     */
    
    /* Allocate a buffer for the escaped string (worst case: each char becomes 6 chars) */
    size_t escaped_size = src_size * 6 + 1;
    char *escaped = malloc(escaped_size);
    if (!escaped) {
        return EB_ERROR_MEMORY;
    }
    
    /* Escape the string */
    ssize_t escaped_len = escape_json_string(
        (const char *)src, 
        src_size, 
        escaped, 
        escaped_size
    );
    
    if (escaped_len < 0) {
        free(escaped);
        return EB_ERROR_BUFFER_TOO_SMALL;
    }
    
    /* Create the JSON object */
    const char *json_template = "{\n  \"data\": \"%s\"\n}";
    if (!config->pretty_print) {
        json_template = "{\"data\":\"%s\"}";
    }
    
    /* Calculate the size of the JSON output */
    size_t json_size = strlen(json_template) + escaped_len;
    char *json = malloc(json_size);
    if (!json) {
        free(escaped);
        return EB_ERROR_MEMORY;
    }
    
    /* Format the JSON */
    snprintf(json, json_size, json_template, escaped);
    
    /* Clean up */
    free(escaped);
    
    /* Return the result */
    *dst_out = json;
    *dst_size_out = strlen(json);
    
    return EB_SUCCESS;
}

/**
 * Extract the data field from a JSON object
 *
 * @param json JSON string
 * @param json_len Length of JSON string
 * @param data_out Pointer to store extracted data
 * @param data_len_out Pointer to store length of extracted data
 * @return true if successful, false otherwise
 */
static bool extract_data_field(
    const char *json,
    size_t json_len,
    char **data_out,
    size_t *data_len_out) {
    
    /* Look for "data":" pattern */
    const char *data_field = strstr(json, "\"data\":\"");
    if (!data_field) {
        return false;
    }
    
    /* Skip to the beginning of the data value */
    const char *value_start = data_field + 8; /* strlen("\"data\":\"") */
    if (value_start >= json + json_len) {
        return false;
    }
    
    /* Find the closing quote */
    const char *value_end = NULL;
    const char *search_pos = value_start;
    
    while ((search_pos = strchr(search_pos, '\"')) != NULL) {
        /* Skip escaped quotes */
        if (search_pos > json && search_pos[-1] == '\\') {
            search_pos++;
            continue;
        }
        
        value_end = search_pos;
        break;
    }
    
    if (!value_end) {
        return false;
    }
    
    /* Calculate the length of the value */
    size_t value_len = value_end - value_start;
    
    /* Allocate a buffer for the unescaped value */
    char *value = malloc(value_len + 1);
    if (!value) {
        return false;
    }
    
    /* Copy and unescape the value */
    ssize_t unescaped_len = unescape_json_string(
        value_start,
        value_len,
        value,
        value_len + 1
    );
    
    if (unescaped_len < 0) {
        free(value);
        return false;
    }
    
    /* Return the result */
    *data_out = value;
    *data_len_out = unescaped_len;
    
    return true;
}

/**
 * Transform JSON data to binary
 *
 * This basic implementation extracts data from a JSON object with base64 decoding.
 * A more advanced implementation would handle structured data appropriately.
 */
static eb_status_t json_inverse_transform(
    struct eb_transformer *transformer,
    const void *src,
    size_t src_size,
    void **dst_out,
    size_t *dst_size_out) {
    
    if (!transformer || !src || !dst_out || !dst_size_out) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    /* Check if the input is JSON */
    if (!is_json((const char *)src, src_size)) {
        /* Not JSON, just copy it */
        void *dst = malloc(src_size);
        if (!dst) {
            return EB_ERROR_MEMORY;
        }
        
        memcpy(dst, src, src_size);
        *dst_out = dst;
        *dst_size_out = src_size;
        return EB_SUCCESS;
    }
    
    /* 
     * Extract data from the JSON
     * In a full implementation, we would handle more complex structures
     * Here, we just look for a data field at the top level
     */
    char *data = NULL;
    size_t data_len = 0;
    
    if (extract_data_field((const char *)src, src_size, &data, &data_len)) {
        /* Successfully extracted data field */
        *dst_out = data;
        *dst_size_out = data_len;
        return EB_SUCCESS;
    }
    
    /* If we couldn't extract a data field, just return the JSON as-is */
    void *dst = malloc(src_size);
    if (!dst) {
        return EB_ERROR_MEMORY;
    }
    
    memcpy(dst, src, src_size);
    *dst_out = dst;
    *dst_size_out = src_size;
    
    return EB_SUCCESS;
}

/**
 * Free resources associated with a JSON transformer
 */
static void json_transformer_free(struct eb_transformer *transformer) {
    if (!transformer) {
        return;
    }
    
    if (transformer->user_data) {
        free(transformer->user_data);
        transformer->user_data = NULL;
    }
    
    /* Let the transformer_free function handle the transformer's memory */
}

/**
 * Clone a JSON transformer
 */
static struct eb_transformer *json_transformer_clone(
    const struct eb_transformer *transformer
) {
    json_transformer_config_t *config = (json_transformer_config_t *)transformer->user_data;
    if (!config) {
        return NULL;
    }
    
    return eb_json_transformer_create(config->pretty_print, config->indent_level);
}

/**
 * Register the JSON transformer
 */
eb_status_t eb_register_json_transformer(void) {
    /* Create the default JSON transformer with pretty printing */
    struct eb_transformer *transformer = eb_json_transformer_create(true, 2);
    if (!transformer) {
        return EB_ERROR_MEMORY;
    }
    
    /* Register the transformer */
    eb_status_t status = eb_register_transformer(transformer);
    if (status != EB_SUCCESS) {
        eb_transformer_free(transformer);
        return status;
    }
    
    return EB_SUCCESS;
}

/**
 * Extract a field from JSON data
 */
eb_status_t eb_json_extract_field(const char *json_data, size_t json_len,
                                const char *field_name, char **value_out, size_t *value_len_out) {
    if (!json_data || !field_name || !value_out || !value_len_out) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    /* Parse JSON */
    json_error_t error;
    json_t *root = json_loadb(json_data, json_len, 0, &error);
    if (!root) {
        DEBUG_ERROR("JSON parse error: %s", error.text);
        return EB_ERROR_PARSING;
    }
    
    /* Extract field value */
    json_t *value = json_object_get(root, field_name);
    if (!value) {
        json_decref(root);
        return EB_ERROR_NOT_FOUND;
    }
    
    if (!json_is_string(value)) {
        json_decref(root);
        return EB_ERROR_TYPE_MISMATCH;
    }
    
    const char *str_value = json_string_value(value);
    if (!str_value) {
        json_decref(root);
        return EB_ERROR_INVALID_DATA;
    }
    
    /* Allocate and copy the value */
    size_t value_len = strlen(str_value);
    char *value_copy = malloc(value_len + 1);
    if (!value_copy) {
        json_decref(root);
        return EB_ERROR_MEMORY;
    }
    
    memcpy(value_copy, str_value, value_len);
    value_copy[value_len] = '\0';
    
    *value_out = value_copy;
    *value_len_out = value_len;
    
    json_decref(root);
    return EB_SUCCESS;
}

/**
 * Parse a JSON object
 */
eb_status_t eb_json_parse_object(const char *json_data, size_t json_len, void **parsed_data) {
    if (!json_data || !parsed_data) {
        return EB_ERROR_INVALID_PARAMETER;
    }
    
    /* Parse JSON */
    json_error_t error;
    json_t *root = json_loadb(json_data, json_len, 0, &error);
    if (!root) {
        DEBUG_ERROR("JSON parse error: %s", error.text);
        return EB_ERROR_PARSING;
    }
    
    *parsed_data = root;
    return EB_SUCCESS;
}

/**
 * Get a string value from parsed JSON
 */
const char *eb_json_get_string(void *parsed_data, const char *key) {
    if (!parsed_data || !key) {
        return NULL;
    }
    
    json_t *root = (json_t *)parsed_data;
    
    /* Handle nested paths like "storage.type" or "files.0.path" */
    char key_copy[256];
    strncpy(key_copy, key, sizeof(key_copy) - 1);
    key_copy[sizeof(key_copy) - 1] = '\0';
    
    char *saveptr = NULL;
    char *token = strtok_r(key_copy, ".", &saveptr);
    json_t *current = root;
    
    while (token) {
        /* Check if token is an array index */
        char *endptr;
        long index = strtol(token, &endptr, 10);
        
        if (*token != '\0' && *endptr == '\0') {
            /* Token is a valid integer, treat as array index */
            if (!json_is_array(current)) {
                DEBUG_ERROR("JSON path error: Expected array but found non-array at '%s'", token);
                return NULL;
            }
            
            current = json_array_get(current, (size_t)index);
            if (!current) {
                DEBUG_ERROR("JSON path error: Array index %ld out of bounds", index);
                return NULL;
            }
        }
        else {
            /* Token is a key for an object */
            if (!json_is_object(current)) {
                DEBUG_ERROR("JSON path error: Expected object but found non-object at '%s'", token);
                return NULL;
            }
            
            current = json_object_get(current, token);
            if (!current) {
                DEBUG_ERROR("JSON path error: Key '%s' not found", token);
                return NULL;
            }
        }
        
        token = strtok_r(NULL, ".", &saveptr);
    }
    
    if (!json_is_string(current)) {
        DEBUG_ERROR("JSON path error: Value is not a string");
        return NULL;
    }
    
    return json_string_value(current);
}

/**
 * Free parsed JSON data
 */
void eb_json_free_parsed(void *parsed_data) {
    if (parsed_data) {
        json_decref((json_t *)parsed_data);
    }
} 