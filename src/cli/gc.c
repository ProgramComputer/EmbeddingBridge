/*
 * EmbeddingBridge - Garbage Collection CLI Command
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
#include <time.h>
#include "cli.h"
#include "../core/gc.h"
#include "../core/error.h"

static const char* GC_USAGE = 
    "usage: embr gc [options]\n"
    "\n"
    "Clean up unnecessary files and optimize the local repository\n"
    "\n"
    "Options:\n"
    "  -n, --dry-run          Don't actually remove anything, just show what would be done\n"
    "  -f, --force            Force running garbage collection\n"
    "  --prune[=<date>]       Prune unreferenced objects older than date (default: 2.weeks.ago)\n"
    "  --no-prune             Don't prune any unreferenced objects\n"
    "  -v, --verbose          Report pruned objects\n"
    "  -q, --quiet            Suppress all output\n"
    "  -h, --help             Show this help message\n"
    "\n"
    "Examples:\n"
    "  embr gc                    # Run standard garbage collection\n"
    "  embr gc --prune=now        # Remove all unreferenced objects\n"
    "  embr gc --no-prune         # Don't remove any unreferenced objects\n"
    "  embr gc -n                 # Show what would be removed without removing\n";

/* Default grace period for unreferenced objects (2 weeks) */
#define DEFAULT_PRUNE_EXPIRE_SECONDS (14 * 24 * 60 * 60)

int cmd_gc(int argc, char** argv) {
    /* Check for help flag */
    if (has_option(argc, argv, "--help") || has_option(argc, argv, "-h")) {
        printf("%s", GC_USAGE);
        return 0;
    }
    
    /* Parse command options */
    bool dry_run = has_option(argc, argv, "--dry-run") || has_option(argc, argv, "-n");
    bool quiet = has_option(argc, argv, "--quiet") || has_option(argc, argv, "-q");
    bool verbose = has_option(argc, argv, "--verbose") || has_option(argc, argv, "-v");
    bool force = has_option(argc, argv, "--force") || has_option(argc, argv, "-f");
    bool no_prune = has_option(argc, argv, "--no-prune");
    
    /* Get prune expiration time or set to NULL if --no-prune */
    const char* prune_expire = no_prune ? "never" : get_option_value(argc, argv, NULL, "--prune");
    
    /* Run garbage collection */
    eb_gc_result_t result;
    eb_status_t status;
    
    if (!quiet && !dry_run)
        printf("Performing garbage collection...\n");
    
    if (verbose && !quiet) {
        printf("Prune expire: %s\n", prune_expire ? prune_expire : "2.weeks.ago (default)");
    }
    
    if (dry_run) {
        printf("Dry run - no changes will be made\n");
        
        /* Find unreferenced objects but don't remove them */
        char* unreferenced[1000];
        size_t count = 0;
        
        /* Use default expiration time for dry run since we can't access parse_expire_time */
        time_t expire_time;
        if (prune_expire) {
            if (strcmp(prune_expire, "now") == 0) {
                expire_time = time(NULL);
            } else if (strcmp(prune_expire, "never") == 0) {
                expire_time = 0;
            } else {
                /* Default to 2 weeks ago for simplicity in dry-run mode */
                expire_time = time(NULL) - DEFAULT_PRUNE_EXPIRE_SECONDS;
                printf("Note: Using default expiration time of 2 weeks for preview\n");
            }
        } else {
            expire_time = time(NULL) - DEFAULT_PRUNE_EXPIRE_SECONDS;
        }
        
        status = gc_find_unreferenced(unreferenced, 1000, &count, expire_time);
        
        if (status != EB_SUCCESS) {
            handle_error(status, "Failed to find unreferenced objects");
            return 1;
        }
        
        printf("Would remove %zu unreferenced objects\n", count);
        
        if (verbose && count > 0) {
            printf("Objects that would be removed:\n");
            for (size_t i = 0; i < count && i < 20; i++)
                printf("  %s\n", unreferenced[i]);
            
            if (count > 20)
                printf("  ... and %zu more\n", count - 20);
        }
        
        /* Free allocated memory */
        for (size_t i = 0; i < count; i++)
            free(unreferenced[i]);
        
        return 0;
    }
    
    /* Run the actual garbage collection */
    status = gc_run(prune_expire, false, &result);
    
    if (status != EB_SUCCESS) {
        handle_error(status, "Garbage collection failed");
        return 1;
    }
    
    /* Show results unless quiet mode is enabled */
    if (!quiet) {
        printf("%s\n", result.message);
        if (verbose && result.objects_removed > 0) {
            printf("Objects removed: %d\n", result.objects_removed);
            printf("Bytes freed: %zu\n", result.bytes_freed);
        }
        
        if (result.objects_removed == 0) {
            printf("No unreferenced objects to remove.\n");
        } else {
            printf("Removed %d unreferenced embedding objects\n", result.objects_removed);
        }
    }
    
    return 0;
} 