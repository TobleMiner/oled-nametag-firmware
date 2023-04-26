#pragma once

#include "httpd.h"

httpd_t *webserver_preinit();
void webserver_init(httpd_t *httpd);
