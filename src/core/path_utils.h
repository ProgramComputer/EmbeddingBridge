#ifndef PATH_UTILS_H
#define PATH_UTILS_H

#include <stdbool.h>
#include <limits.h>

// Maximum path depth to prevent infinite loops
#define MAX_PATH_DEPTH 100

// Find the .embr repository root directory
char* find_repo_root(const char* start_path);

// Convert path to be relative to repository root
char* get_relative_path(const char* abs_path, const char* repo_root);

// Get absolute path from relative path and repo root
char* get_absolute_path(const char* rel_path, const char* repo_root);

// Get the path to the current repository
char* get_repository_path(void);

/**
 * URL parsing structure
 */
struct url_parts {
	char *scheme;    /* URL scheme (http, https, s3, etc.) */
	char *host;      /* Host name */
	char *port;      /* Port number as string, NULL if not specified */
	char *path;      /* Path component */
	char *query;     /* Query string, NULL if not present */
	char *fragment;  /* Fragment, NULL if not present */
};

/**
 * Parse a URL into its components
 * 
 * @param url The URL to parse
 * @return Pointer to a url_parts structure or NULL on failure
 * The caller is responsible for freeing the structure with free_url_parts()
 */
struct url_parts *parse_url(const char *url);

/**
 * Free resources associated with url_parts structure
 * 
 * @param parts URL parts structure to free
 */
void free_url_parts(struct url_parts *parts);

/**
 * Extract query parameter from a URL
 * 
 * @param url The URL to extract from
 * @param param The parameter name to find
 * @return Allocated string with parameter value, or NULL if not found
 * The caller is responsible for freeing the returned string
 */
char *get_url_param(const char *url, const char *param);

/**
 * Build a clean URL without query parameters
 * 
 * @param url Original URL with query parameters
 * @return New URL string without query parameters
 * The caller is responsible for freeing the returned string
 */
char *get_url_without_params(const char *url);

/**
 * Parse S3 URL into bucket and prefix components
 * 
 * @param url The S3 URL to parse (s3://bucket/prefix?region=xyz)
 * @param bucket_out Pointer to store allocated bucket name
 * @param prefix_out Pointer to store allocated prefix
 * @param region_out Pointer to store allocated region (can be NULL if not needed)
 * @return 0 on success, -1 on failure
 * The caller is responsible for freeing the returned strings
 */
int parse_s3_url(const char *url, char **bucket_out, char **prefix_out, char **region_out);

#endif 