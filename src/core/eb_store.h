#ifndef EB_STORE_H
#define EB_STORE_H

#include "types.h"

// Add the store initialization function declaration
eb_status_t eb_store_init(const eb_store_config_t* config, eb_store_t** out);

/* Get current hash for a source file */
eb_status_t get_current_hash(const char* root, const char* source, 
                           char* hash, size_t hash_size); 

/* Store embedding file */
eb_status_t store_embedding_file(const char* embedding_path,
                               const char* source_file,
                               const char* base_dir,
                               const char* provider);

eb_status_t eb_store_vector_memory(eb_store_t* store,
                                 const eb_embedding_t* embedding,
                                 const eb_metadata_t* metadata,
                                 const char* model_version,
                                 uint64_t* out_id);

eb_status_t eb_get_vector_memory(eb_store_t* store,
                               uint64_t vector_id,
                               eb_embedding_t** out_embedding,
                               eb_metadata_t** out_metadata);

eb_status_t eb_store_init_memory(eb_store_t** out);

eb_status_t eb_store_metadata(eb_store_t* store,
                            const eb_metadata_t* metadata,
                            char out_hash[65]);

eb_status_t eb_get_metadata(eb_store_t* store,
                          const char* hash,
                          eb_metadata_t** out_metadata);

eb_status_t eb_update_refs(eb_store_t* store,
                         const char* vector_hash,
                         const char* meta_hash,
                         const char* model_version);

eb_status_t eb_get_ref(eb_store_t* store,
                      const char* vector_hash,
                      char meta_hash[65]);

eb_status_t get_version_history(const char* root, const char* source, 
                              eb_stored_vector_t** out_versions, size_t* out_count); 

eb_status_t read_object(eb_store_t* store,
                       const char* hash,
                       void** out_data,
                       size_t* out_size,
                       eb_object_header_t* out_header); 

#endif // EB_STORE_H 