/*
 * EmbeddingBridge - Command Line Option Parsing
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
#include <stdbool.h>
#include "cli.h"
#include "../core/debug.h"

/**
 * Determines if an option requires an argument based on the short_opts string.
 * 
 * @param opt The short option character to check
 * @param short_opts getopt-style string where "a:bc" means 'a' requires an arg
 * @return true if option requires an argument, false otherwise
 */
static bool option_requires_arg(char opt, const char* short_opts)
{
    if (!short_opts)
        return false;
    
    const char* p = short_opts;
    while (*p) {
        if (*p == opt && p[1] == ':')
            return true;
        p++;
    }
    return false;
}

/**
 * Finds the short option character for a given long option
 *
 * @param long_opt The long option to find
 * @param long_opts Array of long option strings
 * @param short_opts getopt-style string 
 * @return The matching short option character or 0 if not found
 */
static char find_short_for_long(const char* long_opt, const char** long_opts, const char* short_opts)
{
    if (!long_opt || !long_opts || !short_opts)
        return 0;
    
    // Skip leading dashes
    if (long_opt[0] == '-') {
        if (long_opt[1] == '-')
            long_opt += 2;  // Skip "--"
        else
            long_opt += 1;  // Skip "-"
    }
    
    // Find the long option
    for (int i = 0; long_opts[i] != NULL; i++) {
        const char* curr_long = long_opts[i];
        // Skip leading dashes
        if (curr_long[0] == '-') {
            if (curr_long[1] == '-')
                curr_long += 2;  // Skip "--"
            else
                curr_long += 1;  // Skip "-"
        }
        
        if (strcmp(long_opt, curr_long) == 0) {
            // Find the corresponding short option
            int index = i;
            int short_idx = 0;
            
            // Count non-colon characters in short_opts
            for (const char* s = short_opts; *s; s++) {
                if (*s != ':') {
                    if (short_idx == index)
                        return *s;
                    short_idx++;
                }
            }
        }
    }
    
    return 0;
}

/**
 * Parses command line arguments in a Git-like fashion, allowing options 
 * to appear before, after, or intermingled with positional arguments.
 *
 * @param argc Number of command line arguments
 * @param argv Array of command line arguments
 * @param short_opts getopt-style string of short options
 * @param long_opts NULL-terminated array of long option strings
 * @param callback Function to call for each option found
 * @param context Context to pass to the callback
 * @param positional Array to store positional arguments (can be NULL)
 * @param positional_count Pointer to store count of positional args (can be NULL)
 * @return 0 on success, non-zero on error
 */
int parse_git_style_options(
    int argc, 
    char** argv,
    const char* short_opts,  
    const char** long_opts,
    option_callback_t callback,
    void* context,
    char** positional,      
    int* positional_count)
{
    int pos_count = 0;
    
    // Skip program name
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        
        // Check if this is an option
        if (arg[0] == '-') {
            char short_opt = 0;
            const char* long_opt = NULL;
            const char* opt_arg = NULL;
            bool needs_arg = false;
            
            // Handle short options
            if (arg[1] != '-') {
                // Short option
                short_opt = arg[1];
                needs_arg = option_requires_arg(short_opt, short_opts);
                
                // Handle -abc style combined short options
                if (arg[2] != '\0' && !needs_arg) {
                    // TODO: Handle combined short options if needed
                    fprintf(stderr, "Warning: Combined short options not fully supported\n");
                }
            } else {
                // Long option
                long_opt = arg;
                
                // Check for --option=value style
                char* equals = strchr(arg, '=');
                if (equals) {
                    // Extract the value part
                    *equals = '\0';  // Temporarily null-terminate the option part
                    opt_arg = equals + 1;
                    
                    // Find the corresponding short option
                    short_opt = find_short_for_long(arg, long_opts, short_opts);
                    *equals = '=';   // Restore the equals sign
                    needs_arg = true;  // Already have the arg
                } else {
                    // Find the corresponding short option
                    short_opt = find_short_for_long(arg, long_opts, short_opts);
                    needs_arg = option_requires_arg(short_opt, short_opts);
                }
            }
            
            // Get the argument for options that require it
            if (needs_arg && !opt_arg) {
                if (i + 1 < argc) {
                    opt_arg = argv[++i];
                } else {
                    fprintf(stderr, "Error: Option %s requires an argument\n", arg);
                    return -1;
                }
            }
            
            // Special check for help option
            if ((short_opt == 'h' && strchr(short_opts, 'h')) || 
                (long_opt && strstr(long_opt, "--help"))) {
                if (callback) {
                    return callback(short_opt, long_opt, opt_arg, context);
                }
            }
            
            // Call the callback function
            if (callback) {
                int result = callback(short_opt, long_opt, opt_arg, context);
                if (result != 0)
                    return result;
            }
        } else {
            // This is a positional argument
            if (positional && pos_count < argc) {
                positional[pos_count] = (char*)arg;
            }
            pos_count++;
        }
    }
    
    // Update the count of positional arguments
    if (positional_count)
        *positional_count = pos_count;
    
    return 0;
}

/**
 * Helper function to check if an option is present in command line args
 */
static bool has_option_internal(int argc, char** argv, const char* option)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], option) == 0)
            return true;
    }
    return false;
}

/**
 * Helper function to get an option's value from command line args
 */
static const char* get_option_value_internal(int argc, char** argv, const char* short_opt, const char* long_opt)
{
    for (int i = 1; i < argc - 1; i++) {
        if ((short_opt && strcmp(argv[i], short_opt) == 0) ||
            (long_opt && strcmp(argv[i], long_opt) == 0)) {
            return argv[i + 1];
        }
        
        // Also handle --option=value style
        if (long_opt && strncmp(argv[i], long_opt, strlen(long_opt)) == 0) {
            const char* equals = strchr(argv[i], '=');
            if (equals && equals == argv[i] + strlen(long_opt))
                return equals + 1;
        }
    }
    return NULL;
}

/**
 * Helper function to get a float option value
 */
static float get_float_option_internal(int argc, char** argv, const char* short_opt, const char* long_opt, float default_value)
{
    const char* value = get_option_value_internal(argc, argv, short_opt, long_opt);
    if (value)
        return atof(value);
    return default_value;
}

/**
 * Helper function to get an integer option value
 */
static int get_int_option_internal(int argc, char** argv, const char* short_opt, const char* long_opt, int default_value)
{
    const char* value = get_option_value_internal(argc, argv, short_opt, long_opt);
    if (value)
        return atoi(value);
    return default_value;
} 