#include "embedding.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void test_basic_registration() {
    printf("Test 1: Basic registration...\n");
    
    eb_status_t status = eb_register_model(
        "test-model",
        512,
        true,
        "1.0.0",
        "Test model description"
    );
    assert(status == EB_SUCCESS && "Model registration should succeed");

    eb_model_info_t info;
    status = eb_get_model_info("test-model", &info);
    assert(status == EB_SUCCESS && "Getting model info should succeed");
    assert(info.dimensions == 512 && "Dimensions should match");
    assert(info.normalize_output == true && "Normalize flag should match");
    assert(strcmp(info.version, "1.0.0") == 0 && "Version should match");
    assert(strcmp(info.description, "Test model description") == 0 && "Description should match");

    // Cleanup
    free(info.version);
    free(info.description);
    eb_unregister_model("test-model");
}

static void test_duplicate_registration() {
    printf("Test 2: Duplicate registration...\n");
    
    // First registration
    eb_status_t status = eb_register_model(
        "test-model",
        512,
        true,
        "1.0.0",
        "Original description"
    );
    assert(status == EB_SUCCESS && "First registration should succeed");

    // Attempt duplicate registration
    status = eb_register_model(
        "test-model",
        256,  // Different dimensions
        false,
        "2.0.0",
        "Another description"
    );
    assert(status == EB_ERROR_INVALID_INPUT && "Duplicate registration should fail");

    // Cleanup
    eb_unregister_model("test-model");
}

static void test_invalid_parameters() {
    printf("Test 3: Invalid parameters...\n");
    
    // Test NULL name
    eb_status_t status = eb_register_model(
        NULL,
        512,
        true,
        "1.0.0",
        "Description"
    );
    assert(status == EB_ERROR_INVALID_INPUT && "NULL name should fail");

    // Test NULL version
    status = eb_register_model(
        "test-model",
        512,
        true,
        NULL,
        "Description"
    );
    assert(status == EB_ERROR_INVALID_INPUT && "NULL version should fail");

    // Test NULL description
    status = eb_register_model(
        "test-model",
        512,
        true,
        "1.0.0",
        NULL
    );
    assert(status == EB_ERROR_INVALID_INPUT && "NULL description should fail");

    // Test empty strings
    status = eb_register_model(
        "",  // Empty name
        512,
        true,
        "1.0.0",
        "Description"
    );
    assert(status == EB_ERROR_INVALID_INPUT && "Empty name should fail");

    status = eb_register_model(
        "test-model",
        512,
        true,
        "",  // Empty version
        "Description"
    );
    assert(status == EB_ERROR_INVALID_INPUT && "Empty version should fail");

    status = eb_register_model(
        "test-model",
        512,
        true,
        "1.0.0",
        ""  // Empty description
    );
    assert(status == EB_ERROR_INVALID_INPUT && "Empty description should fail");

    // Test zero dimensions
    status = eb_register_model(
        "test-model",
        0,
        true,
        "1.0.0",
        "Description"
    );
    assert(status == EB_ERROR_INVALID_INPUT && "Zero dimensions should fail");
}

static void test_list_models() {
    printf("Test 4: Listing models...\n");
    
    // Register a few models
    eb_status_t status = eb_register_model("model1", 512, true, "1.0.0", "First model");
    assert(status == EB_SUCCESS && "First model registration should succeed");
    
    status = eb_register_model("model2", 768, false, "2.0.0", "Second model");
    assert(status == EB_SUCCESS && "Second model registration should succeed");

    // Test listing models
    char** model_names;
    size_t count;
    status = eb_list_models(&model_names, &count);
    assert(status == EB_SUCCESS && "Listing models should succeed");
    assert(count == 2 && "Should have 2 models registered");

    // Verify model names
    bool found_model1 = false, found_model2 = false;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(model_names[i], "model1") == 0) found_model1 = true;
        if (strcmp(model_names[i], "model2") == 0) found_model2 = true;
        free(model_names[i]);  // Clean up each string
    }
    free(model_names);  // Clean up array
    
    assert(found_model1 && found_model2 && "Both models should be in the list");

    // Test listing with NULL parameters
    status = eb_list_models(NULL, &count);
    assert(status == EB_ERROR_INVALID_INPUT && "NULL output parameter should fail");
    
    status = eb_list_models(&model_names, NULL);
    assert(status == EB_ERROR_INVALID_INPUT && "NULL count parameter should fail");

    // Cleanup
    eb_unregister_model("model1");
    eb_unregister_model("model2");
}

static void test_model_info_errors() {
    printf("Test 5: Model info error cases...\n");
    
    eb_model_info_t info;
    
    // Test getting info for non-existent model
    eb_status_t status = eb_get_model_info("nonexistent-model", &info);
    assert(status == EB_ERROR_MODEL_ERROR && "Getting info for non-existent model should fail");

    // Test NULL parameters
    status = eb_get_model_info(NULL, &info);
    assert(status == EB_ERROR_INVALID_INPUT && "NULL model name should fail");
    
    status = eb_get_model_info("test-model", NULL);
    assert(status == EB_ERROR_INVALID_INPUT && "NULL info parameter should fail");
}

void test_model_registry() {
    printf("Testing model registry...\n");

    // Run all test cases
    test_basic_registration();
    test_duplicate_registration();
    test_invalid_parameters();
    test_list_models();
    test_model_info_errors();

    printf("All model registry tests passed!\n");
}

int main() {
    test_model_registry();
    return 0;
} 