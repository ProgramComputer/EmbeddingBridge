#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include "cli.h"
#include "colors.h"
#include "../core/store.h"
#include "../core/path_utils.h"

/* 
 * Git-style config implementation
 * Format is INI-style with sections and keys:
 * [section]
 *     key = value
 */

static const char* CONFIG_USAGE = 
    "Usage: eb config <command> [<args>]\n"
    "\n"
    "Manage embedding configuration\n"
    "\n"
    "Commands:\n"
    "  get <key>              Get config value\n"
    "  set <key> <value>      Set config value\n"
    "  list                   List all config values\n"
    "  unset <key>           Remove config value\n"
    "\n"
    "Examples:\n"
    "  # Set default model\n"
    "  eb config set model.default openai-3\n"
    "\n"
    "  # Enable verbose Git hooks\n"
    "  eb config set git.hooks.pre-commit.verbose true\n"
    "\n"
    "  # List all settings\n"
    "  eb config list\n"
    "\n"
    "  # Get a specific setting\n"
    "  eb config get model.default\n";

/*
 * File paths
 */
static char* get_config_path(void)
{
	char* eb_root = find_repo_root(".");
	if (!eb_root)
		return NULL;
	
	char* config_path = malloc(strlen(eb_root) + 15);
	if (!config_path) {
		free(eb_root);
		return NULL;
	}
	
	sprintf(config_path, "%s/.eb/config", eb_root);
	free(eb_root);
	return config_path;
}

/*
 * Config file handling functions
 */

/* Read the entire config file into memory */
static char* read_config_file(void)
{
	char* config_path = get_config_path();
	if (!config_path) {
		cli_error("Could not find repository root");
		return NULL;
	}
	
	FILE* f = fopen(config_path, "r");
	if (!f) {
		if (errno == ENOENT) {
			/* Config file doesn't exist, create with defaults */
			f = fopen(config_path, "w");
			if (!f) {
				cli_error("Could not create config file: %s", strerror(errno));
				free(config_path);
				return NULL;
			}
			
			fprintf(f, "# EmbeddingBridge config file\n\n");
			fprintf(f, "[core]\n");
			fprintf(f, "	version = 0.1.0\n\n");
			fprintf(f, "[model]\n");
			fprintf(f, "	default = \n\n");
			fprintf(f, "[storage]\n");
			fprintf(f, "	compression = true\n");
			fprintf(f, "	deduplication = true\n\n");
			fprintf(f, "[git]\n");
			fprintf(f, "	auto_update = true\n\n");
			fprintf(f, "[git \"hooks.pre-commit\"]\n");
			fprintf(f, "	enabled = true\n");
			fprintf(f, "	verbose = false\n\n");
			fprintf(f, "[git \"hooks.post-commit\"]\n");
			fprintf(f, "	enabled = true\n");
			fprintf(f, "	verbose = false\n\n");
			fprintf(f, "[git \"hooks.pre-push\"]\n");
			fprintf(f, "	enabled = true\n");
			fprintf(f, "	verbose = false\n\n");
			fprintf(f, "[git \"hooks.post-merge\"]\n");
			fprintf(f, "	enabled = true\n");
			fprintf(f, "	verbose = false\n");
			
			fclose(f);
			
			/* Reopen for reading */
			f = fopen(config_path, "r");
			if (!f) {
				cli_error("Could not open config file: %s", strerror(errno));
				free(config_path);
				return NULL;
			}
		} else {
			cli_error("Could not open config file: %s", strerror(errno));
			free(config_path);
			return NULL;
		}
	}
	
	/* Get file size */
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	
	/* Read file content */
	char* content = malloc(size + 1);
	if (!content) {
		cli_error("Out of memory");
		fclose(f);
		free(config_path);
		return NULL;
	}
	
	size_t read_bytes = fread(content, 1, size, f);
	fclose(f);
	
	if (read_bytes != (size_t)size) {
		cli_error("Failed to read config file");
		free(content);
		free(config_path);
		return NULL;
	}
	
	content[size] = '\0';
	free(config_path);
	return content;
}

/* Get config value by section and key */
static char* get_config_value(const char* content, const char* section, const char* key)
{
	if (!content || !section || !key)
		return NULL;
	
	char section_header[256];
	snprintf(section_header, sizeof(section_header), "[%s]", section);
	
	/* Find the section */
	const char* section_start = strstr(content, section_header);
	if (!section_start)
		return NULL;
	
	/* Find the next section (or end of file) */
	const char* next_section = strstr(section_start + 1, "[");
	if (!next_section)
		next_section = content + strlen(content);
	
	/* Look for the key within this section */
	char key_prefix[256];
	snprintf(key_prefix, sizeof(key_prefix), "%s = ", key);
	
	const char* key_line = strstr(section_start, key_prefix);
	if (!key_line || key_line >= next_section)
		return NULL;
	
	/* Extract the value */
	const char* value_start = key_line + strlen(key_prefix);
	const char* value_end = strchr(value_start, '\n');
	if (!value_end)
		value_end = value_start + strlen(value_start);
	
	int len = value_end - value_start;
	char* value = malloc(len + 1);
	if (!value)
		return NULL;
	
	strncpy(value, value_start, len);
	value[len] = '\0';
	
	/* Trim trailing whitespace */
	char* end = value + len - 1;
	while (end > value && isspace((unsigned char)*end))
		end--;
	*(end + 1) = '\0';
	
	return value;
}

/* Get config value by dot notation key (section.key) */
static char* get_config_by_key(const char* content, const char* dot_key)
{
	if (!content || !dot_key)
		return NULL;
	
	/* Parse the key into section and name */
	char* key_copy = strdup(dot_key);
	if (!key_copy)
		return NULL;
	
	char* dot = strchr(key_copy, '.');
	if (!dot) {
		free(key_copy);
		return NULL;
	}
	
	*dot = '\0';
	const char* section = key_copy;
	const char* name = dot + 1;
	
	char* value = get_config_value(content, section, name);
	free(key_copy);
	
	return value;
}

/* Set config value by section and key */
static int set_config_value(const char* section, const char* key, const char* value)
{
	char* config_path = get_config_path();
	if (!config_path) {
		cli_error("Could not find repository root");
		return 1;
	}
	
	char* content = read_config_file();
	if (!content) {
		free(config_path);
		return 1;
	}
	
	/* Create the new content in memory */
	char* mem_content = NULL;
	size_t mem_size = 0;
	FILE* mem = open_memstream(&mem_content, &mem_size);
	if (!mem) {
		cli_error("Memory allocation error");
		free(content);
		free(config_path);
		return 1;
	}
	
	char section_header[256];
	snprintf(section_header, sizeof(section_header), "[%s]", section);
	
	/* Find the section */
	const char* section_start = strstr(content, section_header);
	
	if (section_start) {
		/* Section exists, find key within it */
		const char* next_section = strstr(section_start + 1, "[");
		if (!next_section)
			next_section = content + strlen(content);
		
		char key_prefix[256];
		snprintf(key_prefix, sizeof(key_prefix), "%s = ", key);
		
		const char* key_line = strstr(section_start, key_prefix);
		
		if (key_line && key_line < next_section) {
			/* Key exists in section, replace it */
			size_t prefix_len = key_line - content;
			fwrite(content, 1, prefix_len, mem);
			
			/* Write the key-value pair */
			fprintf(mem, "%s = %s\n", key, value);
			
			/* Skip the old line */
			const char* line_end = strchr(key_line, '\n');
			if (line_end)
				fwrite(line_end + 1, 1, (content + strlen(content)) - (line_end + 1), mem);
			else
				fprintf(mem, "\n");
		} else {
			/* Key doesn't exist in section, add it */
			size_t prefix_len = section_start - content + strlen(section_header);
			fwrite(content, 1, prefix_len, mem);
			fprintf(mem, "\n	%s = %s\n", key, value);
			fwrite(section_start + strlen(section_header), 1, 
				strlen(content) - (section_start + strlen(section_header) - content), mem);
		}
	} else {
		/* Section doesn't exist, add it to the end */
		fwrite(content, 1, strlen(content), mem);
		if (content[strlen(content) - 1] != '\n')
			fprintf(mem, "\n");
		fprintf(mem, "\n[%s]\n", section);
		fprintf(mem, "	%s = %s\n", key, value);
	}
	
	fclose(mem);
	
	/* Write the new content back to the file */
	FILE* f = fopen(config_path, "w");
	if (!f) {
		cli_error("Could not open config file for writing: %s", strerror(errno));
		free(mem_content);
		free(content);
		free(config_path);
		return 1;
	}
	
	fputs(mem_content, f);
	fclose(f);
	
	free(mem_content);
	free(content);
	free(config_path);
	
	return 0;
}

/* Set config value by dot notation key (section.key) */
static int set_config_by_key(const char* dot_key, const char* value)
{
	if (!dot_key || !value)
		return 1;
	
	/* Parse the key into section and name */
	char* key_copy = strdup(dot_key);
	if (!key_copy)
		return 1;
	
	char* dot = strchr(key_copy, '.');
	if (!dot) {
		free(key_copy);
		return 1;
	}
	
	*dot = '\0';
	const char* section = key_copy;
	const char* name = dot + 1;
	
	int result = set_config_value(section, name, value);
	free(key_copy);
	
	return result;
}

/* Command functions */
static int cmd_config_get(int argc, char** argv)
{
	if (argc < 2) {
		cli_error("Key required");
		return 1;
	}
	
	char* content = read_config_file();
	if (!content)
		return 1;
	
	char* value = get_config_by_key(content, argv[1]);
	if (value) {
		printf("%s\n", value);
		free(value);
		free(content);
		return 0;
	} else {
		cli_error("Config value '%s' not found", argv[1]);
		free(content);
		return 1;
	}
}

static int cmd_config_set(int argc, char** argv)
{
	if (argc < 3) {
		cli_error("Key and value required");
		return 1;
	}
	
	printf("Setting %s = %s\n", argv[1], argv[2]);
	return set_config_by_key(argv[1], argv[2]);
}

static int cmd_config_list(void)
{
	char* content = read_config_file();
	if (!content)
		return 1;
	
	/* Print the config file as is */
	printf("%s\n", content);
	free(content);
	return 0;
}

static int cmd_config_unset(int argc, char** argv)
{
	if (argc < 2) {
		cli_error("Key required");
		return 1;
	}
	
	/* Unset by setting to empty string for now */
	printf("Removing setting: %s\n", argv[1]);
	return set_config_by_key(argv[1], "");
}

int cmd_config(int argc, char** argv)
{
	if (argc < 2 || has_option(argc, argv, "-h") || has_option(argc, argv, "--help")) {
		printf("%s", CONFIG_USAGE);
		return (argc < 2) ? 1 : 0;
	}
	
	const char* subcmd = argv[1];
	
	if (strcmp(subcmd, "get") == 0) {
		return cmd_config_get(argc - 1, argv + 1);
	} else if (strcmp(subcmd, "set") == 0) {
		return cmd_config_set(argc - 1, argv + 1);
	} else if (strcmp(subcmd, "list") == 0) {
		return cmd_config_list();
	} else if (strcmp(subcmd, "unset") == 0) {
		return cmd_config_unset(argc - 1, argv + 1);
	} else {
		cli_error("Unknown config command: %s", subcmd);
		printf("\n%s", CONFIG_USAGE);
		return 1;
	}
} 