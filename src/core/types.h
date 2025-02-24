#ifndef EB_TYPES_H
#define EB_TYPES_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "error.h"  // Include error codes

// Magic numbers for binary format
#define EB_MAGIC_VECTOR 0x53564245  // "EBVS"
#define EB_MAGIC_META  0x4D564245   // "EBVM"
// START OF SET THE VERSION HERE
// Version components
#define EB_VERSION_MAJOR 0
#define EB_VERSION_MINOR 1
#define EB_VERSION_PATCH 0

// Full version as string
#define EB_VERSION_STR "0.1.0"
// END OF SET THE VERSION HERE


// Full version as integer for compatibility checks
#define EB_VERSION     ((EB_VERSION_MAJOR << 16) | (EB_VERSION_MINOR << 8) | EB_VERSION_PATCH)

// Version compatibility macros
#define EB_GET_VERSION_MAJOR(v) ((v) >> 16)
#define EB_GET_VERSION_MINOR(v) (((v) >> 8) & 0xFF)
#define EB_GET_VERSION_PATCH(v) ((v) & 0xFF)
#define EB_MAKE_VERSION(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))
#define EB_VERSION_COMPATIBLE(v1, v2) (EB_GET_VERSION_MAJOR(v1) == EB_GET_VERSION_MAJOR(v2))

// Core data types
typedef enum {
    EB_FLOAT32,
    EB_FLOAT64,
    EB_INT32,
    EB_INT64
} eb_dtype_t;

// Compact binary metadata header
typedef struct {
    uint32_t magic;         // Magic number (EBVM)
    uint32_t version;       // Format version
    uint32_t key_length;    // Length of key string
    uint32_t value_length;  // Length of value string
    // Followed by key_length + value_length bytes of data
} __attribute__((packed)) eb_meta_header_t;

// Compact binary vector header
typedef struct {
    uint32_t magic;         // Magic number (EBVS)
    uint32_t version;       // Format version
    uint32_t dimensions;    // Number of dimensions
    uint32_t count;         // Number of vectors
    uint8_t dtype;         // Data type enum
    uint8_t flags;         // Bit flags (normalized, etc)
    uint8_t reserved[2];   // Reserved for future use
    // Followed by vector data
} __attribute__((packed)) eb_vector_header_t;

// Embedding structure
typedef struct {
    float* values;           // Array of embedding values
    size_t dimensions;       // Number of dimensions
    bool normalize;          // Whether to normalize the embedding
} eb_embedding_t;

// Storage types - now optimized for binary storage
typedef struct eb_metadata {
    char* key;              // Key string
    char* value;            // Value string
    uint32_t total_size;    // Total size including header
    struct eb_metadata* next;
} eb_metadata_t;

typedef struct eb_stored_vector {
    uint64_t id;                    // Vector ID
    eb_embedding_t* embedding;      // Vector data
    eb_metadata_t* metadata;        // Linked list of metadata
    char* model_version;            // Model version string
    uint64_t timestamp;            // Storage timestamp
    uint64_t parent_id;            // ID of parent vector (0 if none)
    struct eb_stored_vector* next;  // Next version in chain
} eb_stored_vector_t;

typedef struct {
    char* root_path;        // Path to .eb directory
    bool compression;       // Whether to compress objects
    bool deduplication;    // Whether to deduplicate vectors
    char* default_model;   // Default model identifier
    uint32_t flags;        // Additional configuration flags
    size_t cache_size;     // Size of memory cache (0 for default)
} eb_store_config_t;

typedef struct {
    eb_stored_vector_t* vectors;    // Hash table of vectors
    char* storage_path;             // Path to storage directory
    size_t vector_count;            // Number of stored vectors
} eb_store_t;

// Cross-model comparison method
typedef enum {
    EB_COMPARE_COSINE,           // Direct cosine similarity (same dimensions)
    EB_COMPARE_PROJECTION,       // Project to common space
    EB_COMPARE_SEMANTIC         // Use semantic similarity via text
} eb_comparison_method_t;

// Enhanced comparison result with cross-model support
typedef struct {
    float cosine_similarity;     // Overall similarity score
    float euclidean_distance;    // Normalized distance
    float* neighborhood_scores;   // Array of k-nearest neighbor preservation scores
    size_t neighborhood_count;    // Length of neighborhood_scores array
    float semantic_preservation;  // Semantic relationship preservation score
    eb_comparison_method_t method_used;  // Method used for comparison
} eb_comparison_result_t;

// Core functions
eb_status_t eb_create_embedding(
    void* data,
    size_t dimensions,
    size_t count,
    eb_dtype_t dtype,
    bool normalize,
    eb_embedding_t** out
);

void eb_destroy_embedding(eb_embedding_t* embedding);

// Metadata functions
eb_status_t eb_metadata_create(const char* key, const char* value, eb_metadata_t** out);
void eb_metadata_destroy(eb_metadata_t* metadata);

// Comparison functions
eb_status_t eb_compare_embeddings(
    const eb_embedding_t* embedding_a,
    const eb_embedding_t* embedding_b,
    size_t k_neighbors,
    eb_comparison_result_t** out_result
);

// Extended comparison with projection support
eb_status_t eb_compare_embeddings_extended(
    const eb_embedding_t* embedding_a,
    const eb_embedding_t* embedding_b,
    size_t k_neighbors,
    bool compute_neighborhood,
    eb_comparison_result_t** out_result
);

void eb_destroy_comparison_result(eb_comparison_result_t* result);

// Utility functions
eb_status_t eb_normalize_embedding(eb_embedding_t* embedding);
size_t eb_get_dtype_size(eb_dtype_t dtype);

// Formatting functions
eb_status_t eb_format_evolution(
    const eb_stored_vector_t* versions,
    size_t version_count,
    const eb_comparison_result_t* changes,
    size_t change_count,
    char** out_formatted,
    size_t* out_length
);

// Vector operations
eb_status_t eb_compute_cosine_similarity(
    const eb_embedding_t* a,
    const eb_embedding_t* b,
    float* result
);

eb_status_t eb_compute_euclidean_distance(
    const eb_embedding_t* a,
    const eb_embedding_t* b,
    float* result
);

eb_status_t eb_compute_neighborhood_preservation(
    const eb_embedding_t* old_vectors,
    const eb_embedding_t* new_vectors,
    size_t k,
    float* out
);

/*
 * PHASE 2+ Operations (Currently Disabled)
 * Complex version control operations to be re-enabled later
 */
/*
vector_delta_t* eb_compute_delta(vector_t* v1, vector_t* v2);
bool eb_apply_delta(vector_t* vector, vector_delta_t* delta);
bool eb_revert_delta(vector_t* vector, vector_delta_t* delta);
*/

// Memory store operations
eb_status_t eb_store_memory(
    eb_store_t* store,
    const eb_embedding_t* embedding,
    const eb_metadata_t* metadata,
    const char* model_version,
    uint64_t* out_id
);

eb_status_t eb_get_memory_evolution_with_changes(
    eb_store_t* store,
    uint64_t vector_id,
    uint64_t from_time,
    uint64_t to_time,
    eb_stored_vector_t** out_versions,
    size_t* out_version_count,
    eb_comparison_result_t** out_changes,
    size_t* out_change_count
);

// Comparison operations
eb_status_t eb_compare_memory_versions(
    const eb_stored_vector_t* version_a,
    const eb_stored_vector_t* version_b,
    eb_comparison_result_t* out_result
);

// Version compatibility function
static inline bool eb_check_version_compatible(uint32_t version) {
    return EB_VERSION_COMPATIBLE(version, EB_VERSION);
}

// Magic number validation
static inline bool eb_is_valid_vector_magic(uint32_t magic) {
    return magic == EB_MAGIC_VECTOR;
}

static inline bool eb_is_valid_meta_magic(uint32_t magic) {
    return magic == EB_MAGIC_META;
}

// New cross-model comparison function
eb_status_t eb_compare_embeddings_cross_model(
    const eb_embedding_t* embedding_a,
    const eb_embedding_t* embedding_b,
    const char* model_version_a,
    const char* model_version_b,
    eb_comparison_method_t preferred_method,
    eb_comparison_result_t** out_result
);

// Add these if they don't exist
// #define EB_VECTOR_MAGIC 0x4542565F  // "EBV_"
// #define EB_VERSION 1                 // Already defined as 0x00000001

typedef enum {
    EB_OBJ_VECTOR = 1,
    EB_OBJ_META = 2
} eb_object_type_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t obj_type;
    uint32_t flags;
    uint32_t size;
    uint8_t hash[32];
} eb_object_header_t;

#endif // EB_TYPES_H 