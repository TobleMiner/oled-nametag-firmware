#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "httpd.h"
#include "util.h"
#include "futil.h"
#include "mime.h"

esp_err_t httpd_response_write(struct httpd_request_ctx* ctx, const char* buff, size_t len) {
  return httpd_resp_send_chunk(ctx->req, buff, len);
}

esp_err_t httpd_response_write_string(struct httpd_request_ctx* ctx, const char* str) {
  return httpd_response_write(ctx, str, strlen(str));
}

esp_err_t httpd_send_error_msg(struct httpd_request_ctx* ctx, const char* status, const char* msg) {
  esp_err_t err = httpd_set_status(ctx, status);
  if(msg) {
    err = httpd_resp_send_chunk(ctx->req, msg, strlen(msg));
  }
  httpd_finalize_response(ctx);
  return err;
}

esp_err_t httpd_send_error(struct httpd_request_ctx* ctx, const char* status) {
  return httpd_send_error_msg(ctx, status, NULL);
}

static esp_err_t request_send_chunk(void* ctx, char* buff, size_t len) {
  struct httpd_request_ctx* req_ctx = ctx;
  return httpd_resp_send_chunk(req_ctx->req, buff, len);
}

static esp_err_t request_send_chunk_templ(void* ctx, char* buff, size_t len) {
  struct httpd_slice_ctx* slice_ctx = ctx;
  return httpd_resp_send_chunk(slice_ctx->req_ctx->req, buff, len);
}

static void httpd_request_ctx_init(struct httpd_request_ctx* ctx, httpd_req_t* req) {
  INIT_LIST_HEAD(ctx->form_data);
  INIT_LIST_HEAD(ctx->query_params);
  ctx->req = req;
}

static esp_err_t invocation_wrapper(httpd_req_t* req) {
  struct httpd_request_ctx ctx;
  struct httpd_handler* hndlr = req->user_ctx;
  struct httpd_slice_ctx slice_ctx = {
    .req_ctx = &ctx,
    .parent = NULL,
    .parent_ctx = NULL,
  };

  httpd_request_ctx_init(&ctx, req);

  return hndlr->ops->invoke(hndlr, &slice_ctx);
}


static esp_err_t embedded_template_send(struct httpd_slice_ctx *ctx, struct httpd_embedded_template_file_handler* hndlr) {
  esp_err_t err;

  if((err = httpd_resp_set_type(ctx->req_ctx->req, hndlr->mime_type))) {
    goto fail;
  }

  if((err = template_apply_embedded(hndlr->templ, hndlr->start, hndlr->end, request_send_chunk_templ, ctx))) {
    printf("Failed to apply embedded template: %d\n", err);
    goto fail;
  }

fail:
  return err;
}

#define HTTPD_HANDLER_TO_HTTPD_EMBEDDED_TEMPLATE_FILE_HANDER(hndlr) \
  container_of((hndlr), struct httpd_embedded_template_file_handler, handler)

static void httpd_free_embedded_template_file_handler(struct httpd_handler* hndlr) {
  struct httpd_embedded_template_file_handler* hndlr_file = HTTPD_HANDLER_TO_HTTPD_EMBEDDED_TEMPLATE_FILE_HANDER(hndlr);

  template_free_instance(hndlr_file->templ);
  // Allthough uri_handler.uri is declared const we use it with dynamically allocated memory
  free((char *)hndlr->uri_handler.uri);
}

static esp_err_t httpd_embedded_template_file_include_handler(struct httpd_handler* hndlr, struct httpd_slice_ctx *slice_ctx) {
  struct httpd_embedded_template_file_handler* hndlr_file = HTTPD_HANDLER_TO_HTTPD_EMBEDDED_TEMPLATE_FILE_HANDER(hndlr);
  esp_err_t err = ESP_OK;

  err = embedded_template_send(slice_ctx, hndlr_file);
  if (err) {
    httpd_send_error_msg(slice_ctx->req_ctx, HTTPD_500, "Failed to render template");
  }

  return err;
}

static esp_err_t httpd_embedded_template_file_request_handler(struct httpd_handler* hndlr, struct httpd_slice_ctx *slice_ctx) {
  struct httpd_embedded_template_file_handler* hndlr_file = HTTPD_HANDLER_TO_HTTPD_EMBEDDED_TEMPLATE_FILE_HANDER(hndlr);
  httpd_req_t* req = slice_ctx->req_ctx->req;
  esp_err_t err;

  printf("httpd: Delivering templated embedded content for %s from @%p\n", req->uri, hndlr_file->start);

  if ((err = httpd_resp_set_type(req, hndlr_file->mime_type))) {
    return err;
  }

  err = httpd_embedded_template_file_include_handler(hndlr, slice_ctx);
  if (!err) {
    httpd_resp_send_chunk(req, NULL, 0);
  }

  return err;
}

const struct httpd_handler_ops httpd_embedded_template_file_handler_ops = {
  .free = httpd_free_embedded_template_file_handler,
  .include = httpd_embedded_template_file_include_handler,
  .invoke = httpd_embedded_template_file_request_handler,
};

static esp_err_t add_embedded_file_template(struct httpd_embedded_template_file_handler **ret, struct httpd* httpd, const char *path, const char *mime_type, const void *start, const void *end) {
  esp_err_t err;
  char *uri;
  struct httpd_embedded_template_file_handler* hndlr;

  hndlr = calloc(1, sizeof(struct httpd_embedded_template_file_handler));
  if(!hndlr) {
    err = ESP_ERR_NO_MEM;
    goto fail;
  }
  hndlr->mime_type = mime_type;
  hndlr->start = start;
  hndlr->end = end;

  if((err = template_alloc_instance_embedded(&hndlr->templ, &httpd->templates, start, end))) {
    printf("Failed to allocate embedded template instance: %d\n", err);
    goto fail_handler_alloc;
  }

  uri = strdup(path);
  if(!uri) {
    err = ESP_ERR_NO_MEM;
    goto fail_template_alloc;
  }

  futil_normalize_path(uri);
/*
  if (!futil_is_path_relative(uri)) {
    if((err = futil_relpath_inplace(uri, httpd->webroot))) {
      printf("Failed to create relative path\n");
      goto fail_uri_alloc;
    }
  }
*/
  hndlr->handler.uri_handler.uri = uri;
  hndlr->handler.uri_handler.method = HTTP_GET;
  hndlr->handler.uri_handler.handler = invocation_wrapper;
  hndlr->handler.uri_handler.user_ctx = &hndlr->handler;

  hndlr->handler.ops = &httpd_embedded_template_file_handler_ops;

  printf("httpd: Registering embedded template handler at '%s' from template @%p\n", uri, start);

  if((err = httpd_register_uri_handler(httpd->server, &hndlr->handler.uri_handler))) {
    goto fail_uri_alloc;
  }

  LIST_APPEND(&hndlr->handler.list, &httpd->handlers);

  if (ret) {
    *ret = hndlr;
  }

  return ESP_OK;

fail_uri_alloc:
  free(uri);
fail_template_alloc:
  template_free_instance(hndlr->templ);
fail_handler_alloc:
  free(hndlr);
fail:
  return err;
}

esp_err_t httpd_add_embedded_template_file(struct httpd *httpd, const char *path, const char *mime_type, const void *ptr_start, const void *ptr_end) {
  return add_embedded_file_template(NULL, httpd, path, mime_type, ptr_start, ptr_end);
}

static esp_err_t template_send(struct httpd_slice_ctx *ctx, struct httpd_static_template_file_handler* hndlr) {
  esp_err_t err;
  const char* mime;

  mime = mime_get_type_from_filename(hndlr->path);
  if(mime) {
    printf("Got mime type: %s\n", mime);
    if((err = httpd_resp_set_type(ctx->req_ctx->req, mime))) {
      goto fail;
    }
  }

  if((err = template_apply(hndlr->templ, hndlr->path, request_send_chunk_templ, ctx))) {
    printf("Failed to apply static template: %d\n", err);
    goto fail;
  }

fail:
  return err;
}

#define HTTPD_HANDLER_TO_HTTPD_STATIC_TEMPLATE_FILE_HANDER(hndlr) \
  container_of((hndlr), struct httpd_static_template_file_handler, handler)

static void httpd_free_static_template_file_handler(struct httpd_handler* hndlr) {
  struct httpd_static_template_file_handler* hndlr_file = HTTPD_HANDLER_TO_HTTPD_STATIC_TEMPLATE_FILE_HANDER(hndlr);

  free(hndlr_file->path);
  template_free_instance(hndlr_file->templ);
  // Allthough uri_handler.uri is declared const we use it with dynamically allocated memory
  free((char *)hndlr->uri_handler.uri);
}

static esp_err_t httpd_static_template_file_include_handler(struct httpd_handler* hndlr, struct httpd_slice_ctx *slice_ctx) {
  struct httpd_static_template_file_handler* hndlr_file = HTTPD_HANDLER_TO_HTTPD_STATIC_TEMPLATE_FILE_HANDER(hndlr);
  esp_err_t err = ESP_OK;

  err = template_send(slice_ctx, hndlr_file);
  if (err) {
    httpd_send_error_msg(slice_ctx->req_ctx, HTTPD_500, "Failed to render template");
  }

  return err;
}

static esp_err_t httpd_static_template_file_request_handler(struct httpd_handler* hndlr, struct httpd_slice_ctx *slice_ctx) {
  struct httpd_static_template_file_handler* hndlr_file = HTTPD_HANDLER_TO_HTTPD_STATIC_TEMPLATE_FILE_HANDER(hndlr);
  esp_err_t err;
  const char* mime;

  printf("httpd: Delivering templated static content for %s from %s\n", slice_ctx->req_ctx->req->uri, hndlr_file->path);

  mime = mime_get_type_from_filename(hndlr_file->path);
  if(mime) {
    printf("Got mime type: %s\n", mime);
    if((err = httpd_resp_set_type(slice_ctx->req_ctx->req, mime))) {
      return err;
    }
  }

  err = httpd_static_template_file_include_handler(hndlr, slice_ctx);
  if (!err) {
    httpd_resp_send_chunk(slice_ctx->req_ctx->req, NULL, 0);
  }

  return err;
}

const struct httpd_handler_ops httpd_static_template_file_handler_ops = {
  .free = httpd_free_static_template_file_handler,
  .include = httpd_static_template_file_include_handler,
  .invoke = httpd_static_template_file_request_handler,
};

static esp_err_t add_static_file_template__(struct httpd_static_template_file_handler **ret, struct httpd* httpd, char* path) {
  esp_err_t err;
  char* uri, *abspath = path;
  struct httpd_static_template_file_handler* hndlr = calloc(1, sizeof(struct httpd_static_template_file_handler));
  if(!hndlr) {
    err = ESP_ERR_NO_MEM;
    goto fail;
  }

  abspath = futil_path_concat(path, httpd->webroot);
  if (!abspath) {
    err = ESP_ERR_NO_MEM;
    goto fail_handler_alloc;
  }

  if((err = template_alloc_instance(&hndlr->templ, &httpd->templates, abspath))) {
    printf("Failed to allocate handler instance: %d\n", err);
    goto fail_handler_alloc;
  }

  hndlr->path = strdup(abspath);
  if(!hndlr->path) {
    err = ESP_ERR_NO_MEM;
    goto fail_template_alloc;
  }

  uri = strdup(path);
  if(!uri) {
    err = ESP_ERR_NO_MEM;
    goto fail_path_alloc;
  }

  futil_normalize_path(uri);
/*
  if (!futil_is_path_relative(uri)) {
    if((err = futil_relpath_inplace(uri, httpd->webroot))) {
      printf("Failed to create relative path\n");
      goto fail_uri_alloc;
    }
  }
*/
  hndlr->handler.uri_handler.uri = uri;
  hndlr->handler.uri_handler.method = HTTP_GET;
  hndlr->handler.uri_handler.handler = invocation_wrapper;
  hndlr->handler.uri_handler.user_ctx = &hndlr->handler;

  hndlr->handler.ops = &httpd_static_template_file_handler_ops;

  printf("httpd: Registering static template handler at '%s' for file '%s'\n", uri, path);

  if((err = httpd_register_uri_handler(httpd->server, &hndlr->handler.uri_handler))) {
    goto fail_uri_alloc;
  }

  LIST_APPEND(&hndlr->handler.list, &httpd->handlers);

  if (ret) {
    *ret = hndlr;
  }

  return ESP_OK;

fail_uri_alloc:
  free(uri);
fail_path_alloc:
  free(hndlr->path);
fail_template_alloc:
  template_free_instance(hndlr->templ);
fail_handler_alloc:
  free(hndlr);
  if (path != abspath) {
    free(abspath);
  }
fail:
  return err;
}

static struct httpd_handler *find_handler_by_path(struct httpd* httpd, const char* path) {
  struct list_head *cursor;

  if (!futil_is_path_relative(path)) {
    const char *relpath = futil_relpath(path, httpd->webroot);

    if (relpath) {
      path = relpath;
    }
  }

  LIST_FOR_EACH(cursor, &httpd->handlers) {
    struct httpd_handler* hndlr = LIST_GET_ENTRY(cursor, struct httpd_handler, list);

    if (!strcmp(hndlr->uri_handler.uri, path)) {
      return hndlr;
    }
  }

  return NULL;
}

static esp_err_t add_static_file_template_(struct httpd_static_template_file_handler **ret, struct httpd* httpd, char* path) {
  struct httpd_handler* hndlr = find_handler_by_path(httpd, path);

  if (hndlr) {
    // Fingers crossed the typecast is correct
    struct httpd_static_template_file_handler *tmpl_hndlr =
      container_of(hndlr, struct httpd_static_template_file_handler, handler);

    printf("Matched template handler '%s'\n", path);
    if (ret) {
      *ret = tmpl_hndlr;
    }
    return ESP_OK;
  }

  printf("Creating new template handler for '%s'\n", path);
  return add_static_file_template__(ret, httpd, path);
}

static esp_err_t add_static_file_template(struct httpd* httpd, char* path) {
  return add_static_file_template_(NULL, httpd, path);
}

static esp_err_t template_include_prepare_cb(void* priv, struct templ_slice* slice) {
  esp_err_t err;
  struct templ_slice_arg* include_arg = template_slice_get_option(slice, "file");
  char *path;
  struct httpd* httpd = priv;

  if (!include_arg) {
    err = ESP_ERR_INVALID_ARG;
    return err;
  }
  path = include_arg->value;

  /* First pass, try matching against existing URI handlers */
  slice->priv = find_handler_by_path(httpd, path);
  if (!slice->priv) {
    /* Second pass, try adding handler for the file */
    struct httpd_static_template_file_handler *template_hndlr;

    err = add_static_file_template__(&template_hndlr, httpd, path);
    if (!err) {
      slice->priv = &template_hndlr->handler;
    }
  }

  return ESP_OK;
}

static esp_err_t template_include_cb(void* ctx, void* priv, struct templ_slice* slice) {
  esp_err_t err;
  char* path;
  struct httpd_slice_ctx *slice_ctx = ctx;
  struct httpd* httpd = priv;
  struct templ_slice_arg* include_arg = template_slice_get_option(slice, "file");

  if(!include_arg) {
    printf("Include directive misses required option 'file'\n");
    err = ESP_ERR_INVALID_ARG;
    goto fail;
  }

  path = include_arg->value;
  printf("Including file '%s'\n", path);

  if(slice->priv) {
    struct httpd_handler *hndlr = slice->priv;
    struct httpd_slice_ctx child_slice_ctx = *slice_ctx;

    child_slice_ctx.parent = slice;
    child_slice_ctx.parent_ctx = slice_ctx;
    if (!hndlr->ops || !hndlr->ops->include) {
        err = ESP_ERR_INVALID_ARG;
        goto fail;
    }
    err = hndlr->ops->include(hndlr, &child_slice_ctx);
  } else {
    if(futil_is_path_relative(path)) {
      path = futil_path_concat(path, httpd->webroot);
      if(!path) {
        err = ESP_ERR_NO_MEM;
        goto fail;
      }
    }

    if((err = futil_read_file(slice_ctx->req_ctx, path, request_send_chunk))) {
      goto fail_path_alloc;
    }

fail_path_alloc:
    if(!futil_is_path_relative(include_arg->value)) {
      free(path);
    }
  }

fail:
  return err;
}

char *slice_scope_get_variable(struct httpd_slice_ctx *slice, const char* name) {
  struct templ_slice_arg* variable;

  while (slice && slice->parent) {
    variable = template_slice_get_option(slice->parent, name);
    if (variable) {
      return variable->value;
    }
    slice = slice->parent_ctx;
  }

  return NULL;
}

static esp_err_t template_variable_cb(void* ctx, void* priv, struct templ_slice* slice) {
  struct templ_slice_arg* name_arg = template_slice_get_option(slice, "name");
  struct httpd_slice_ctx *slice_ctx = ctx;
  char *value;

  if(!name_arg) {
    printf("Missing variable name arg\n");
    return ESP_ERR_INVALID_ARG;
  }

  if (!slice_ctx->parent) {
    printf("Can't use variable template without parent!\n");
    return ESP_ERR_INVALID_ARG;
  }

  value = slice_scope_get_variable(slice_ctx, name_arg->value);
  if (value) {
    return httpd_resp_send_chunk(slice_ctx->req_ctx->req, value, strlen(value));
  }

  printf("Requested variable '%s' missing from parents\n", name_arg->value);
  return ESP_ERR_INVALID_ARG;
}

static ssize_t query_string_decode_value(char* value, size_t len) {
  char* search_pos = value, *limit = value + len;

  strntr(value, len, '+', ' ');

  while(search_pos < limit) {
    if (!*search_pos) {
      limit = search_pos;
      break;
    }
    if(*search_pos != '%') {
      search_pos++;
      continue;
    }

    if(limit - search_pos < 2) {
      return -ESP_ERR_INVALID_ARG;
    }

    *search_pos = hex_to_byte(search_pos + 1);
    search_pos++;
    limit -= 2;
    memmove(search_pos, search_pos + 2, limit - search_pos);
  }

  return limit - value;
}

static ssize_t query_string_decode_callback(char** retval, char* str, size_t len) {
  ssize_t proc_len = query_string_decode_value(str, len);

  if(proc_len <= 0) {
    return proc_len;
  }
  str[proc_len] = 0;
  *retval = str;
  return proc_len;
}

struct kv_str_processor_def httpd_param_str_proc = {
  .cb = query_string_decode_callback,
  .flags = { 0 },
};

esp_err_t httpd_alloc(struct httpd** retval, const char* webroot, uint16_t max_num_handlers) {
  httpd_t* httpd = calloc(1, sizeof(struct httpd));
  if (!httpd) {
    return  ESP_ERR_NO_MEM;
  }

  esp_err_t err = httpd_init(httpd, webroot, max_num_handlers);
  if (err) {
    free(httpd);
  } else {
    *retval = httpd;
  }
  return err;
}

esp_err_t httpd_init(struct httpd* httpd, const char* webroot, uint16_t max_num_handlers) {
  esp_err_t err;
  httpd_config_t conf = HTTPD_DEFAULT_CONFIG();
  conf.task_priority = 2;
  conf.max_uri_handlers = max_num_handlers;

  httpd->webroot = strdup(webroot);
  if(!httpd->webroot) {
    err = ESP_ERR_NO_MEM;
    goto fail;
  }

  futil_normalize_path(httpd->webroot);

  INIT_LIST_HEAD(httpd->handlers);

  template_init(&httpd->templates);

  if((err = template_add(&httpd->templates, "include", template_include_cb, template_include_prepare_cb, httpd))) {
    goto fail_webroot_alloc;
  }

  if((err = template_add(&httpd->templates, "variable", template_variable_cb, NULL, httpd))) {
    goto fail_webroot_alloc;
  }

  if((err = kvparser_init_processors(&httpd->uri_kv_parser, "&", "=", &httpd_param_str_proc, &httpd_param_str_proc))) {
    goto fail_templates_alloc;
  }

  if((err = httpd_start(&httpd->server, &conf))) {
    goto fail_kvparser_alloc;
  }

  return ESP_OK;

fail_kvparser_alloc:
  kvparser_free(&httpd->uri_kv_parser);
fail_templates_alloc:
  template_free_templates(&httpd->templates);
fail_webroot_alloc:
  free(httpd->webroot);
fail:
  return err;
}

#define HTTPD_HANDLER_TO_HTTPD_EMBEDDED_STATIC_FILE_HANDLER(hndlr) \
  container_of((hndlr), struct httpd_embedded_static_file_handler, handler)

static esp_err_t httpd_embedded_static_file_include_handler(struct httpd_handler* hndlr, struct httpd_slice_ctx *slice_ctx) {
  struct httpd_embedded_static_file_handler* hndlr_embedded_file = HTTPD_HANDLER_TO_HTTPD_EMBEDDED_STATIC_FILE_HANDLER(hndlr);
  esp_err_t err;

  if(hndlr_embedded_file->flags.gzip) {
    httpd_send_error_msg(slice_ctx->req_ctx, HTTPD_500, "Can not include GZIP compressed data");
    return ESP_ERR_INVALID_ARG;
  }

  if ((err = httpd_resp_send_chunk(slice_ctx->req_ctx->req, hndlr_embedded_file->start, hndlr_embedded_file->end - hndlr_embedded_file->start))) {
    return err;
  }

  return ESP_OK;
}

static esp_err_t httpd_embedded_static_file_request_handler(struct httpd_handler* hndlr, struct httpd_slice_ctx *slice_ctx) {
  struct httpd_embedded_static_file_handler* hndlr_embedded_file = HTTPD_HANDLER_TO_HTTPD_EMBEDDED_STATIC_FILE_HANDLER(hndlr);
  esp_err_t err;

  printf("httpd: Delivering static content for %s from embedded file @%p\n", slice_ctx->req_ctx->req->uri, hndlr_embedded_file->start);

  if(hndlr_embedded_file->flags.gzip) {
    printf("httpd: Content is gzip compressed, setting Content-Encoding header\n");
    if((err = httpd_resp_set_hdr(slice_ctx->req_ctx->req, "Content-Encoding", "gzip"))) {
      return err;
    }
  }

  // This is a static file, set nice long cache lifetime
  if((err = httpd_resp_set_hdr(slice_ctx->req_ctx->req, "Cache-Control", "public, immutable, max-age=31536000"))) {
    return err;
  }

  if((err = httpd_resp_set_type(slice_ctx->req_ctx->req, hndlr_embedded_file->mime_type))) {
    return err;
  }

  if ((err = httpd_resp_send_chunk(slice_ctx->req_ctx->req, hndlr_embedded_file->start, hndlr_embedded_file->end - hndlr_embedded_file->start))) {
    return err;
  }
  httpd_resp_send_chunk(slice_ctx->req_ctx->req, NULL, 0);

  return ESP_OK;
}

struct httpd_handler_ops httpd_embedded_static_file_handler_ops = {
  .include = httpd_embedded_static_file_include_handler,
  .invoke = httpd_embedded_static_file_request_handler,
};

esp_err_t httpd_add_embedded_static_file(struct httpd* httpd, const char* path, const char *mime_type, const void *ptr_start, const void *ptr_end) {
  esp_err_t err;
  char* uri;
  struct httpd_embedded_static_file_handler* hndlr = calloc(1, sizeof(struct httpd_embedded_static_file_handler));
  if(!hndlr) {
    err = ESP_ERR_NO_MEM;
    goto fail;
  }
  hndlr->start = ptr_start;
  hndlr->end = ptr_end;
  // Safe to assume this will always be a const ptr with global
  // lifetime since embedded files are defined at compile time
  hndlr->mime_type = mime_type;

  uri = strdup(path);
  if(!uri) {
    err = ESP_ERR_NO_MEM;
    goto fail_alloc;
  }

  futil_normalize_path(uri);

  if(!magic_buffer_is_gzip(ptr_start)) {
    hndlr->flags.gzip = 1;
  }

  hndlr->handler.uri_handler.uri = uri;
  hndlr->handler.uri_handler.method = HTTP_GET;
  hndlr->handler.uri_handler.handler = invocation_wrapper;
  hndlr->handler.uri_handler.user_ctx = &hndlr->handler;
  hndlr->handler.ops = &httpd_embedded_static_file_handler_ops;

  printf("httpd: Registering embedded static file handler at '%s' for file @%p, gzip: %u\n", uri, ptr_start, hndlr->flags.gzip);

  if((err = httpd_register_uri_handler(httpd->server, &hndlr->handler.uri_handler))) {
    goto fail_uri_alloc;
  }

  LIST_APPEND(&hndlr->handler.list, &httpd->handlers);

  return ESP_OK;

fail_uri_alloc:
  free(uri);
fail_alloc:
  free(hndlr);
fail:
  return err;
}

#define HTTPD_HANDLER_TO_HTTPD_STATIC_FILE_HANDER(hndlr) \
  container_of((hndlr), struct httpd_static_file_handler, handler)

static void httpd_free_static_file_handler(struct httpd_handler* hndlr) {
  struct httpd_static_file_handler* hndlr_static_file = HTTPD_HANDLER_TO_HTTPD_STATIC_FILE_HANDER(hndlr);
  free(hndlr_static_file->path);
  // Allthough uri_handler.uri is declared const we use it with dynamically allocated memory
  free((char*)hndlr->uri_handler.uri);
}

static esp_err_t httpd_static_file_include_handler(struct httpd_handler* hndlr, struct httpd_slice_ctx *slice_ctx) {
  struct httpd_static_file_handler* hndlr_static_file = HTTPD_HANDLER_TO_HTTPD_STATIC_FILE_HANDER(hndlr);
  esp_err_t err;

  if(hndlr_static_file->flags.gzip) {
    httpd_send_error_msg(slice_ctx->req_ctx, HTTPD_500, "Can not include GZIP compressed data");
    return ESP_ERR_INVALID_ARG;
  }

  if((err = futil_read_file(slice_ctx->req_ctx, hndlr_static_file->path, request_send_chunk))) {
    return err;
  }

  return ESP_OK;
}

static esp_err_t httpd_static_file_request_handler(struct httpd_handler* hndlr, struct httpd_slice_ctx *slice_ctx) {
  struct httpd_static_file_handler* hndlr_static_file = HTTPD_HANDLER_TO_HTTPD_STATIC_FILE_HANDER(hndlr);
  esp_err_t err = ESP_OK;
  const char* mime;

  printf("httpd: Delivering static content for %s from %s\n", slice_ctx->req_ctx->req->uri, hndlr_static_file->path);

  if(hndlr_static_file->flags.gzip) {
    printf("httpd: Content is gzip compressed, setting Content-Encoding header\n");
    if((err = httpd_resp_set_hdr(slice_ctx->req_ctx->req, "Content-Encoding", "gzip"))) {
      goto fail;
    }
  }

  if((err = httpd_resp_set_hdr(slice_ctx->req_ctx->req, "Cache-Control", "public, immutable, max-age=31536000"))) {
    goto fail;
  }

  mime = mime_get_type_from_filename(hndlr_static_file->path);
  if(mime) {
    printf("Got mime type: %s\n", mime);
    if((err = httpd_resp_set_type(slice_ctx->req_ctx->req, mime))) {
      goto fail;
    }
  }

  if((err = futil_read_file(slice_ctx->req_ctx, hndlr_static_file->path, request_send_chunk))) {
    goto fail;
  }

  httpd_resp_send_chunk(slice_ctx->req_ctx->req, NULL, 0);

fail:
  return err;
}

struct httpd_handler_ops httpd_static_file_handler_ops = {
  .free = httpd_free_static_file_handler,
  .include = httpd_static_file_include_handler,
  .invoke = httpd_static_file_request_handler,
};

static esp_err_t httpd_add_static_file_default(struct httpd* httpd, char* path) {
  esp_err_t err;
  char* uri;
  struct httpd_static_file_handler* hndlr = calloc(1, sizeof(struct httpd_static_file_handler));
  if(!hndlr) {
    err = ESP_ERR_NO_MEM;
    goto fail;
  }

  hndlr->path = strdup(path);
  if(!hndlr->path) {
    err = ESP_ERR_NO_MEM;
    goto fail_alloc;
  }

  uri = strdup(path);
  if(!uri) {
    err = ESP_ERR_NO_MEM;
    goto fail_path_alloc;
  }

  futil_normalize_path(uri);
  if((err = futil_relpath_inplace(uri, httpd->webroot))) {
    goto fail_uri_alloc;
  }

  if(!magic_file_is_gzip(path)) {
    hndlr->flags.gzip = 1;
  }

  hndlr->handler.uri_handler.uri = uri;
  hndlr->handler.uri_handler.method = HTTP_GET;
  hndlr->handler.uri_handler.handler = invocation_wrapper;
  hndlr->handler.uri_handler.user_ctx = &hndlr->handler;

  hndlr->handler.ops = &httpd_static_file_handler_ops;

  printf("httpd: Registering static file handler at '%s' for file '%s', gzip: %u\n", uri, path, hndlr->flags.gzip);

  if((err = httpd_register_uri_handler(httpd->server, &hndlr->handler.uri_handler))) {
    goto fail_uri_alloc;
  }

  LIST_APPEND(&hndlr->handler.list, &httpd->handlers);

  return ESP_OK;

fail_uri_alloc:
  free(uri);
fail_path_alloc:
  free(hndlr->path);
fail_alloc:
  free(hndlr);
fail:
  return err;
}

static esp_err_t httpd_add_static_file(struct httpd* httpd, char* path) {
  char* fext = futil_get_fext(path);
  if(fext && !strcmp(fext, "thtml")) {
    return add_static_file_template(httpd, path);
  }

  return httpd_add_static_file_default(httpd, path);
}


#define HTTPD_HANDLER_TO_HTTPD_REDIRECT_HANDER(hndlr) \
  container_of((hndlr), struct httpd_redirect_handler, handler)

static void httpd_free_redirect_handler(struct httpd_handler* hndlr) {
  struct httpd_redirect_handler* hndlr_redirect = HTTPD_HANDLER_TO_HTTPD_REDIRECT_HANDER(hndlr);
  free(hndlr_redirect->location);
  // Allthough uri_handler.uri is declared const we use it with dynamically allocated memory
  free((char*)hndlr->uri_handler.uri);
}

struct httpd_handler_ops httpd_redirect_handler_ops = {
  .free = httpd_free_redirect_handler,
};

static esp_err_t redirect_handler(httpd_req_t* req) {
  esp_err_t err;
  struct httpd_redirect_handler* hndlr = req->user_ctx;

  if((err = httpd_resp_set_status(req, HTTPD_302))) {
    goto fail;
  }

  if((err = httpd_resp_set_hdr(req, "Location", hndlr->location))) {
    goto fail;
  }

  printf("httpd: Delivering redirect from %s to %s\n", req->uri, hndlr->location);

  httpd_resp_send_chunk(req, NULL, 0);

fail:
  return err;
}

esp_err_t httpd_add_redirect(struct httpd* httpd, const char* from, const char* to) {
  esp_err_t err;
  char* uri;
  struct httpd_redirect_handler* hndlr = calloc(1, sizeof(struct httpd_redirect_handler));
  if(!hndlr) {
    err = ESP_ERR_NO_MEM;
    goto fail;
  }

  hndlr->location = strdup(to);
  if(!hndlr->location) {
    err = ESP_ERR_NO_MEM;
    goto fail_alloc;
  }

  uri = strdup(from);
  if(!uri) {
    err = ESP_ERR_NO_MEM;
    goto fail_path_alloc;
  }

  hndlr->handler.uri_handler.uri = uri;
  hndlr->handler.uri_handler.method = HTTP_GET;
  hndlr->handler.uri_handler.handler = redirect_handler;
  hndlr->handler.uri_handler.user_ctx = hndlr;

  hndlr->handler.ops = &httpd_redirect_handler_ops;

  printf("httpd: Registering redirect handler at '%s' to location '%s'\n", from, to);

  if((err = httpd_register_uri_handler(httpd->server, &hndlr->handler.uri_handler))) {
    goto fail_uri_alloc;
  }

  LIST_APPEND(&hndlr->handler.list, &httpd->handlers);

  return ESP_OK;

fail_uri_alloc:
  free(uri);
fail_path_alloc:
  free(hndlr->location);
fail_alloc:
  free(hndlr);
fail:
  return err;
}

static esp_err_t httpd_add_static_directory(struct httpd* httpd, char* path) {
  esp_err_t err;
  struct dirent* cursor;
  DIR* dir = opendir(path);
  if(!dir) {
    return xlate_err(errno);
  }
  DIRENT_FOR_EACH(cursor, dir) {
    if(!cursor) {
      err = xlate_err(errno);
    }
    if((err = __httpd_add_static_path(httpd, path, cursor->d_name))) {
      goto fail;
    }
  }
  closedir(dir);
  return ESP_OK;
fail:
  closedir(dir);
  return err;
}

esp_err_t __httpd_add_static_path(struct httpd* httpd, const char* dir, char* name) {
  int err;
  struct stat pathinfo;
  char* path = name;
  if(dir) {
    // $dir/$name\0
    size_t len = strlen(dir) + 1 + strlen(name) + 1;
    path = calloc(1, len);
    if(!path) {
      err = ESP_ERR_NO_MEM;
      goto fail;
    }
    strcat(path, dir);
    strcat(path, "/");
    strcat(path, name);
  }
  printf("Stating '%s'\n", path);
  if(stat(path, &pathinfo)) {
    printf("Stat failed: %s(%d)\n", strerror(errno), errno);
    err = xlate_err(errno);
    goto fail_alloc;
  }
  if(pathinfo.st_mode & S_IFDIR) {
    err = httpd_add_static_directory(httpd, path);
  } else if(pathinfo.st_mode & S_IFREG) {
    err = httpd_add_static_file(httpd, path);
  } else {
    err = ESP_ERR_INVALID_ARG;
  }

  if(err) {
    struct list_head* cursor, *next;
    LIST_FOR_EACH_SAFE(cursor, next, &httpd->handlers) {
      struct httpd_handler* hndlr = LIST_GET_ENTRY(cursor, struct httpd_handler, list);
      if(hndlr->ops && hndlr->ops->free) {
        hndlr->ops->free(hndlr);
      }
      LIST_DELETE(cursor);
    }
  }

fail_alloc:
  if(dir) {
    free(path);
  }
fail:
  return err;
}


#define HTTPD_HANDLER_TO_HTTPD_REQUEST_HANDER(hndlr) \
  container_of((hndlr), struct httpd_request_handler, handler)

static void httpd_free_request_handler(struct httpd_handler* hndlr) {
  struct httpd_request_handler* hndlr_request = HTTPD_HANDLER_TO_HTTPD_REQUEST_HANDER(hndlr);
  char** key = hndlr_request->required_keys;
  while(*key) {
    free(*key++);
  }
  free(hndlr_request->required_keys);
  // Allthough uri_handler.uri is declared const we use it with dynamically allocated memory
  free((char*)hndlr->uri_handler.uri);
}

struct httpd_handler_ops httpd_request_handler_ops = {
  .free = httpd_free_request_handler,
};

#define REQUEST_MALFORMED_PARAM "Invalid request, malformed query parameters"
#define REQUEST_MISSING_PARAM "Invalid request, missing parameters"

/*
static esp_err_t httpd_parse_form_data(struct httpd_request_ctx* ctx) {

}
*/

static esp_err_t httpd_request_handler(httpd_req_t* req) {
  esp_err_t err;
  size_t query_len;
  struct httpd_request_handler* hndlr = req->user_ctx;
  char* query_string;
  char** required_params = hndlr->required_keys;
  struct httpd_request_ctx ctx;

  httpd_request_ctx_init(&ctx, req);

  ctx.req = req;

  query_len = httpd_req_get_url_query_len(req) + 1;
  query_string = calloc(1, query_len);
  if(!query_string) {
    err = ESP_ERR_NO_MEM;
    goto fail;
  }
  httpd_req_get_url_query_str(req, query_string, query_len);

  if((err = kvparser_parse_string(&hndlr->httpd->uri_kv_parser, &ctx.query_params, query_string, query_len))) {
    httpd_send_error_msg(&ctx, HTTPD_400, REQUEST_MALFORMED_PARAM);
    goto fail_query_string_alloc;
  }

  while(*required_params) {
    if(!kvparser_find_pair(&ctx.query_params, *required_params)) {
      err = httpd_send_error_msg(&ctx, HTTPD_400, REQUEST_MISSING_PARAM);
      goto fail_query_params_alloc;
    }
    required_params++;
  }

  err = hndlr->cb(&ctx, hndlr->priv);

fail_query_params_alloc:
  {
    kvlist* cursor, *next;
    LIST_FOR_EACH_SAFE(cursor, next, &ctx.query_params) {
      struct kvpair* pair = LIST_GET_ENTRY(cursor, struct kvpair, list);
      kvparser_free_kvpair(&hndlr->httpd->uri_kv_parser, pair);
    }
  }
fail_query_string_alloc:
  free(query_string);
fail:
  return err;
}

esp_err_t httpd_add_handler(struct httpd* httpd, httpd_method_t method, const char* path, httpd_request_cb cb, void* priv, bool websocket, size_t num_param, ...) {
  esp_err_t err;
  char* uri;
  va_list aq;

  struct httpd_request_handler* hndlr = calloc(1, sizeof(struct httpd_request_handler));
  if(!hndlr) {
    err = ESP_ERR_NO_MEM;
    goto fail;
  }

  hndlr->httpd = httpd;
  hndlr->cb = cb;
  hndlr->priv = priv;

  uri = strdup(path);
  if(!uri) {
    err = ESP_ERR_NO_MEM;
    goto fail_hndlr_alloc;
  }

  hndlr->handler.uri_handler.handler = httpd_request_handler;
  hndlr->handler.uri_handler.user_ctx = hndlr;
  hndlr->handler.uri_handler.method = method;
  hndlr->handler.uri_handler.uri = uri;
#ifdef CONFIG_HTTPD_WS_SUPPORT
  hndlr->handler.uri_handler.is_websocket = websocket;
#endif

  hndlr->required_keys = calloc(num_param + 1, sizeof(char*));
  if(!hndlr->required_keys) {
    err = ESP_ERR_NO_MEM;
    goto fail_uri_alloc;
  }

  va_start(aq, num_param);
  if(num_param) {
    char** key_copy = hndlr->required_keys;
    while(num_param--) {
      char* param = va_arg(aq, char*);
      *key_copy = strdup(param);
      if(!*key_copy) {
        err = ESP_ERR_NO_MEM;
        goto fail_keys_alloc;
      }
      key_copy++;
    }
  }

  if((err = httpd_register_uri_handler(httpd->server, &hndlr->handler.uri_handler))) {
    goto fail_keys_alloc;
  }

  va_end(aq);

  return ESP_OK;


fail_keys_alloc:
  va_end(aq);
  {
    char** key = hndlr->required_keys;
    while(*key) {
      free(*key++);
    }
    free(hndlr->required_keys);
  }
fail_uri_alloc:
  free(uri);
fail_hndlr_alloc:
  free(hndlr);
fail:
  return err;
}

ssize_t httpd_query_string_get_param(struct httpd_request_ctx* ctx, const char* param, char** value) {
  struct kvpair* pair = kvparser_find_pair(&ctx->query_params, param);
  if(!pair) {
    return -ESP_ERR_NOT_FOUND;
  }
  *value = pair->value;
  return pair->value_len;
}
