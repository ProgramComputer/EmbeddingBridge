#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "store.h"

// Test helper to check status
static void check_status(eb_status_t status, const char* message) {
    if (status != EB_SUCCESS) {
        fprintf(stderr, "Error: %s (status=%d)\n", message, status);
        exit(1);
    }
}

int main() {
    printf("Testing library memory management...\n");
    
    // Initialize store
    eb_store_t* store = NULL;
    eb_store_config_t config = {
        .root_path = ":memory:"
    };
    check_status(
        eb_store_init(&config, &store),
        "Store initialization"
    );
    
    // Create test embedding
    float test_data[] = {1.0f, 2.0f, 3.0f, 4.0f};
    eb_embedding_t* embedding;
    check_status(
        eb_create_embedding(
            test_data,
            4,  // dimensions
            1,  // count
            EB_FLOAT32,
            false,  // don't normalize
            &embedding
        ),
        "Embedding creation"
    );
    
    // Create test metadata
    eb_metadata_t* metadata;
    check_status(
        eb_metadata_create("text", "test data", &metadata),
        "Metadata creation"
    );
    
    // Store the embedding
    uint64_t vector_id = 0;
    check_status(
        eb_store_memory(
            store,
            embedding,
            metadata,
            "test-model",
            &vector_id
        ),
        "Memory storage"
    );
    
    // Retrieve the evolution
    eb_stored_vector_t* versions;
    size_t version_count;
    eb_comparison_result_t* changes;
    size_t change_count;
    
    check_status(
        eb_get_memory_evolution_with_changes(
            store,
            vector_id,
            0,  // from_time
            (uint64_t)-1,  // to_time (max)
            &versions,
            &version_count,
            &changes,
            &change_count
        ),
        "Memory evolution retrieval"
    );
    
    // Format the evolution
    char* formatted;
    size_t formatted_length;
    check_status(
        eb_format_evolution(
            versions,
            version_count,
            changes,
            change_count,
            &formatted,
            &formatted_length
        ),
        "Evolution formatting"
    );
    
    // Clean up everything
    free(formatted);
    eb_destroy_comparison_result(changes);
    eb_destroy_stored_vectors(versions, version_count);
    eb_destroy_embedding(embedding);
    eb_metadata_destroy(metadata);
    eb_store_destroy(store);
    
    printf("All memory management tests completed successfully\n");
    return 0;
} 