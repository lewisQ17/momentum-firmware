#include "sequential_file.h"

char* sequential_file_resolve_path(
    Storage* storage,
    const char* dir,
    const char* prefix,
    const char* extension) {
    if(storage == NULL || dir == NULL || prefix == NULL || extension == NULL) {
        return NULL;
    }

    char file_path[256];
    int file_index = 0;

    do {
        // prefix/extension come from the cartridge: cap them so the _N suffix never gets truncated
        int written = snprintf(
            file_path, sizeof(file_path), "%s/%.128s_%d.%.8s", dir, prefix, file_index, extension);
        if(written < 0 || written >= (int)sizeof(file_path)) {
            return NULL;
        }
        file_index++;
    } while(storage_file_exists(storage, file_path));

    return strdup(file_path);
}

bool sequential_file_open(
    Storage* storage,
    File* file,
    const char* dir,
    const char* prefix,
    const char* extension) {
    if(storage == NULL || file == NULL || dir == NULL || prefix == NULL || extension == NULL) {
        return false;
    }

    char* file_path = sequential_file_resolve_path(storage, dir, prefix, extension);
    if(file_path == NULL) {
        return false;
    }

    bool success = storage_file_open(file, file_path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    free(file_path);

    return success;
}