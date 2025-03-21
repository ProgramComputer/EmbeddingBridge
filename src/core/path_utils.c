#include "path_utils.h"
#include "debug.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>

char* find_repo_root(const char* start_path) {
    char current_path[PATH_MAX];
    char *absolute_path;
    
    DEBUG_PRINT("find_repo_root called with path: %s", start_path ? start_path : "(null)");
    
    // If start_path is empty or ".", use getcwd
    if (!start_path || !strlen(start_path) || strcmp(start_path, ".") == 0) {
        if (!getcwd(current_path, PATH_MAX)) {
            DEBUG_PRINT("Failed to get current working directory");
            return NULL;
        }
        DEBUG_PRINT("Using current directory: %s", current_path);
    } else {
        absolute_path = realpath(start_path, current_path);
        if (!absolute_path) {
            DEBUG_PRINT("Failed to resolve path: %s", start_path);
            return NULL;
        }
        DEBUG_PRINT("Resolved to absolute path: %s", current_path);
    }

    int depth = 0;
    while (depth < MAX_PATH_DEPTH) {
        char eb_path[PATH_MAX];
        snprintf(eb_path, PATH_MAX, "%s/.eb", current_path);
        DEBUG_PRINT("Checking for .eb at: %s", eb_path);
        
        struct stat st;
        if (stat(eb_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            DEBUG_PRINT("Found .eb repository at: %s", current_path);
            return strdup(current_path);
        }

        // Move up one directory
        char *last_slash = strrchr(current_path, '/');
        if (!last_slash || last_slash == current_path) {
            DEBUG_PRINT("Reached root directory without finding .eb");
            break;
        }
        *last_slash = '\0';
        depth++;
        DEBUG_PRINT("Moving up to parent: %s", current_path);
    }

    DEBUG_PRINT("No .eb repository found in parent directories");
    return NULL;
}

char* get_relative_path(const char* abs_path, const char* repo_root) {
    char real_abs[PATH_MAX];
    char real_root[PATH_MAX];
    
    if (!realpath(abs_path, real_abs) || !realpath(repo_root, real_root)) {
        return NULL;
    }

    if (strncmp(real_abs, real_root, strlen(real_root)) != 0) {
        return NULL;
    }

    const char* rel = real_abs + strlen(real_root);
    while (*rel == '/') rel++;
    
    return strdup(rel);
}

char* get_absolute_path(const char* rel_path, const char* repo_root) {
    char result[PATH_MAX];
    snprintf(result, PATH_MAX, "%s/%s", repo_root, rel_path);
    
    char* abs_path = realpath(result, NULL);
    if (!abs_path) {
        // If file doesn't exist yet, just concatenate
        return strdup(result);
    }
    return abs_path;
}

/**
 * Get the path to the current repository
 * 
 * @return Dynamically allocated string with the repository path, or NULL if not in a repository
 * The caller is responsible for freeing the returned string.
 */
char* get_repository_path(void) {
    return find_repo_root(NULL);
}

/* URL parsing implementation functions */

void free_url_parts(struct url_parts *parts)
{
	if (!parts)
		return;

	free(parts->scheme);
	free(parts->host);
	free(parts->port);
	free(parts->path);
	free(parts->query);
	free(parts->fragment);
	free(parts);
}

/**
 * Parse a URL into its components
 * 
 * @param url The URL to parse
 * @return Pointer to a url_parts structure or NULL on failure
 * The caller is responsible for freeing the structure with free_url_parts()
 */
struct url_parts *parse_url(const char *url)
{
	if (!url)
		return NULL;

	struct url_parts *parts = calloc(1, sizeof(struct url_parts));
	if (!parts)
		return NULL;

	/* Find scheme (protocol) */
	const char *scheme_end = strstr(url, "://");
	if (!scheme_end) {
		free_url_parts(parts);
		return NULL;
	}

	size_t scheme_len = scheme_end - url;
	parts->scheme = malloc(scheme_len + 1);
	if (!parts->scheme) {
		free_url_parts(parts);
		return NULL;
	}
	
	memcpy(parts->scheme, url, scheme_len);
	parts->scheme[scheme_len] = '\0';

	/* Skip over the "://" */
	const char *host_start = scheme_end + 3;

	/* Find the end of host[:port] section */
	const char *host_end = strchr(host_start, '/');
	if (!host_end) {
		/* No path component, everything after scheme is host */
		parts->host = strdup(host_start);
		parts->path = strdup("");
	} else {
		/* Extract host */
		size_t host_len = host_end - host_start;
		char *host_port = malloc(host_len + 1);
		if (!host_port) {
			free_url_parts(parts);
			return NULL;
		}
		
		memcpy(host_port, host_start, host_len);
		host_port[host_len] = '\0';
		
		/* Check for port */
		char *port_sep = strchr(host_port, ':');
		if (port_sep) {
			*port_sep = '\0';
			parts->port = strdup(port_sep + 1);
			parts->host = strdup(host_port);
		} else {
			parts->host = strdup(host_port);
		}
		free(host_port);
		
		/* Find query string and fragment */
		const char *path_start = host_end;
		const char *query_start = strchr(path_start, '?');
		const char *fragment_start = strchr(path_start, '#');
		
		/* Handle query string */
		if (query_start) {
			size_t path_len = query_start - path_start;
			parts->path = malloc(path_len + 1);
			if (!parts->path) {
				free_url_parts(parts);
				return NULL;
			}
			
			memcpy(parts->path, path_start, path_len);
			parts->path[path_len] = '\0';
			
			/* Extract query string */
			if (fragment_start && fragment_start > query_start) {
				size_t query_len = fragment_start - query_start - 1;
				parts->query = malloc(query_len + 1);
				if (!parts->query) {
					free_url_parts(parts);
					return NULL;
				}
				
				memcpy(parts->query, query_start + 1, query_len);
				parts->query[query_len] = '\0';
			} else {
				parts->query = strdup(query_start + 1);
			}
		} else {
			/* No query string */
			if (fragment_start) {
				size_t path_len = fragment_start - path_start;
				parts->path = malloc(path_len + 1);
				if (!parts->path) {
					free_url_parts(parts);
					return NULL;
				}
				
				memcpy(parts->path, path_start, path_len);
				parts->path[path_len] = '\0';
			} else {
				parts->path = strdup(path_start);
			}
		}
		
		/* Handle fragment */
		if (fragment_start) {
			parts->fragment = strdup(fragment_start + 1);
		}
	}
	
	return parts;
}

/**
 * Extract query parameter from a URL
 * 
 * @param url The URL to extract from
 * @param param The parameter name to find
 * @return Allocated string with parameter value, or NULL if not found
 * The caller is responsible for freeing the returned string
 */
char *get_url_param(const char *url, const char *param)
{
	if (!url || !param)
		return NULL;

	struct url_parts *parts = parse_url(url);
	if (!parts || !parts->query)
		return NULL;

	char *result = NULL;
	char *query_copy = strdup(parts->query);
	if (!query_copy) {
		free_url_parts(parts);
		return NULL;
	}

	/* Parse query parameters */
	char *token = strtok(query_copy, "&");
	while (token) {
		char *equals = strchr(token, '=');
		if (equals) {
			*equals = '\0';
			if (strcmp(token, param) == 0) {
				result = strdup(equals + 1);
				break;
			}
		}
		token = strtok(NULL, "&");
	}

	free(query_copy);
	free_url_parts(parts);
	return result;
}

/**
 * Build a clean URL without query parameters
 * 
 * @param url Original URL with query parameters
 * @return New URL string without query parameters
 * The caller is responsible for freeing the returned string
 */
char *get_url_without_params(const char *url)
{
	if (!url)
		return NULL;

	struct url_parts *parts = parse_url(url);
	if (!parts)
		return NULL;

	/* Calculate the length of the clean URL */
	size_t len = strlen(parts->scheme) + 3; /* scheme:// */
	len += strlen(parts->host);
	
	if (parts->port)
		len += strlen(parts->port) + 1; /* :port */
		
	len += strlen(parts->path);
	
	/* Create the clean URL */
	char *clean_url = malloc(len + 1);
	if (!clean_url) {
		free_url_parts(parts);
		return NULL;
	}
	
	/* Construct the URL */
	if (parts->port) {
		snprintf(clean_url, len + 1, "%s://%s:%s%s", 
			parts->scheme, parts->host, parts->port, parts->path);
	} else {
		snprintf(clean_url, len + 1, "%s://%s%s", 
			parts->scheme, parts->host, parts->path);
	}
	
	free_url_parts(parts);
	return clean_url;
}

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
int parse_s3_url(const char *url, char **bucket_out, char **prefix_out, char **region_out)
{
	if (!url || !bucket_out || !prefix_out)
		return -1;

	/* Get clean URL without query parameters */
	char *clean_url = get_url_without_params(url);
	if (!clean_url)
		return -1;

	/* Parse the clean URL */
	struct url_parts *parts = parse_url(clean_url);
	free(clean_url);
	
	if (!parts)
		return -1;

	/* Verify it's an S3 URL */
	if (strcmp(parts->scheme, "s3") != 0) {
		free_url_parts(parts);
		return -1;
	}

	/* Extract bucket and prefix */
	*bucket_out = strdup(parts->host);
	
	/* Skip leading slash in path if present */
	const char *prefix = parts->path;
	while (*prefix == '/')
		prefix++;
		
	*prefix_out = strdup(prefix);

	/* Extract region if requested */
	if (region_out) {
		*region_out = get_url_param(url, "region");
	}

	free_url_parts(parts);
	
	/* Validate outputs */
	if (!*bucket_out || !*prefix_out) {
		free(*bucket_out);
		free(*prefix_out);
		if (region_out && *region_out)
			free(*region_out);
		return -1;
	}
	
	return 0;
} 