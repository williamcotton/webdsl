#ifndef WEBSITE_JSON_H
#define WEBSITE_JSON_H

#include "ast.h"
#include "arena.h"

// Convert a website node to a JSON string
char* websiteToJson(Arena *arena, const WebsiteNode* website);

#endif // WEBSITE_JSON_H
