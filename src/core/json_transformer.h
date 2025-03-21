/*
 * EmbeddingBridge - JSON Transformer Interface
 * Copyright (C) 2024 ProgramComputer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef JSON_TRANSFORMER_H
#define JSON_TRANSFORMER_H

#include "transformer.h"
#include <stdbool.h>
#include "status.h"

/**
 * Create a new JSON transformer
 *
 * @param pretty_print Whether to pretty-print the JSON output
 * @param indent_level Indentation level for pretty printing
 * @return New transformer instance
 */
struct eb_transformer *eb_json_transformer_create(bool pretty_print, int indent_level);

/**
 * Register the JSON transformer
 *
 * @return Status code
 */
eb_status_t eb_register_json_transformer(void);

/**
 * Extract a field from JSON data
 *
 * @param json_data JSON data to extract from
 * @param json_len Length of JSON data
 * @param field_name Field name to extract
 * @param value_out Pointer to store extracted value
 * @param value_len_out Pointer to store length of extracted value
 * @return Status code
 */
eb_status_t eb_json_extract_field(const char *json_data, size_t json_len, 
                                const char *field_name, char **value_out, size_t *value_len_out);

/**
 * Parse a JSON object
 *
 * @param json_data JSON data to parse
 * @param json_len Length of JSON data
 * @param parsed_data Pointer to store parsed data
 * @return Status code
 */
eb_status_t eb_json_parse_object(const char *json_data, size_t json_len,
                                void **parsed_data);

/**
 * Get a string value from parsed JSON
 *
 * @param parsed_data Parsed JSON data
 * @param key Key to get value for
 * @return String value associated with key
 */
const char *eb_json_get_string(void *parsed_data, const char *key);

/**
 * Free parsed JSON data
 *
 * @param parsed_data Parsed JSON data to free
 */
void eb_json_free_parsed(void *parsed_data);

#endif /* JSON_TRANSFORMER_H */ 