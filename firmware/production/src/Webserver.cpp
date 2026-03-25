#include <Webserver.hpp>
#include <Arduino.h>
#include <esp_log.h>
#include <esp_tls_crypto.h>
#include <esp_app_format.h>
#include <esp_bootloader_desc.h>
#include <esp_http_server.h>
#include <esp_flash_partitions.h>
#include <esp_flash_encrypt.h>
#include <esp_flash.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <Configuration.hpp>
#include <UPSHIDDevice.hpp>
#include <Temperature.hpp>
#include <Pinger.hpp>
#include <ETH.h>
#include <GlobalStatus.hpp>
#include <NetworkStatus.hpp>
#include "StaticWebContent.h"

#define MAX_OTA_FILENAME 64
#define DEVICE_NAME "ESP32"
#define HTTPD_401   "401 UNAUTHORIZED"           /*!< HTTP Response 401 */

#define API_URI "/api/"

#define FW_HEADER_LEN 8
static const char FW_HEADER[FW_HEADER_LEN] = "UPS-FW";
static const char* TAG = "Webserver";

const Webserver::HandlerMap Webserver::postHandlers = {
    {API_URI "ota", Webserver::ota_post_handler},
    {API_URI "config", Webserver::cfg_post_handler},
};

const Webserver::HandlerMap Webserver::getHandlers = {
    {API_URI "config", Webserver::cfg_get_handler},
    {API_URI "oid_info", Webserver::oid_info_get_handler},
    {API_URI "status", Webserver::status_get_handler},
    {API_URI "tasks", Webserver::tasks_info_get_handler},
};

Webserver webServer;

//-----------------------------------------------------------------------------

bool Webserver::checkAuthentication(httpd_req_t *req)
{
    if(authDigest_.empty()){
        //Always authenticated
        return true;
    }
    char* auth_buffer = (char*)calloc(512, sizeof(char));
    if(!auth_buffer){
        ESP_LOGE(TAG, "Unable to allocate auth_buffer!");
    }
    size_t buf_len = httpd_req_get_hdr_value_len( req, "Authorization" ) + 1;
    if ( ( buf_len > 1 ) && ( buf_len <= 512 ) )
    {
        if ( httpd_req_get_hdr_value_str( req, "Authorization", auth_buffer, buf_len ) == ESP_OK )
        {
            if ( !strncmp(authDigest_.c_str(), auth_buffer, buf_len ) )
            {
                ESP_LOGI(TAG, "Authenticated!" );
                free(auth_buffer);
                return true;
            }
        }
    }
    ESP_LOGI(TAG, "Not authenticated" );
    httpd_resp_set_status( req, HTTPD_401 );
    httpd_resp_set_hdr( req, "Connection", "keep-alive" );
    httpd_resp_set_hdr( req, "WWW-Authenticate", "Basic realm=\"UPS monitoring\"" );
    httpd_resp_send( req, NULL, 0 );
    free(auth_buffer);
    return false;
}

bool Webserver::isAPICall(httpd_req_t *req)
{;
    if(strncmp(API_URI, req->uri, strlen(API_URI)) == 0){
        return true;
    }
    return false;
}

esp_err_t Webserver::post_handler(httpd_req_t *req)
{
    Webserver* instance = static_cast<Webserver*>(req->user_ctx);
    //Protect POST API requests
    if(isAPICall(req)){
        if(!instance->checkAuthentication(req)){
            return ESP_OK;
        }
    }
    const HandlerMap::const_iterator it = Webserver::postHandlers.find(req->uri);
    if(it != postHandlers.end()){
        return it->second(req);
    }
    return not_found_handler(req);
}

esp_err_t Webserver::get_handler(httpd_req_t *req)
{
    Webserver* instance = static_cast<Webserver*>(req->user_ctx);
    if((strcmp(req->uri, "/") == 0) || (strlen(req->uri) == 0)){
        return redirect_handler(req);
    }
    const HandlerMap::const_iterator it = Webserver::getHandlers.find(req->uri);
    if(it != getHandlers.end()){
        return it->second(req);
    }
    //Try our chance on static files
    if(static_get_handler(req) == ESP_OK){
        return ESP_OK;
    }
    return not_found_handler(req);
}


static esp_err_t register_partition(size_t offset, size_t size, const char *label, esp_partition_type_t type, esp_partition_subtype_t subtype, const esp_partition_t **p_partition)
{
    // If the partition table contains this partition, then use it, otherwise register it.
    *p_partition = esp_partition_find_first(type, subtype, NULL);
    if ((*p_partition) == NULL) {
        esp_err_t error = esp_partition_register_external(NULL, offset, size, label, type, subtype, p_partition);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register %s partition (err=0x%x)", "PrimaryBTLDR", error);
            return error;
        }
    }
    ESP_LOGI(TAG, "Use <%s> partition (0x%08" PRIx32 ")", (*p_partition)->label, (*p_partition)->address);
    return ESP_OK;
}

//-----------------------------------------------------------------------------
esp_err_t Webserver::ota_post_handler( httpd_req_t *req )
{
    Webserver* instance = static_cast<Webserver*>(req->user_ctx);
    char buf[256];
    httpd_resp_set_status( req, HTTPD_500 );    // Assume failure
    bool isFWFile = false;
    size_t ret, remaining = req->content_len;
    uint32_t blSize = 0;

    esp_ota_handle_t update_handle = 0 ;
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    const esp_partition_t *running          = esp_ota_get_running_partition();
    esp_err_t err = ESP_OK;

    const esp_partition_t *primary_bootloader = nullptr;

    ESP_LOGI(TAG, "Receiving");
    if(remaining < FW_HEADER_LEN){
        ESP_LOGE(TAG, "OTA firmware too short!");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Corrupted file");
        return ESP_FAIL;
    }

    esp_bootloader_desc_t bootLoaderInfo;
    esp_ota_get_bootloader_description(NULL, &bootLoaderInfo);
    ESP_LOGI(TAG, "Bootloader version : %d, IDF : %s", bootLoaderInfo.version, bootLoaderInfo.idf_ver);

    if ( update_partition == NULL )
    {
        ESP_LOGI(TAG, "Unable to get update partition!");
        goto return_failure;
    }
    ESP_LOGI(TAG, "Writing partition: type %d, subtype %d, offset 0x%08x", update_partition-> type, update_partition->subtype, update_partition->address);
    ESP_LOGI(TAG, "Running partition: type %d, subtype %d, offset 0x%08x", running->type, running->subtype, running->address);

    //Get file header
    if((ret = httpd_req_recv( req, buf, FW_HEADER_LEN)) < FW_HEADER_LEN){
        ESP_LOGI(TAG, "Unable to get file header");
        goto return_failure;
    }
    remaining -= ret;

    if(buf[0] == ESP_IMAGE_HEADER_MAGIC){
        ESP_LOGI(TAG, "Got image header magic, assuming raw image.");
        isFWFile = false;   //Raw .bin image
    }else if(strncmp(buf, FW_HEADER, FW_HEADER_LEN) == 0){
        ESP_LOGI(TAG, "Found firmware header.");
        isFWFile = true;
        const size_t infoLen = sizeof(esp_bootloader_desc_t::idf_ver) + sizeof(uint32_t);
        if((ret = httpd_req_recv( req, buf, infoLen)) < infoLen){
            ESP_LOGI(TAG, "Unable to get FW bootloader information");
            goto return_failure;
        }
        remaining -= ret;
        blSize = *(uint32_t*)(buf);
        ESP_LOGI(TAG, "Bootloader size : %u, version : %s", blSize, (buf + sizeof(uint32_t)));

        bool doBLUpdate = false;
        if(strcmp((buf + sizeof(uint32_t)), bootLoaderInfo.idf_ver) != 0){
            ESP_LOGI(TAG, "Bootloader IDF version mismatch, needs to update it.");
            doBLUpdate = true;
        }else{
            ESP_LOGI(TAG, "Bootloader update not needed.");
        }


        if(doBLUpdate){
            //Move to bootloader
            ESP_ERROR_CHECK(register_partition(ESP_PRIMARY_BOOTLOADER_OFFSET, ESP_BOOTLOADER_SIZE, "PrimaryBTLDR", ESP_PARTITION_TYPE_BOOTLOADER, ESP_PARTITION_SUBTYPE_BOOTLOADER_PRIMARY, &primary_bootloader));
            err = esp_ota_begin(update_partition, blSize, &update_handle);
            if (err != ESP_OK)
            {
                ESP_LOGI(TAG, "Bootloader : esp_ota_begin failed (%s)", esp_err_to_name(err));
                goto return_failure;
            }
            err = esp_ota_set_final_partition(update_handle, primary_bootloader, true);
            if (err != ESP_OK)
            {
                ESP_LOGI(TAG, "Bootloader : esp_ota_set_final_partition failed (%s)", esp_err_to_name(err));
                goto return_failure;
            }
        }
        ESP_LOGI(TAG, "Rem. bytes : %u", remaining);
        while(blSize > 0){
                if ( ( ret = httpd_req_recv( req, buf, std::min( blSize, (uint32_t)sizeof( buf ) ) ) ) <= 0 )
                {
                    if ( ret == HTTPD_SOCK_ERR_TIMEOUT )
                    {
                    // Retry receiving if timeout occurred
                    continue;
                    }
                    ESP_LOGI(TAG, "Bootloader : httpd_req_recv failed (%s)", esp_err_to_name(err));
                    goto return_failure;
                }
                size_t bytes_read = ret;
                blSize -= bytes_read;
                remaining -= bytes_read;
                if(doBLUpdate){
                    err = esp_ota_write( update_handle, buf, bytes_read);
                    if (err != ESP_OK)
                    {
                        ESP_LOGI(TAG, "Bootloader : esp_ota_write failed (%s)", esp_err_to_name(err));
                        goto return_failure;
                    }
                }
        }
        if(doBLUpdate){
            if(esp_ota_end(update_handle) == ESP_OK){
                ESP_LOGI(TAG, "Booloader updated sucessfully!");
                esp_partition_deregister_external(primary_bootloader);
                primary_bootloader = nullptr;
            }else{
                ESP_LOGE(TAG, "Unable to update bootloader. esp_ota_end failed(%s)", esp_err_to_name(err));
                goto return_failure;
            }
        }
    }else{
        ESP_LOGE(TAG, "Unable to determinate the type of file!");
        goto return_failure;
    }

    //Now update the app image
    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
    if (err != ESP_OK)
    {
        ESP_LOGI(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
        goto return_failure;
    }
    //If not FW file, write the 8 missing bytes
    if(!isFWFile){
        err = esp_ota_write( update_handle, buf, FW_HEADER_LEN);
        if (err != ESP_OK)
        {
            goto return_failure;
        }
    }

    while ( remaining > 0 )
    {
        // Read the data for the request        //TODO: Review this
        if ( ( ret = httpd_req_recv( req, buf, std::min( remaining, (size_t)sizeof( buf ) ) ) ) <= 0 )
        {
            if ( ret == HTTPD_SOCK_ERR_TIMEOUT )
            {
            // Retry receiving if timeout occurred
            continue;
            }

            goto return_failure;
        }

        size_t bytes_read = ret;

        remaining -= bytes_read;
        err = esp_ota_write( update_handle, buf, bytes_read);
        if (err != ESP_OK)
        {
            goto return_failure;
        }
    }

    ESP_LOGI(TAG, "Receiving done" );

    // End response
    if ( ( esp_ota_end(update_handle)                   == ESP_OK ) &&
        ( esp_ota_set_boot_partition(update_partition) == ESP_OK ) )
    {
        ESP_LOGI(TAG, "OTA Success?!");
        ESP_LOGI(TAG, "Rebooting" );

        httpd_resp_set_status( req, HTTPD_200 );
        httpd_resp_send( req, NULL, 0 );

        vTaskDelay( 2000 / portTICK_RATE_MS);
        esp_restart();

        return ESP_OK;
    }
    ESP_LOGI(TAG, "OTA End failed (%s)!", esp_err_to_name(err));

return_failure:
    if ( update_handle )
    {
        esp_ota_abort(update_handle);
    }

    if( primary_bootloader )
    {
        esp_partition_deregister_external(primary_bootloader);
    }

    httpd_resp_set_status( req, HTTPD_500 );    // Assume failure
    httpd_resp_send( req, NULL, 0 );

    return ESP_FAIL;
}

esp_err_t Webserver::cfg_get_handler( httpd_req_t *req )
{
    Webserver* instance = static_cast<Webserver*>(req->user_ctx);

    if(instance->checkAuthentication(req)){
        httpd_resp_set_status( req, HTTPD_200 );
        httpd_resp_set_hdr( req, "Connection", "keep-alive" );
        httpd_resp_set_type(req, "application/json");
        std::string cfgJSON;
        configuration.toJSONString(cfgJSON);
        httpd_resp_send( req, cfgJSON.c_str(), cfgJSON.length());
        return ESP_OK;
    }
    return ESP_OK;
}

esp_err_t Webserver::cfg_post_handler( httpd_req_t *req )
{
    Webserver* instance = static_cast<Webserver*>(req->user_ctx);
    size_t dataSize = std::min((size_t)512, req->content_len);
    std::string content;
    content.resize(dataSize);
    int ret = httpd_req_recv(req, &content[0], dataSize);
    if (ret <= 0) {  /* 0 return value indicates connection closed */
        /* Check if timeout occurred */
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            /* In case of timeout one can choose to retry calling
            * httpd_req_recv(), but to keep it simple, here we
            * respond with an HTTP 408 (Request Timeout) error */
            httpd_resp_send_408(req);
        }
        /* In case of error, returning ESP_FAIL will
        * ensure that the underlying socket is closed */
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "JSON (%u) : %s", dataSize, content.c_str());
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, content);
    if(error){
        httpd_resp_set_status( req, HTTPD_500 );    // Assume failure
        httpd_resp_send( req, "{\"message\":\"JSON parse failure\"}", HTTPD_RESP_USE_STRLEN );
        return ESP_OK;
    }
    bool changed, valid, ipChanged;
    configuration.fromJSON(doc, changed, valid, ipChanged);

    /* Send a simple response */
    if(ipChanged){
        httpd_resp_send(req, "{\"message\":\"Configuration updated!\", \"IPChanged\":true}", HTTPD_RESP_USE_STRLEN);
    }else if(changed){
        httpd_resp_send(req, "{\"message\":\"Configuration updated!\", \"IPChanged\":false}", HTTPD_RESP_USE_STRLEN);
    }else if(!valid){
        httpd_resp_set_status( req, HTTPD_500 );    // Assume failure
        httpd_resp_send( req, "{\"message\":\"Invalid parameters!\"}", HTTPD_RESP_USE_STRLEN );
    }else{
        httpd_resp_send(req, "{\"message\":\"Configuration not changed!\"}", HTTPD_RESP_USE_STRLEN);
    }
    return ESP_OK;
}

esp_err_t Webserver::status_get_handler( httpd_req_t *req )
{
    Webserver* instance = static_cast<Webserver*>(req->user_ctx);

    httpd_resp_set_status( req, HTTPD_200 );
    httpd_resp_set_hdr( req, "Connection", "keep-alive" );
    httpd_resp_set_type(req, "application/json");
    //Build a JSON with UPS status
    JsonDocument doc;
    //TODO: Add all providers
    //Sets UPS status to JSON file
    upsDevice.insertStatusInJSON(doc);

#ifdef HAS_TEMP_PROBE
    tempProbe.insertStatusInJSON(doc);
#endif
    //Network status
    networkStatus.insertStatusInJSON(doc);
    //Global status (uptime, CPU temp)
    globalStatus.insertStatusInJSON(doc);
    //Ping results (if configured)
    pinger.insertStatusInJSON(doc);

    //Serialize to string
    std::string upsStatusJSON;
    serializeJson(doc, upsStatusJSON);
    httpd_resp_send( req, upsStatusJSON.c_str(), upsStatusJSON.length());
    return ESP_OK;
}

esp_err_t Webserver::oid_info_get_handler( httpd_req_t *req )
{
    Webserver* instance = static_cast<Webserver*>(req->user_ctx);

    httpd_resp_set_status( req, HTTPD_200 );
    httpd_resp_set_hdr( req, "Connection", "keep-alive" );
    httpd_resp_set_type(req, "application/json");
    //Build a JSON with device name + known OID
    JsonDocument doc;
    std::string name;
    configuration.getDeviceName(name);
    doc["deviceName"] = name;

    //Insert OID in document
    StatusProvider::listOIDInJSON(doc);

    //Serialize to string
    std::string jsonStr;
    serializeJson(doc, jsonStr);
    httpd_resp_send( req, jsonStr.c_str(), jsonStr.length());
    return ESP_OK;
}

esp_err_t Webserver::tasks_info_get_handler( httpd_req_t *req )
{
    Webserver* instance = static_cast<Webserver*>(req->user_ctx);

    httpd_resp_set_status( req, HTTPD_200 );
    httpd_resp_set_hdr( req, "Connection", "keep-alive" );
    httpd_resp_set_type(req, "application/json");
    //Build a JSON with tasks info
    JsonDocument doc;
    JsonArray data = doc["tasks"].to<JsonArray>();

    TaskStatus_t *pxTaskStatusArray;
    /* Take a snapshot of the number of tasks in case it changes while this
      function is executing. */
    UBaseType_t uxArraySize = uxTaskGetNumberOfTasks();
    /* Allocate a TaskStatus_t structure for each task. An array could be
      allocated statically at compile time. */
    pxTaskStatusArray = (TaskStatus_t*)pvPortMalloc( uxArraySize * sizeof( TaskStatus_t ) );
    if( pxTaskStatusArray != NULL )
    {
        unsigned long ulTotalRunTime;
        /* Generate raw status information about each task. */
        uxArraySize = uxTaskGetSystemState( pxTaskStatusArray, uxArraySize, &ulTotalRunTime );
        doc["totalRuntime"] = ulTotalRunTime;
        /* For percentage calculations. */
        ulTotalRunTime /= 100UL;
        /* Avoid divide by zero errors. */
        if( ulTotalRunTime > 0 )
        {
            /* For each populated position in the pxTaskStatusArray array,
            format the raw data as human readable ASCII data. */
            for(UBaseType_t  x = 0; x < uxArraySize; x++ )
            {
                JsonObject taskEntry = data.add<JsonObject>();
                /* What percentage of the total run time has the task used?
                    This will always be rounded down to the nearest integer.
                    ulTotalRunTimeDiv100 has already been divided by 100. */
                double ulStatsAsPercentage = pxTaskStatusArray[x].ulRunTimeCounter / ulTotalRunTime;
                taskEntry["name"] = pxTaskStatusArray[x].pcTaskName;
                taskEntry["runTime"] = pxTaskStatusArray[x].ulRunTimeCounter;
                taskEntry["percent"] = ulStatsAsPercentage;
                taskEntry["priority"] = pxTaskStatusArray[x].uxCurrentPriority;
                if(pxTaskStatusArray[x].xCoreID != tskNO_AFFINITY){
                    taskEntry["core"] = pxTaskStatusArray[x].xCoreID;
                }
            }
        }

        /* The array is no longer needed, free the memory it consumes. */
        vPortFree( pxTaskStatusArray );
    }

    //Serialize to string
    std::string jsonStr;
    serializeJson(doc, jsonStr);
    httpd_resp_send( req, jsonStr.c_str(), jsonStr.length());
    return ESP_OK;
}

esp_err_t Webserver::not_found_handler( httpd_req_t *req )
{
    httpd_resp_set_status( req, HTTPD_404 );
    const char* html = "<html><h1>Error 404. Page not found.</h1></html>";
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

esp_err_t Webserver::redirect_handler( httpd_req_t *req )
{
    httpd_resp_set_status(req, "308 Permanent Redirect");
    httpd_resp_set_hdr(req, "Location", "/index.html");
    const char* html = "Moved.";
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

Webserver::Webserver() : server_(nullptr)
{
}

void Webserver::start()
{
    if(!server_){
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.uri_match_fn = httpd_uri_match_wildcard;
        // Start the httpd server
        ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
        if (httpd_start(&server_, &config) == ESP_OK) {
            httpd_uri_t all_post_handler =
            {
                .uri       = "/*",
                .method    = HTTP_POST,
                .handler   = post_handler,
                .user_ctx  = this
            };
            httpd_register_uri_handler(server_, &all_post_handler);

            httpd_uri_t all_get_handler =
            {
                .uri       = "/*",
                .method    = HTTP_GET,
                .handler   = get_handler,
                .user_ctx  = this
            };
            httpd_register_uri_handler(server_, &all_get_handler);
            return;
        }
        ESP_LOGI(TAG, "Error starting server!");
        server_ = nullptr;
    }
}

void Webserver::stop()
{
    if(server_){
        httpd_stop(server_);
        server_ = nullptr;
    }
}

void Webserver::setup()
{

}

void Webserver::setCredentials(const char* userName, const char* password)
{
    createAuthDigest(authDigest_, userName, password);
}

void Webserver::createAuthDigest(std::string& digest, const char* usernane, const char* password)
{
    digest = "";
    if(usernane != nullptr){
        int rc = 0;
        char* user_info;
        if(password != nullptr){
            rc = asprintf(&user_info, "%s:%s", usernane, password);
        }else{
            rc = asprintf(&user_info, "%s:", usernane);
        }
        if(rc < 0){
            ESP_LOGE(TAG, "asprintf() returned: %d", rc);
            return;
        }
        if (!user_info) {
            ESP_LOGE(TAG, "No enough memory for user information");
            return;
        }
        size_t n = 0;
        //Get len of the base64 string
        esp_crypto_base64_encode(NULL, 0, &n, (const unsigned char *)user_info, strlen(user_info));
        char * digestCStr = (char*)calloc(1, 6 + n + 1);
        if(digestCStr) {
            strcpy(digestCStr, "Basic ");
            size_t out;
            esp_crypto_base64_encode((unsigned char *)digestCStr + 6, n, &out, (const unsigned char *)user_info, strlen(user_info));
            digest = digestCStr;
            free(digestCStr);
        }
        free(user_info);
    }
}
