
#include "stdio.h"
#include "upload_image_template.h"
#include "../lib/template_helpers.h"

void get_upload_image_template(void *t_data, template_callback_context_t *cb_context,
template_callback_t cb) {
upload_image_template_context_t *data = (upload_image_template_context_t*) t_data;

cb(cb_context, "\r\n<!DOCTYPE html>\r\n<html lang=\"en\">\r\n\r\n<head>\r\n    <meta charset=\"UTF-8\">\r\n    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\r\n    <title>Document</title>\r\n</head>\r\n\r\n<body>\r\n\r\n</body>\r\n\r\n</html>\r\n\r\n");

}

