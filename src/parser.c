#include "parser.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

// Forward declarations
static void advanceParser(Parser *parser);
static void consume(Parser *parser, TokenType type, const char *errorMsg);
static PageNode *parsePages(Parser *parser);
static PageNode *parsePage(Parser *parser);
static ContentNode *parseContent(Parser *parser);
static StyleBlockNode *parseStyles(Parser *parser);
static StyleBlockNode *parseStyleBlock(Parser *parser);
static StylePropNode *parseStyleProps(Parser *parser);
static LayoutNode *parseLayouts(Parser *parser);
static LayoutNode *parseLayout(Parser *parser);
static char *copyString(Parser *parser, const char *source);
static bool parsePort(const char* str, int* result);
static ApiEndpoint *parseApi(Parser *parser);
static QueryNode *parseQuery(Parser *parser);
static ApiField *parseApiFields(Parser *parser);
static QueryParam *parseQueryParams(Parser *parser);

void initParser(Parser *parser, const char *source) {
    initLexer(&parser->lexer, source, parser);
    parser->current.type = TOKEN_UNKNOWN;
    parser->previous.type = TOKEN_UNKNOWN;
    parser->hadError = 0;
    parser->arena = createArena(1024 * 1024); // 1MB arena
}

static void advanceParser(Parser *parser) {
    parser->previous = parser->current;
    parser->current = getNextToken(&parser->lexer);
}

static void consume(Parser *parser, TokenType type, const char *errorMsg) {
    if (parser->current.type == type) {
        advanceParser(parser);
        return;
    }
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "Parse error at line %d: %s (got \"%s\")\n",
            parser->current.line, errorMsg, parser->current.lexeme);
    fputs(buffer, stderr);
    parser->hadError = 1;
}

static char *copyString(Parser *parser, const char *source) {
    return arenaDupString(parser->arena, source);
}

static LayoutNode *parseLayout(Parser *parser) {
    LayoutNode *layout = arenaAlloc(parser->arena, sizeof(LayoutNode));
    memset(layout, 0, sizeof(LayoutNode));

    consume(parser, TOKEN_STRING, "Expected string for layout identifier.");
    layout->identifier = copyString(parser, parser->previous.lexeme);

    consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after layout identifier.");

    while (parser->current.type != TOKEN_CLOSE_BRACE &&
           parser->current.type != TOKEN_EOF && !parser->hadError) {
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wswitch-enum"
        switch (parser->current.type) {
            case TOKEN_HTML: {
                advanceParser(parser);  // consume HTML token
                
                if (parser->current.type == TOKEN_RAW_BLOCK || parser->current.type == TOKEN_STRING) {
                    layout->bodyContent = arenaAlloc(parser->arena, sizeof(ContentNode));
                    memset(layout->bodyContent, 0, sizeof(ContentNode));
                    layout->bodyContent->type = "raw_html";
                    layout->bodyContent->arg1 = copyString(parser, parser->current.lexeme);
                    advanceParser(parser);  // consume raw block or string
                } else {
                    char buffer[256];
                    snprintf(buffer, sizeof(buffer),
                            "Expected HTML block at line %d\n",
                            parser->current.line);
                    fputs(buffer, stderr);
                    parser->hadError = 1;
                }
                break;
            }
            case TOKEN_CONTENT: {
                advanceParser(parser);
                consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'content'.");
                layout->bodyContent = parseContent(parser);
                break;
            }
            default: {
                char buffer[256];
                snprintf(buffer, sizeof(buffer),
                        "Parse error at line %d: Unexpected token in layout.\n",
                        parser->current.line);
                fputs(buffer, stderr);
                parser->hadError = 1;
                break;
            }
        }
        #pragma clang diagnostic pop
    }

    consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' after layout block.");
    return layout;
}

static LayoutNode *parseLayouts(Parser *parser) {
    LayoutNode *head = NULL;
    LayoutNode *tail = NULL;

    while (parser->current.type == TOKEN_STRING && !parser->hadError) {
        LayoutNode *layout = parseLayout(parser);
        if (!head) {
            head = layout;
            tail = layout;
        } else {
            tail->next = layout;
            tail = layout;
        }
    }

    consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' at end of layouts block.");
    return head;
}

static PageNode *parsePage(Parser *parser) {
    PageNode *page = arenaAlloc(parser->arena, sizeof(PageNode));
    memset(page, 0, sizeof(PageNode));
    
    advanceParser(parser); // consume 'page'
    consume(parser, TOKEN_STRING, "Expected string for page identifier.");
    page->identifier = copyString(parser, parser->previous.lexeme);

    consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after page identifier.");

    while (parser->current.type != TOKEN_CLOSE_BRACE &&
           parser->current.type != TOKEN_EOF && !parser->hadError) {
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wswitch-enum"
        switch (parser->current.type) {
            case TOKEN_ROUTE: {
                advanceParser(parser);
                consume(parser, TOKEN_STRING, "Expected string after 'route'.");
                page->route = copyString(parser, parser->previous.lexeme);
                break;
            }
            case TOKEN_LAYOUT: {
                advanceParser(parser);
                consume(parser, TOKEN_STRING, "Expected string after 'layout'.");
                page->layout = copyString(parser, parser->previous.lexeme);
                break;
            }
            case TOKEN_HTML: {
                advanceParser(parser);  // consume HTML token
                
                if (parser->current.type == TOKEN_RAW_BLOCK || parser->current.type == TOKEN_STRING) {
                    page->contentHead = arenaAlloc(parser->arena, sizeof(ContentNode));
                    memset(page->contentHead, 0, sizeof(ContentNode));
                    page->contentHead->type = "raw_html";
                    page->contentHead->arg1 = copyString(parser, parser->current.lexeme);
                    advanceParser(parser);  // consume raw block or string
                } else {
                    char buffer[256];
                    snprintf(buffer, sizeof(buffer),
                            "Expected HTML block at line %d\n",
                            parser->current.line);
                    fputs(buffer, stderr);
                    parser->hadError = 1;
                }
                break;
            }
            case TOKEN_CONTENT: {
                advanceParser(parser);
                consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'content'.");
                page->contentHead = parseContent(parser);
                break;
            }
            default: {
                char buffer[256];
                snprintf(buffer, sizeof(buffer),
                        "Parse error at line %d: Unexpected token '%s' in page.\n",
                        parser->current.line, parser->current.lexeme);
                fputs(buffer, stderr);
                parser->hadError = 1;
                break;
            }
        }
        #pragma clang diagnostic pop
    }

    consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' after page block.");
    return page;
}

static PageNode *parsePages(Parser *parser) {
    PageNode *head = NULL;
    PageNode *tail = NULL;

    while (parser->current.type == TOKEN_PAGE && !parser->hadError) {
        PageNode *page = parsePage(parser);
        if (!head) {
            head = page;
            tail = page;
        } else {
            tail->next = page;
            tail = page;
        }
    }

    consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' at end of pages block.");
    return head;
}

static ContentNode *parseContent(Parser *parser) {
    ContentNode *head = NULL;
    ContentNode *tail = NULL;

    while (parser->current.type != TOKEN_CLOSE_BRACE &&
           parser->current.type != TOKEN_EOF && !parser->hadError) {
        
        // Handle raw HTML blocks
        if (parser->current.type == TOKEN_HTML) {
            advanceParser(parser);  // consume HTML token
            
            if (parser->current.type == TOKEN_RAW_BLOCK || parser->current.type == TOKEN_STRING) {
                ContentNode *node = arenaAlloc(parser->arena, sizeof(ContentNode));
                memset(node, 0, sizeof(ContentNode));
                node->type = "raw_html";
                node->arg1 = copyString(parser, parser->current.lexeme);
                advanceParser(parser);  // consume raw block or string
                
                if (!head) {
                    head = node;
                    tail = node;
                } else {
                    tail->next = node;
                    tail = node;
                }
                continue;
            } else {
                char buffer[256];
                snprintf(buffer, sizeof(buffer),
                        "Expected HTML block at line %d\n",
                        parser->current.line);
                fputs(buffer, stderr);
                parser->hadError = 1;
            }
        }

        ContentNode *node = arenaAlloc(parser->arena, sizeof(ContentNode));
        memset(node, 0, sizeof(ContentNode));

        if (parser->current.type == TOKEN_STRING) {
            const char *value = parser->current.lexeme;
            node->type = copyString(parser, value);
            advanceParser(parser);

            // Check if this is the special "content" placeholder
            if (strcmp(node->type, "content") == 0) {
                // Already the right type
            } else if (parser->current.type == TOKEN_OPEN_BRACE) {
                advanceParser(parser);
                node->children = parseContent(parser);
            } else {
                consume(parser, TOKEN_STRING, "Expected string after tag");
                node->arg1 = copyString(parser, parser->previous.lexeme);

                if (strcmp(node->type, "link") == 0) {
                    consume(parser, TOKEN_STRING, "Expected link text after URL string");
                    node->arg2 = copyString(parser, parser->previous.lexeme);
                }
                else if (strcmp(node->type, "image") == 0 && 
                         parser->current.type == TOKEN_ALT) {
                    advanceParser(parser);
                    consume(parser, TOKEN_STRING, "Expected alt text after 'alt'");
                    node->arg2 = copyString(parser, parser->previous.lexeme);
                }
            }

            if (!head) {
                head = node;
                tail = node;
            } else {
                tail->next = node;
                tail = node;
            }
        } else {
            break;
        }
    }

    consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' at end of content block");
    return head;
}

static StyleBlockNode *parseStyleBlock(Parser *parser) {
    StyleBlockNode *block = arenaAlloc(parser->arena, sizeof(StyleBlockNode));
    memset(block, 0, sizeof(StyleBlockNode));
    
    if (parser->current.type != TOKEN_STRING) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer),
                "Expected style selector at line %d\n",
                parser->current.line);
        fputs(buffer, stderr);
        parser->hadError = 1;
        return NULL;
    }
    
    block->selector = copyString(parser, parser->current.lexeme);
    advanceParser(parser);
    
    consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after style selector.");

    // Check if this is a raw CSS block
    if (parser->current.type == TOKEN_CSS || parser->current.type == TOKEN_RAW_BLOCK) {
        advanceParser(parser);
        
        // Create a single property node to hold the raw CSS
        StylePropNode *prop = arenaAlloc(parser->arena, sizeof(StylePropNode));
        memset(prop, 0, sizeof(StylePropNode));
        prop->property = "raw_css";
        prop->value = copyString(parser, parser->previous.lexeme);
        block->propHead = prop;
        
        // If it was a RAW_BLOCK, we don't need to consume a closing brace
        if (parser->current.type == TOKEN_OPEN_BRACE) {
            consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'css'.");
            consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' after CSS block.");
        }
        
        consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' after style block.");
        return block;
    }
    
    // Regular property parsing
    StylePropNode *propHead = parseStyleProps(parser);
    block->propHead = propHead;
    
    return block;
}

static StylePropNode *parseStyleProps(Parser *parser) {
    StylePropNode *head = NULL;
    StylePropNode *tail = NULL;

    while (parser->current.type != TOKEN_CLOSE_BRACE &&
           parser->current.type != TOKEN_EOF && !parser->hadError) {
        if (parser->current.type == TOKEN_STRING) {
            StylePropNode *prop = arenaAlloc(parser->arena, sizeof(StylePropNode));
            memset(prop, 0, sizeof(StylePropNode));
            
            prop->property = copyString(parser, parser->current.lexeme);
            advanceParser(parser);

            if (parser->current.type == TOKEN_STRING) {
                prop->value = copyString(parser, parser->current.lexeme);
                advanceParser(parser);
            } else {
                fputs("Expected string value after style property.\n", stderr);
                parser->hadError = 1;
                break;
            }

            if (!head) {
                head = prop;
                tail = prop;
            } else {
                tail->next = prop;
                tail = prop;
            }
        } else {
            break;
        }
    }

    consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' at end of style block.");
    return head;
}

static StyleBlockNode *parseStyles(Parser *parser) {
    StyleBlockNode *head = NULL;
    StyleBlockNode *tail = NULL;

    while (!parser->hadError) {
        if (parser->current.type == TOKEN_CLOSE_BRACE) {
            advanceParser(parser);  // Consume the closing brace
            break;
        } else if (parser->current.type == TOKEN_CSS || parser->current.type == TOKEN_RAW_BLOCK) {
            StyleBlockNode *block = arenaAlloc(parser->arena, sizeof(StyleBlockNode));
            memset(block, 0, sizeof(StyleBlockNode));
            
            // Create a single property node to hold the raw CSS
            StylePropNode *prop = arenaAlloc(parser->arena, sizeof(StylePropNode));
            memset(prop, 0, sizeof(StylePropNode));
            prop->property = "raw_css";
            
            if (parser->current.type == TOKEN_CSS) {
                advanceParser(parser);  // consume 'css'
                consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'css'");
            }
            
            prop->value = copyString(parser, parser->current.lexeme);
            block->propHead = prop;
            
            advanceParser(parser);  // consume the CSS content
            
            if (!head) {
                head = block;
                tail = block;
            } else {
                tail->next = block;
                tail = block;
            }
        } else if (parser->current.type == TOKEN_STRING) {
            StyleBlockNode *block = parseStyleBlock(parser);
            if (block) {
                if (!head) {
                    head = block;
                    tail = block;
                } else {
                    tail->next = block;
                    tail = block;
                }
            }
        } else if (parser->current.type == TOKEN_EOF) {
            char buffer[256];
            snprintf(buffer, sizeof(buffer), 
                    "Unexpected end of file in styles block at line %d\n",
                    parser->current.line);
            fputs(buffer, stderr);
            parser->hadError = 1;
            break;
        } else {
            char buffer[256];
            snprintf(buffer, sizeof(buffer), 
                    "Expected style selector or '}' at line %d\n",
                    parser->current.line);
            fputs(buffer, stderr);
            parser->hadError = 1;
            break;
        }
    }

    return head;
}

static bool parsePort(const char* str, int* result) {
    char* endptr;
    errno = 0;  // Reset errno before the call
    long val = strtol(str, &endptr, 10);
    
    // Check for conversion errors
    if (errno == ERANGE) return false;  // Overflow/underflow
    if (endptr == str) return false;    // No conversion performed
    if (*endptr != '\0') return false;  // Not all characters consumed
    
    // Check if value is in valid port range (1-65535)
    if (val < 1 || val > 65535) return false;
    
    *result = (int)val;
    return true;
}

static ApiEndpoint *parseApi(Parser *parser) {
    ApiEndpoint *endpoint = arenaAlloc(parser->arena, sizeof(ApiEndpoint));
    memset(endpoint, 0, sizeof(ApiEndpoint));

    consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'api'.");

    while (parser->current.type != TOKEN_CLOSE_BRACE &&
           parser->current.type != TOKEN_EOF && !parser->hadError) {

        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wswitch-enum"
        switch (parser->current.type) {
            case TOKEN_ROUTE: {
                advanceParser(parser);
                consume(parser, TOKEN_STRING, "Expected string after 'route'.");
                endpoint->route = copyString(parser, parser->previous.lexeme);
                break;
            }
            case TOKEN_METHOD: {
                advanceParser(parser);
                consume(parser, TOKEN_STRING, "Expected string after 'method'.");
                endpoint->method = copyString(parser, parser->previous.lexeme);
                break;
            }
            case TOKEN_JSON_RESPONSE: {
                advanceParser(parser);
                consume(parser, TOKEN_STRING, "Expected string after 'jsonResponse'.");
                endpoint->jsonResponse = copyString(parser, parser->previous.lexeme);
                
                // Check for response fields
                if (parser->current.type == TOKEN_OPEN_BRACKET) {
                    advanceParser(parser);  // Consume [

                    ResponseField *head = NULL;
                    ResponseField *tail = NULL;

                    while (parser->current.type != TOKEN_CLOSE_BRACKET && 
                           parser->current.type != TOKEN_EOF && 
                           !parser->hadError) {

                        if (parser->current.type == TOKEN_STRING) {
                            ResponseField *field = arenaAlloc(parser->arena, sizeof(ResponseField));
                            memset(field, 0, sizeof(ResponseField));
                            field->name = copyString(parser, parser->current.lexeme);

                            if (!head) {
                                head = field;
                                tail = field;
                            } else {
                                tail->next = field;
                                tail = field;
                            }

                            advanceParser(parser);

                            if (parser->current.type == TOKEN_COMMA) {
                                advanceParser(parser);
                            } else if (parser->current.type != TOKEN_CLOSE_BRACKET) {
                                parser->hadError = 1;
                                char buffer[256];
                                snprintf(buffer, sizeof(buffer),
                                        "Expected ',' or ']' after field name at line %d\n",
                                        parser->current.line);
                                fputs(buffer, stderr);
                                break;
                            }
                        } else {
                            parser->hadError = 1;
                            char buffer[256];
                            snprintf(buffer, sizeof(buffer),
                                    "Expected field name at line %d\n",
                                    parser->current.line);
                            fputs(buffer, stderr);
                            break;
                        }
                    }
                    
                    consume(parser, TOKEN_CLOSE_BRACKET, "Expected ']' after response fields.");
                    endpoint->fields = head;
                }
                break;
            }
            case TOKEN_PRE_FILTER: {
                advanceParser(parser);  // consume preFilter token
                
                if (parser->current.type == TOKEN_JQ) {
                    advanceParser(parser);
                    if (parser->current.type == TOKEN_RAW_BLOCK) {
                        endpoint->preJqFilter = copyString(parser, parser->current.lexeme);
                        advanceParser(parser);
                    } else {
                        char buffer[256];
                        snprintf(buffer, sizeof(buffer),
                                "Expected JQ filter block at line %d\n",
                                parser->current.line);
                        fputs(buffer, stderr);
                        parser->hadError = 1;
                    }
                } else if (parser->current.type == TOKEN_LUA) {
                    advanceParser(parser);
                    if (parser->current.type == TOKEN_RAW_BLOCK) {
                        endpoint->preLuaFilter = copyString(parser, parser->current.lexeme);
                        advanceParser(parser);
                    } else {
                        char buffer[256];
                        snprintf(buffer, sizeof(buffer),
                                "Expected Lua filter block at line %d\n",
                                parser->current.line);
                        fputs(buffer, stderr);
                        parser->hadError = 1;
                    }
                } else {
                    char buffer[256];
                    snprintf(buffer, sizeof(buffer),
                            "Expected 'jq' or 'lua' after 'preFilter' at line %d\n",
                            parser->current.line);
                    fputs(buffer, stderr);
                    parser->hadError = 1;
                }
                break;
            }
            case TOKEN_FILTER: {
                advanceParser(parser);  // consume filter token
                
                if (parser->current.type == TOKEN_JQ) {
                    advanceParser(parser);
                    if (parser->current.type == TOKEN_RAW_BLOCK) {
                        endpoint->jqFilter = copyString(parser, parser->current.lexeme);
                        advanceParser(parser);
                    } else {
                        char buffer[256];
                        snprintf(buffer, sizeof(buffer),
                                "Expected JQ filter block at line %d\n",
                                parser->current.line);
                        fputs(buffer, stderr);
                        parser->hadError = 1;
                    }
                } else if (parser->current.type == TOKEN_LUA) {
                    advanceParser(parser);
                    if (parser->current.type == TOKEN_RAW_BLOCK) {
                        endpoint->luaFilter = copyString(parser, parser->current.lexeme);
                        advanceParser(parser);
                    } else {
                        char buffer[256];
                        snprintf(buffer, sizeof(buffer),
                                "Expected Lua filter block at line %d\n",
                                parser->current.line);
                        fputs(buffer, stderr);
                        parser->hadError = 1;
                    }
                } else {
                    char buffer[256];
                    snprintf(buffer, sizeof(buffer),
                            "Expected 'jq' or 'lua' after 'filter' at line %d\n",
                            parser->current.line);
                    fputs(buffer, stderr);
                    parser->hadError = 1;
                }
                break;
            }
            case TOKEN_JQ: {
                // Maintain backward compatibility with existing jq syntax
                advanceParser(parser);  // consume JQ token
                
                if (parser->current.type == TOKEN_RAW_BLOCK) {
                    endpoint->jqFilter = copyString(parser, parser->current.lexeme);
                    advanceParser(parser);
                } else {
                    char buffer[256];
                    snprintf(buffer, sizeof(buffer),
                            "Expected JQ filter block at line %d\n",
                            parser->current.line);
                    fputs(buffer, stderr);
                    parser->hadError = 1;
                }
                break;
            }
            case TOKEN_FIELDS: {
                advanceParser(parser);
                consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'fields'.");
                endpoint->apiFields = parseApiFields(parser);
                break;
            }
            default: {
                char buffer[256];
                snprintf(buffer, sizeof(buffer),
                        "Unexpected token in API block at line %d: %s\n",
                        parser->current.line, parser->current.lexeme);
                fputs(buffer, stderr);
                parser->hadError = 1;
                break;
            }
        }
        #pragma clang diagnostic pop
    }

    consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' after API block.");

    return endpoint;
}

static ApiField *parseApiFields(Parser *parser) {
    ApiField *head = NULL;
    ApiField *tail = NULL;

    while (parser->current.type == TOKEN_STRING && !parser->hadError) {
        ApiField *field = arenaAlloc(parser->arena, sizeof(ApiField));
        memset(field, 0, sizeof(ApiField));

        // Parse field name
        field->name = copyString(parser, parser->current.lexeme);
        advanceParser(parser);

        consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after field name.");

        // Parse field properties
        while (parser->current.type != TOKEN_CLOSE_BRACE && 
               parser->current.type != TOKEN_EOF && 
               !parser->hadError) {
            
            if (parser->current.type == TOKEN_STRING) {
                const char *prop = parser->current.lexeme;
                advanceParser(parser);

                if (strcmp(prop, "type") == 0) {
                    consume(parser, TOKEN_STRING, "Expected string after 'type'.");
                    field->type = copyString(parser, parser->previous.lexeme);
                }
                else if (strcmp(prop, "required") == 0) {
                    consume(parser, TOKEN_STRING, "Expected boolean after 'required'.");
                    field->required = strcmp(parser->previous.lexeme, "true") == 0;
                }
                else if (strcmp(prop, "format") == 0) {
                    consume(parser, TOKEN_STRING, "Expected string after 'format'.");
                    field->format = copyString(parser, parser->previous.lexeme);
                }
                else if (strcmp(prop, "length") == 0) {
                    consume(parser, TOKEN_RANGE, "Expected range after 'length'.");
                    const char *range = parser->previous.lexeme;
                    char *dotdot = strstr(range, "..");
                    if (dotdot) {
                        *dotdot = '\0';
                        field->minLength = atoi(range);
                        field->maxLength = atoi(dotdot + 2);
                    }
                }
            } else {
                char buffer[256];
                snprintf(buffer, sizeof(buffer),
                        "Unexpected token in field definition at line %d\n",
                        parser->current.line);
                fputs(buffer, stderr);
                parser->hadError = 1;
                break;
            }
        }

        consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' after field properties.");

        // Add to linked list
        if (!head) {
            head = field;
            tail = field;
        } else {
            tail->next = field;
            tail = field;
        }
    }

    consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' after fields block.");
    return head;
}

static QueryParam *parseQueryParams(Parser *parser) {
    QueryParam *head = NULL;
    QueryParam *tail = NULL;

    consume(parser, TOKEN_OPEN_BRACKET, "Expected '[' after 'params'");

    while (parser->current.type != TOKEN_CLOSE_BRACKET && 
           parser->current.type != TOKEN_EOF && 
           !parser->hadError) {

        if (parser->current.type == TOKEN_STRING) {
            QueryParam *param = arenaAlloc(parser->arena, sizeof(QueryParam));
            memset(param, 0, sizeof(QueryParam));
            param->name = copyString(parser, parser->current.lexeme);

            if (!head) {
                head = param;
                tail = param;
            } else {
                tail->next = param;
                tail = param;
            }

            advanceParser(parser);

            if (parser->current.type == TOKEN_COMMA) {
                advanceParser(parser);
            } else if (parser->current.type != TOKEN_CLOSE_BRACKET) {
                parser->hadError = 1;
                char buffer[256];
                snprintf(buffer, sizeof(buffer),
                        "Expected ',' or ']' after parameter name at line %d\n",
                        parser->current.line);
                fputs(buffer, stderr);
                break;
            }
        } else {
            parser->hadError = 1;
            char buffer[256];
            snprintf(buffer, sizeof(buffer),
                    "Expected parameter name at line %d\n",
                    parser->current.line);
            fputs(buffer, stderr);
            break;
        }
    }

    consume(parser, TOKEN_CLOSE_BRACKET, "Expected ']' after parameter list");
    return head;
}

static QueryNode *parseQuery(Parser *parser) {
    QueryNode *query = arenaAlloc(parser->arena, sizeof(QueryNode));
    memset(query, 0, sizeof(QueryNode));
    
    consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'query'.");
    
    while (parser->current.type != TOKEN_CLOSE_BRACE &&
           parser->current.type != TOKEN_EOF && !parser->hadError) {
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wswitch-enum"
        switch (parser->current.type) {
            case TOKEN_NAME: {
                advanceParser(parser);
                consume(parser, TOKEN_STRING, "Expected string after 'name'.");
                query->name = copyString(parser, parser->previous.lexeme);
                break;
            }
            case TOKEN_PARAMS: {
                advanceParser(parser);
                query->params = parseQueryParams(parser);
                break;
            }
            case TOKEN_SQL: {
                advanceParser(parser);
                if (parser->current.type == TOKEN_RAW_BLOCK || 
                    parser->current.type == TOKEN_STRING) {
                    query->sql = copyString(parser, parser->current.lexeme);
                    advanceParser(parser);
                } else {
                    char buffer[256];
                    snprintf(buffer, sizeof(buffer),
                            "Expected SQL query at line %d\n",
                            parser->current.line);
                    fputs(buffer, stderr);
                    parser->hadError = 1;
                }
                break;
            }
            default: {
                char buffer[256];
                snprintf(buffer, sizeof(buffer),
                        "Parse error at line %d: Unexpected token in query.\n",
                        parser->current.line);
                fputs(buffer, stderr);
                parser->hadError = 1;
                break;
            }
        }
        #pragma clang diagnostic pop
    }

    consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' after query block.");
    return query;
}

WebsiteNode *parseProgram(Parser *parser) {
    WebsiteNode *website = arenaAlloc(parser->arena, sizeof(WebsiteNode));
    memset(website, 0, sizeof(WebsiteNode));
    advanceParser(parser); // read the first token

    consume(parser, TOKEN_WEBSITE, "Expected 'website' at start.");
    consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'website'.");

    while (parser->current.type != TOKEN_CLOSE_BRACE &&
           parser->current.type != TOKEN_EOF && !parser->hadError) {
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wswitch-enum"
        switch (parser->current.type) {
            case TOKEN_NAME: {
                advanceParser(parser);
                consume(parser, TOKEN_STRING, "Expected string after 'name'.");
                website->name = copyString(parser, parser->previous.lexeme);
                break;
            }
            case TOKEN_AUTHOR: {
                advanceParser(parser);
                consume(parser, TOKEN_STRING, "Expected string after 'author'.");
                website->author = copyString(parser, parser->previous.lexeme);
                break;
            }
            case TOKEN_VERSION: {
                advanceParser(parser);
                consume(parser, TOKEN_STRING, "Expected string after 'version'.");
                website->version = copyString(parser, parser->previous.lexeme);
                break;
            }
            case TOKEN_PAGES: {
                advanceParser(parser);
                consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'pages'.");
                PageNode *pagesHead = parsePages(parser);
                website->pageHead = pagesHead;
                break;
            }
            case TOKEN_STYLES: {
                advanceParser(parser);
                consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'styles'.");
                StyleBlockNode *styleHead = parseStyles(parser);
                website->styleHead = styleHead;
                break;
            }
            case TOKEN_LAYOUTS: {
                advanceParser(parser);
                consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'layouts'.");
                LayoutNode *layoutHead = parseLayouts(parser);
                website->layoutHead = layoutHead;
                break;
            }
            case TOKEN_PORT: {
                advanceParser(parser);
                if (parser->current.type != TOKEN_NUMBER) {
                    fputs("Expected number after 'port'.\n", stderr);
                    parser->hadError = 1;
                    break;
                }
                if (!parsePort(parser->current.lexeme, &website->port)) {
                    fprintf(stderr, "Invalid port number: %s (must be between 1 and 65535)\n", 
                            parser->current.lexeme);
                    parser->hadError = 1;
                    break;
                }
                advanceParser(parser);
                break;
            }
            case TOKEN_API: {
                advanceParser(parser);
                ApiEndpoint *endpoint = parseApi(parser);
                if (!website->apiHead) {
                    website->apiHead = endpoint;
                } else {
                    // Add to end of list
                    ApiEndpoint *current = website->apiHead;
                    while (current->next) {
                        current = current->next;
                    }
                    current->next = endpoint;
                }
                break;
            }
            case TOKEN_QUERY: {
                advanceParser(parser);
                QueryNode *query = parseQuery(parser);
                if (!website->queryHead) {
                    website->queryHead = query;
                } else {
                    // Add to end of list
                    QueryNode *current = website->queryHead;
                    while (current->next) {
                        current = current->next;
                    }
                    current->next = query;
                }
                break;
            }
            case TOKEN_DATABASE: {
                advanceParser(parser);
                consume(parser, TOKEN_STRING, "Expected string after 'database'.");
                website->databaseUrl = copyString(parser, parser->previous.lexeme);
                break;
            }
            default: {
                char buffer[256];
                snprintf(buffer, sizeof(buffer), 
                        "Parse error at line %d: Unexpected token '%s'\n",
                        parser->current.line, parser->current.lexeme);
                fputs(buffer, stderr);
                parser->hadError = 1;
                break;
            }
        }
        #pragma clang diagnostic pop
    }

    consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' at end of website block.");
    return website;
}
