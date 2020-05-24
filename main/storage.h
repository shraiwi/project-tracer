#include "nvs_flash.h"
#include "nvs.h"

#include "string.h"
#include "stdlib.h"

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
        ESP_ERROR_CHECK(nvs_open("userstorage", NVS_READWRITE, &storage_handle));
    }
    storage_file out;
    out.name = fname;
    out.data_len = 0;
    esp_err_t err = nvs_get_blob(storage_handle, fname, NULL, &out.data_len);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);
    if (err != ESP_OK) {
        out.data = NULL;
        out.data_len = 0;
        return out;
    }
    out.data = malloc(out.data_len);
    err = nvs_get_blob(storage_handle, fname, out.data, &out.data_len);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        free(out.data);
        out.data = NULL;
        out.data_len = 0;
    }
    return out;
}

// writes to and closes a file. the file will be invalid after this!
void storage_close(storage_file file) {
    ESP_ERROR_CHECK(nvs_set_blob(storage_handle, file.name, file.data, file.data_len));
    ESP_ERROR_CHECK(nvs_commit(storage_handle));
    file.data_len = 0;
    if (--storage_open_files == 0) {
        nvs_close(storage_handle);
    }
}

#endif