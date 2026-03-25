#ifndef _UPS_WEB_SERVER_H__
#define _UPS_WEB_SERVER_H__

#include <esp_http_server.h>
#include <string>
#include <map>
#include <functional>

class Webserver{
public:
    Webserver();
    virtual ~Webserver() = default;

    /**
     * Stars the Webserver
     */
    void start();

    /**
     * Stops the webserver
     */
    void stop();

    /**
     * Setup
     */
    void setup();

    /**
     * Sets credentials for protected pages
     */
    void setCredentials(const char* userName, const char* password);

    /**
     * Creates the authentication digest
     */
    static void createAuthDigest(std::string& digest, const char* usernane, const char* password);

private:
    std::string authDigest_;
    httpd_handle_t server_;

    /**
     * Checks authentication of the user
     * @param req HTTP request
     * @return true if authentication is success full
     */
    bool checkAuthentication(httpd_req_t *req);

    //HTTP handlers
    static bool isAPICall(httpd_req_t *req);
    static esp_err_t post_handler(httpd_req_t *req);                //Handle POST
    static esp_err_t get_handler(httpd_req_t *req);                 //Handle GET
    static esp_err_t ota_post_handler( httpd_req_t *req );          //Handle OTA POST request
    static esp_err_t cfg_get_handler( httpd_req_t *req );           //Handle configuration GET request
    static esp_err_t cfg_post_handler( httpd_req_t *req );          //Handle configuration POST request
    static esp_err_t status_get_handler( httpd_req_t *req );        //Handle status GET request
    static esp_err_t oid_info_get_handler( httpd_req_t *req );      //SNMP OID GET request
    static esp_err_t tasks_info_get_handler( httpd_req_t *req );    //FreeRTOS tasks GET request

    static esp_err_t not_found_handler( httpd_req_t *req );     //404 not found error
    static esp_err_t redirect_handler( httpd_req_t *req );      //Redirects to index.html

    typedef std::function<esp_err_t(httpd_req_t *)> WebHandlerFunction;
    typedef const std::map<const std::string, const WebHandlerFunction> HandlerMap;
    static HandlerMap postHandlers;
    static HandlerMap getHandlers;
    // esp_err_t (*handler)(httpd_req_t *r);
};

extern Webserver webServer;
#endif