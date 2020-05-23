#include "nvs_flash.h"
#include "nvs.h"

#ifndef _STORAGE_H_
#define _STORAGE_H_

nvs_handle_t storage_handle;
int storage_open_files = 0;

typedef struct {
    const char * name;
    void * data;
    size_t data_len;
} storage_file;

// clears the data at a name
void storage_delete(const char * fname) {
    ESP_ERROR_CHECK(nvs_erase_key(storage_handle, fname));
}

// opens a file given a filename
storage_file storage_open(const char * fname) {
    if (storage_open_files++ == 0) {
        ESP_ERROR_CHECK(nvs_flash_init());
        ESP_ERROR_CHECK(nvs_open("userstorage", NVS_READWRITE, &storage_handle));
    }
    storage_file out;
    out.name = fname;
    out.data_len = 0;
    ESP_ERROR_CHECK(nvs_get_blob(storage_handle, fname, out.data, &out.data_len));
    return out;
}

// writes to and closes a file
void storage_close(storage_file file) {
    ESP_ERROR_CHECK(nvs_set_blob(storage_handle, file.name, file.data, file.data_len));
    if (--storage_open_files == 0) {
        nvs_close(storage_handle);
    }
}

#endif