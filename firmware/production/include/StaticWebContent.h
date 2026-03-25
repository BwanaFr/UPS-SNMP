#ifndef _STATIC_WEB_CONTENT_H__
#define _STATIC_WEB_CONTENT_H__

#include <esp_http_server.h>

typedef struct static_file {
    const char* file_name;
    size_t len;
    const char* mimetype;
    const char* etag;
    const uint8_t* data;
    const char* last_modified;
}static_file_t;

esp_err_t static_get_handler( httpd_req_t *req );


#endif
