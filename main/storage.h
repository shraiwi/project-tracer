#include "nvs_flash.h"
#include "nvs.h"

#include "string.h"
#include "stdlib.h"

#ifndef _STORAGE_H_
#define _STORAGE_H_

// THIS API IS NOW OBSELETE IN FAVOR FOR THE SPIFFS FILESYSTEM

nvs_handle_t storage_handle;
int storage_open_files = 0;

// a file. if the data pointer is null, you can reallocate the memory and use it freely.
typedef struct {
    const char * name;
    void * data;
    size_t data_len;
} storage_file;

// deletes a file given its name
void storage_delete(const char * fname) {
    if (storage_open_files == 0) {
        ESP_ERROR_CHECK(nvs_open("userstorage", NVS_READWRITE, &storage_handle));
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_erase_key(storage_handle, fname));
    ESP_ERROR_CHECK(nvs_commit(storage_handle));
    if (storage_open_files == 0) {
        nvs_close(storage_handle);
    }
}

// checks if a file exists.
bool storage_exists(const char * fname) {
    if (storage_open_files == 0) {
        ESP_ERROR_CHECK(nvs_open("userstorage", NVS_READWRITE, &storage_handle));
    }
    size_t len;
    esp_err_t err = nvs_get_blob(storage_handle, fname, NULL, &len);
    if (storage_open_files == 0) {
        nvs_close(storage_handle);
    }
    return err != ESP_ERR_NVS_NOT_FOUND;
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