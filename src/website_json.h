#ifndef WEBSITE_JSON_H
#define WEBSITE_JSON_H

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
#include <jansson.h>
#pragma clang diagnostic pop
#include "website.h"

// Convert a WebsiteNode structure to a JSON representation
json_t* websiteToJson(const WebsiteNode* website);

#endif // WEBSITE_JSON_H
