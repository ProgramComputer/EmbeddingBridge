#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "cli.h"
#include "colors.h"
#include "../core/store.h"

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

// Helper function to read config file
static char* read_config_file(void) {
    char config_path[1024];
    char cwd[1024];
    
    if (!getcwd(cwd, sizeof(cwd))) {
        cli_error("Could not get current directory");
        return NULL;
    }
    
    snprintf(config_path, sizeof(config_path), "%s/.eb/config.json", cwd);
    
    FILE* f = fopen(config_path, "r");
    if (!f) {
        cli_error("Could not open config file: %s", strerror(errno));
        return NULL;
    }
    
    // Get file size
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    // Read file content
    char* content = malloc(size + 1);
    if (!content) {
        cli_error("Out of memory");
        fclose(f);
        return NULL;
    }
    
    size_t read = fread(content, 1, size, f);
    fclose(f);
    
    if (read != (size_t)size) {
        cli_error("Failed to read config file");
        free(content);
        return NULL;
    }
    
    content[size] = '\0';
    return content;
}

// Helper function to write config file
static int write_config_file(const char* content) {
    char config_path[1024];
    char cwd[1024];
    
    if (!getcwd(cwd, sizeof(cwd))) {
        cli_error("Could not get current directory");
        return 1;
    }
    
    snprintf(config_path, sizeof(config_path), "%s/.eb/config.json", cwd);
    
    FILE* f = fopen(config_path, "w");
    if (!f) {
        cli_error("Could not open config file for writing: %s", strerror(errno));
        return 1;
    }
    
    if (fputs(content, f) == EOF) {
        cli_error("Failed to write config file: %s", strerror(errno));
        fclose(f);
        return 1;
    }
    
    fclose(f);
    return 0;
}

static int cmd_config_get(int argc, char** argv) {
    if (argc < 2) {
        cli_error("Key required");
        return 1;
    }
    
    char* config = read_config_file();
    if (!config) {
        return 1;
    }
    
    // TODO: Parse JSON and get value
    // For now just print the config
    printf("%s\n", config);
    free(config);
    return 0;
}

static int cmd_config_set(int argc, char** argv) {
    if (argc < 3) {
        cli_error("Key and value required");
        return 1;
    }
    
    char* config = read_config_file();
    if (!config) {
        return 1;
    }
    
    // TODO: Parse JSON, update value, and write back
    // For now just acknowledge
    printf("Setting %s = %s\n", argv[1], argv[2]);
    free(config);
    return 0;
}

static int cmd_config_list(void) {
    char* config = read_config_file();
    if (!config) {
        return 1;
    }
    
    // TODO: Parse JSON and format nicely
    // For now just print the raw config
    printf("%s\n", config);
    free(config);
    return 0;
}

static int cmd_config_unset(int argc, char** argv) {
    if (argc < 2) {
        cli_error("Key required");
        return 1;
    }
    
    char* config = read_config_file();
    if (!config) {
        return 1;
    }
    
    // TODO: Parse JSON, remove key, and write back
    // For now just acknowledge
    printf("Removing setting: %s\n", argv[1]);
    free(config);
    return 0;
}

int cmd_config(int argc, char** argv) {
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