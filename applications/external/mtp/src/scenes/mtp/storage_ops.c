#include "storage_ops.h"
#include "utils.h"

char* get_base_path_from_storage_id(uint32_t storage_id) {
    if(storage_id == INTERNAL_STORAGE_ID) {
        return STORAGE_INT_PATH_PREFIX;
    } else if(storage_id == EXTERNAL_STORAGE_ID) {
        return STORAGE_EXT_PATH_PREFIX;
    }
    return NULL;
}

char* get_path_from_handle(AppMTP* mtp, uint32_t handle) {
    FileHandle* current = mtp->handles;
    while(current != NULL) {
        if(current->handle == handle) {
            return current->path;
        }
        current = current->next;
    }
    return NULL;
}

uint32_t issue_object_handle(AppMTP* mtp, char* path) {
    // Look up first (every node, including the last) so a known path neither
    // leaks a copy nor gets a duplicate node.
    FileHandle* tail = NULL;
    for(FileHandle* cur = mtp->handles; cur != NULL; cur = cur->next) {
        if(strcmp(cur->path, path) == 0) {
            return cur->handle;
        }
        tail = cur;
    }

    char* path_store = malloc(strlen(path) + 1);
    strcpy(path_store, path);

    FileHandle* node = malloc(sizeof(FileHandle));
    node->handle = tail ? tail->handle + 1 : 1;
    node->path = path_store;
    node->next = NULL;
    if(tail) {
        tail->next = node;
    } else {
        mtp->handles = node;
    }
    return node->handle;
}

uint32_t update_object_handle_path(AppMTP* mtp, uint32_t handle, char* path) {
    FileHandle* current = mtp->handles;
    while(current != NULL) {
        if(current->handle == handle) {
            free(current->path);
            current->path = path;
            return handle;
        }
        current = current->next;
    }
    return 0;
}

int list_and_issue_handles(
    AppMTP* mtp,
    uint32_t storage_id,
    uint32_t association,
    uint32_t* handles,
    uint32_t max_handles) {
    Storage* storage = mtp->storage;
    char* base_path = get_base_path_from_storage_id(storage_id);
    if(base_path == NULL) {
        base_path = "";
    }

    File* dir = storage_file_alloc(storage);
    if(association == 0xffffffff) {
        // count the objects in the root directory
        storage_dir_open(dir, base_path);
    } else {
        char* path = get_path_from_handle(mtp, association);
        FURI_LOG_I("MTP", "Association path: %s", path);
        if(path == NULL) {
            storage_file_free(dir);
            return 0;
        }
        storage_dir_open(dir, path);
        base_path = path;
    }

    int count = 0;
    FileInfo fileinfo;
    char* file_name = malloc(sizeof(char) * 256);
    char* full_path = malloc(sizeof(char) * 256);
    while(storage_dir_read(dir, &fileinfo, file_name, 256)) {
        if(file_info_is_dir(&fileinfo)) {
            FURI_LOG_I("MTP", "Found directory: %s", file_name);
        } else {
            FURI_LOG_I("MTP", "Found file: %s", file_name);
        }

        // Skip entries whose path does not fit (a truncated path would hit the wrong file)
        if(!merge_path(full_path, MTP_PATH_SIZE, base_path, file_name)) {
            FURI_LOG_W("MTP", "Path too long, skipping: %s", file_name);
            continue;
        }
        FURI_LOG_I("MTP", "Full path: %s", full_path);

        uint32_t handle = issue_object_handle(mtp, full_path);
        // The caller's buffer holds max_handles entries; keep counting but never write past it
        if(handles != NULL && (uint32_t)count < max_handles) {
            handles[count] = handle;
        }
        count++;
    }

    FURI_LOG_I("MTP", "Getting number of objects in storage %ld", storage_id);
    FURI_LOG_I("MTP", "Base path: %s", base_path);
    FURI_LOG_I("MTP", "Association: %ld", association);
    FURI_LOG_I("MTP", "Number of objects: %d", count);

    storage_dir_close(dir);
    storage_file_free(dir);
    free(file_name);
    free(full_path);
    return count;
}
