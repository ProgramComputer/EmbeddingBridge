#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <sys/stat.h>
#include "cli.h"
#include "colors.h"
#include "../core/error.h"
#include "rollback.h"
#include "log.h"

bool has_option(int argc, char** argv, const char* option) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], option) == 0) {
            return true;
        }
    }
    return false;
}

const char* get_option_value(int argc, char** argv, const char* short_opt, const char* long_opt) {
    for (int i = 1; i < argc - 1; i++) {
        if ((short_opt && strcmp(argv[i], short_opt) == 0) || 
            (long_opt && strcmp(argv[i], long_opt) == 0)) {
            return argv[i + 1];
        }
    }
    return NULL;
}

bool file_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

void handle_error(eb_status_t status, const char* context) {
    fprintf(stderr, "%serror:%s %s: %s\n", 
            COLOR_RED, COLOR_RESET, 
            context ? context : "Operation failed",
            eb_status_str(status));
}

void cli_error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "%serror:%s ", COLOR_RED, COLOR_RESET);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void cli_warning(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "%swarning:%s ", COLOR_YELLOW, COLOR_RESET);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void cli_info(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "%sinfo:%s ", COLOR_BLUE, COLOR_RESET);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

bool is_option_with_value(const char* arg) {
    return (strcmp(arg, "-m") == 0 || strcmp(arg, "--model") == 0 ||
            strcmp(arg, "-t") == 0 || strcmp(arg, "--threshold") == 0);
}

float get_float_option(int argc, char** argv, const char* short_opt, const char* long_opt, float default_value) {
    const char* value = get_option_value(argc, argv, short_opt, long_opt);
    if (!value) return default_value;
    
    char* end;
    float result = strtof(value, &end);
    if (*end != '\0') {
        cli_error("Invalid float value for %s: %s", short_opt, value);
        return default_value;
    }
    return result;
}

int get_int_option(int argc, char** argv, const char* short_opt, const char* long_opt, int default_value) {
    const char* value = get_option_value(argc, argv, short_opt, long_opt);
    if (!value) return default_value;
    
    char* end;
    long result = strtol(value, &end, 10);
    if (*end != '\0' || result < INT_MIN || result > INT_MAX) {
        cli_error("Invalid integer value for %s: %s", short_opt, value);
        return default_value;
    }
    return (int)result;
}

const char* get_model(int argc, char** argv) {
    // First try command line
    const char* model = get_option_value(argc, argv, "-m", "--model");
    if (model) {
        return model;
    }

    // Then try config
    char config_path[1024];
    char cwd[1024];
    
    if (!getcwd(cwd, sizeof(cwd))) {
        cli_error("Could not get current directory");
        return NULL;
    }
    
    snprintf(config_path, sizeof(config_path), "%s/.eb/config", cwd);
    
    FILE* f = fopen(config_path, "r");
    if (!f) {
        return NULL;
    }
    
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "\"default_model\"")) {
            char* start = strchr(line, ':');
            if (!start) continue;
            start++;
            while (*start == ' ' || *start == '"') start++;
            char* end = strrchr(start, '"');
            if (!end) continue;
            *end = '\0';
            if (strlen(start) > 0) {
                char* result = strdup(start);
                fclose(f);
                return result;
            }
        }
    }
    fclose(f);
    return NULL;
}

static const eb_command_t commands[] = {
    {"init", "Initialize embedding repository", cmd_init},
    {"store", "Store embedding for source file", cmd_store},
    {"diff", "Compare two embeddings", cmd_diff},
    {"model", "Manage embedding models", cmd_model},
    {"rollback", "Revert to a previous embedding version", cmd_rollback},
    {"status", "Show embedding status for a source file", cmd_status},
    {"log", "Display embedding log for files", cmd_log},
    {NULL, NULL, NULL}
}; 