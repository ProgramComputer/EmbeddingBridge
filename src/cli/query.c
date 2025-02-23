#include <stdio.h>
#include <string.h>
#include <stdlib.h>  // For atoi and atof
#include "cli.h"
#include "colors.h"  // For colored output
#include "../core/search.h"  // For search types and functions
#include "../core/types.h"   // For config types
#include "../core/eb_store.h"   // For store functions
#include "../core/store.h"   // For store destroy function

static const char* QUERY_USAGE = 
    "Usage: eb query [options] <search-text>\n"
    "   or: eb query [options] -f <file>\n"
    "\n"
    "Search across stored embeddings\n"
    "\n"
    "Options:\n"
    "  -m, --model <name>    Use specific embedding model\n"
    "  -k, --top <n>         Number of results (default: 5)\n"
    "  -t, --threshold <n>   Similarity threshold (default: 0.7)\n"
    "  -f, --file            Use file content as query\n"
    "  -v, --verbose         Show detailed output\n"
    "  -q, --quiet           Show only filenames\n"
    "  --no-color           Disable colored output\n"
    "\n"
    "Examples:\n"
    "  # Search by text\n"
    "  eb query \"error handling implementation\"\n"
    "\n"
    "  # Search using file content\n"
    "  eb query -f similar_doc.txt\n"
    "\n"
    "  # Detailed search with specific model\n"
    "  eb query -v --model openai-3 \"memory management\"\n";

// Progress indicator states
static const char spinner[] = "⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏";
static int spinner_state = 0;

static void show_progress(const char* msg) {
    printf("\r%s%c", msg, spinner[spinner_state]);
    spinner_state = (spinner_state + 1) % sizeof(spinner);
    fflush(stdout);
}

static void clear_progress(void) {
    printf("\r\033[K");  // Clear line
    fflush(stdout);
}

static void print_result(const eb_search_result_t* result, const eb_cli_options_t* opts) {
    if (opts->quiet) {
        printf("%s\n", result->filepath);
        return;
    }

    if (opts->use_color) {
        float sim = result->similarity;
        if (sim >= 0.8f) {
            printf(COLOR_BOLD_GREEN "%.0f%%" COLOR_RESET, sim * 100);
        } else if (sim >= 0.5f) {
            printf(COLOR_BOLD_YELLOW "%.0f%%" COLOR_RESET, sim * 100);
        } else {
            printf(COLOR_BOLD_RED "%.0f%%" COLOR_RESET, sim * 100);
        }
        printf(" %s\n", result->filepath);
    } else {
        printf("%.0f%% %s\n", result->similarity * 100, result->filepath);
    }

    if (opts->verbose) {
        if (result->context && strlen(result->context) > 0) {
            printf("  Context: %s\n", result->context);
        }
        if (result->last_modified) {
            printf("  Modified: %s\n", result->last_modified);
        }
    }
}

int cmd_query(int argc, char** argv) {
    if (argc < 2 || has_option(argc, argv, "-h") || has_option(argc, argv, "--help")) {
        printf("%s", QUERY_USAGE);
        return (argc < 2) ? 1 : 0;
    }

    eb_cli_options_t opts = {
        .model = NULL,  // Must be specified or configured
        .top_k = 5,
        .threshold = 0.7f,
        .verbose = has_option(argc, argv, "-v") || has_option(argc, argv, "--verbose"),
        .quiet = has_option(argc, argv, "-q") || has_option(argc, argv, "--quiet"),
        .use_color = !has_option(argc, argv, "--no-color"),
        .use_file = has_option(argc, argv, "-f") || has_option(argc, argv, "--file")
    };

    // Parse options
    const char* model = get_model(argc, argv);
    if (model) {
        opts.model = model;
    } else {
        fprintf(stderr, "error: no model specified\n");
        fprintf(stderr, "hint: specify a model with --model or configure a default with 'eb config set model.default <name>'\n");
        return 1;
    }

    const char* top_k_arg = get_option_value(argc, argv, "-k", "--top");
    if (top_k_arg) {
        opts.top_k = atoi(top_k_arg);
        if (opts.top_k < 1) {
            fprintf(stderr, "error: invalid value for -k/--top: %s\n", top_k_arg);
            return 1;
        }
    }

    const char* threshold_arg = get_option_value(argc, argv, "-t", "--threshold");
    if (threshold_arg) {
        opts.threshold = atof(threshold_arg);
        if (opts.threshold < 0.0f || opts.threshold > 1.0f) {
            fprintf(stderr, "error: threshold must be between 0.0 and 1.0\n");
            return 1;
        }
    }

    // Get query text or file
    const char* query_source = argv[argc - 1];
    if (!query_source) {
        fprintf(stderr, "error: no query specified\n");
        return 1;
    }

    // Create embedding from query
    eb_embedding_t* query_embedding = NULL;
    eb_status_t status;

    if (!opts.quiet) {
        show_progress("Generating query embedding...");
    }

    if (opts.use_file) {
        if (!file_exists(query_source)) {
            clear_progress();
            fprintf(stderr, "error: %s: No such file or directory\n", query_source);
            return 1;
        }
        status = eb_create_embedding_from_file(query_source, opts.model, &query_embedding);
    } else {
        status = eb_create_embedding_from_text(query_source, opts.model, &query_embedding);
    }

    if (status != EB_SUCCESS) {
        clear_progress();
        handle_error(status, "Failed to create query embedding");
        return 1;
    }

    // Search for similar embeddings
    eb_search_result_t* results = NULL;
    size_t result_count = 0;

    if (!opts.quiet) {
        clear_progress();
        show_progress("Searching...");
    }

    status = eb_search_embeddings(query_embedding, opts.threshold, opts.top_k, &results, &result_count);
    clear_progress();

    if (status != EB_SUCCESS) {
        handle_error(status, "Search failed");
        eb_destroy_embedding(query_embedding);
        return 1;
    }

    // No results found
    if (result_count == 0) {
        if (!opts.quiet) {
            printf("No matches found.\n");
        }
        eb_destroy_embedding(query_embedding);
        return 0;
    }

    // Print results
    if (opts.verbose && !opts.quiet) {
        printf("Found %zu matches:\n\n", result_count);
    }

    for (size_t i = 0; i < result_count; i++) {
        print_result(&results[i], &opts);
    }

    // Cleanup
    eb_destroy_embedding(query_embedding);
    eb_free_search_results(results, result_count);
    return 0;
} 