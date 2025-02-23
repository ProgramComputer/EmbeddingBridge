#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cli.h"
#include "../core/embedding.h"
#include "../core/error.h"
#include "../core/debug.h"

static const char* MODEL_USAGE = 
    "Usage: eb model <command> [options]\n"
    "\n"
    "Commands:\n"
    "  register <name>    Register a new model\n"
    "  unregister <name>  Unregister a model\n"
    "  list              List registered models\n"
    "\n"
    "Options for register:\n"
    "  --dimensions <n>   Number of dimensions (required)\n"
    "  --normalize       Normalize output vectors\n"
    "  --version <v>     Model version (default: 1.0.0)\n"
    "  --description <d> Model description (default: User registered model)\n"
    "\n"
    "Examples:\n"
    "  # Register a new model\n"
    "  eb model register my-model --dimensions 1536 --normalize\n"
    "\n"
    "  # List registered models\n"
    "  eb model list\n";

static int cmd_model_register(int argc, char** argv) {
    DEBUG_PRINT("Entering cmd_model_register\n");
    
    if (argc < 2) {
        fprintf(stderr, "error: model name required\n");
        printf("\n%s", MODEL_USAGE);
        return 1;
    }

    const char* name = argv[1];
    const char* dimensions_str = get_option_value(argc, argv, NULL, "--dimensions");
    bool normalize = has_option(argc, argv, "--normalize");
    const char* version = get_option_value(argc, argv, NULL, "--version");
    const char* description = get_option_value(argc, argv, NULL, "--description");

    DEBUG_PRINT("Parsed arguments:\n");
    DEBUG_PRINT("  - name: %s\n", name);
    DEBUG_PRINT("  - dimensions: %s\n", dimensions_str ? dimensions_str : "(null)");
    DEBUG_PRINT("  - normalize: %d\n", normalize);
    DEBUG_PRINT("  - version: %s\n", version ? version : "(default)");
    DEBUG_PRINT("  - description: %s\n", description ? description : "(default)");

    // Validate required arguments
    if (!dimensions_str) {
        fprintf(stderr, "error: --dimensions is required\n");
        return 1;
    }

    // Parse dimensions
    char* endptr;
    size_t dims = strtoull(dimensions_str, &endptr, 10);
    if (*endptr != '\0' || dims == 0) {
        DEBUG_PRINT("Failed to parse dimensions: %s\n", dimensions_str);
        cli_error("Invalid dimensions value");
        return 1;
    }
    DEBUG_PRINT("Parsed dimensions: %zu\n", dims);

    // Use default version if not specified
    if (!version) {
        version = "1.0.0";
        DEBUG_PRINT("Using default version: %s\n", version);
    }

    // Use default description if not specified
    if (!description) {
        description = "User registered model";
        DEBUG_PRINT("Using default description: %s\n", description);
    }

    DEBUG_PRINT("Calling eb_register_model with:\n");
    DEBUG_PRINT("  - name: %s\n", name);
    DEBUG_PRINT("  - dimensions: %zu\n", dims);
    DEBUG_PRINT("  - normalize: %d\n", normalize);
    DEBUG_PRINT("  - version: %s\n", version);
    DEBUG_PRINT("  - description: %s\n", description);

    // Register the model
    eb_status_t status = eb_register_model(name, dims, normalize, version, description);
    DEBUG_PRINT("eb_register_model returned status: %d (%s)\n", 
            status, eb_status_str(status));

    if (status != EB_SUCCESS) {
        cli_error("Failed to register model: %s", eb_status_str(status));
        return 1;
    }

    printf("Successfully registered model '%s'\n", name);
    return 0;
}

static int cmd_model_unregister(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "error: model name required\n");
        printf("\n%s", MODEL_USAGE);
        return 1;
    }

    const char* name = argv[1];
    if (!eb_is_model_registered(name)) {
        fprintf(stderr, "error: model '%s' not found\n", name);
        return 1;
    }

    eb_unregister_model(name);
    printf("Successfully unregistered model '%s'\n", name);
    return 0;
}

static int cmd_model_list(int argc, char** argv) {
    DEBUG_PRINT("Entering cmd_model_list\n");
    
    char** model_names;
    size_t count;
    eb_status_t status = eb_list_models(&model_names, &count);
    
    if (status != EB_SUCCESS) {
        cli_error("Failed to list models: %s", eb_status_str(status));
        return 1;
    }

    printf("Available models:\n");
    for (size_t i = 0; i < count; i++) {
        eb_model_info_t info;
        if (eb_get_model_info(model_names[i], &info) == EB_SUCCESS) {
            printf("  %s (v%s) - %zu dimensions%s\n    %s\n",
                   model_names[i],
                   info.version,
                   info.dimensions,
                   info.normalize_output ? ", normalized" : "",
                   info.description);
            free(info.version);
            free(info.description);
        } else {
            printf("  %s\n", model_names[i]);
        }
    }

    // Cleanup
    for (size_t i = 0; i < count; i++) {
        free(model_names[i]);
    }
    free(model_names);

    DEBUG_PRINT("Completed cmd_model_list\n");
    return 0;
}

int cmd_model(int argc, char** argv) {
    if (argc < 2 || has_option(argc, argv, "-h") || has_option(argc, argv, "--help")) {
        printf("%s", MODEL_USAGE);
        return (argc < 2) ? 1 : 0;
    }

    const char* subcmd = argv[1];
    if (strcmp(subcmd, "register") == 0) {
        return cmd_model_register(argc - 1, argv + 1);
    } else if (strcmp(subcmd, "unregister") == 0) {
        return cmd_model_unregister(argc - 1, argv + 1);
    } else if (strcmp(subcmd, "list") == 0) {
        return cmd_model_list(argc - 1, argv + 1);
    } else {
        fprintf(stderr, "error: unknown model command '%s'\n", subcmd);
        printf("\n%s", MODEL_USAGE);
        return 1;
    }
} 