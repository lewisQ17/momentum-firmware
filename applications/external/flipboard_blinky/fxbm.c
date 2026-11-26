#include "fxbm.h"
#include <storage/storage.h>

#define FXBM_MAX_SIZE 4096 // 128x64 bitmap is 1 KiB; generous upper bound

struct FxbmFile {
    uint32_t width;
    uint32_t height;
    uint8_t data[];
};

FxbmFile* fxbm_alloc(const char* path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    FxbmFile* fxbm = NULL;
    uint32_t size = sizeof(size);

    do {
        if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
            break;
        }

        if(storage_file_read(file, &size, size) != sizeof(size)) {
            break;
        }

        // size comes from the file: a huge value would crash malloc, a tiny one under-allocates the header
        if(size < sizeof(FxbmFile) || size > FXBM_MAX_SIZE) {
            break;
        }

        fxbm = malloc(size);
        if(storage_file_read(file, fxbm, size) != size) {
            free(fxbm);
            fxbm = NULL;
            break;
        }

        // the bitmap rows (w+7)/8 * h must fit in the data that was actually loaded
        uint64_t bitmap_bytes = (((uint64_t)fxbm->width + 7) / 8) * (uint64_t)fxbm->height;
        if(bitmap_bytes > size - sizeof(FxbmFile)) {
            free(fxbm);
            fxbm = NULL;
            break;
        }
    } while(false);

    storage_file_free(file);
    furi_record_close(RECORD_STORAGE); // Leak fix: balance furi_record_open
    return fxbm;
}

void fxbm_free(FxbmFile* fxbm) {
    free(fxbm);
}

uint32_t fxbm_get_width(FxbmFile* fxbm) {
    return fxbm->width;
}

uint32_t fxbm_get_height(FxbmFile* fxbm) {
    return fxbm->height;
}

uint8_t* fxbm_get_data(FxbmFile* fxbm) {
    return fxbm->data;
}
