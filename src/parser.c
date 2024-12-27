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

    // Parse content
    consume(parser, TOKEN_CONTENT, "Expected 'content' in layout.");
    consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'content'.");
    layout->bodyContent = parseContent(parser);

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
        ContentNode *node = arenaAlloc(parser->arena, sizeof(ContentNode));
        memset(node, 0, sizeof(ContentNode));

        if (parser->current.type == TOKEN_STRING) {
            const char *value = parser->current.lexeme;
            node->type = copyString(parser, value);
            advanceParser(parser);

            // Check if this is a string literal (starts with quote)
            if (value[0] == '"') {
                // This is the special "content" placeholder
                if (strcmp(node->type, "\"content\"") == 0) {
                    node->type = "content";
                } else {
                    char buffer[256];
                    snprintf(buffer, sizeof(buffer),
                            "Unexpected quoted string '%s' in content block\n", value);
                    fputs(buffer, stderr);
                    parser->hadError = 1;
                    break;
                }
            }
            // Otherwise it's a tag type that needs arguments
            else if (parser->current.type == TOKEN_OPEN_BRACE) {
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
            break;
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

    consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' at end of styles block.");
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
            case TOKEN_RESPONSE: {
                advanceParser(parser);
                consume(parser, TOKEN_STRING, "Expected string after 'response'.");
                endpoint->response = copyString(parser, parser->previous.lexeme);
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
            case TOKEN_SQL: {
                advanceParser(parser);
                consume(parser, TOKEN_STRING, "Expected string after 'sql'.");
                query->sql = copyString(parser, parser->previous.lexeme);
                break;
            }
            default: {
                char buffer[256];
                snprintf(buffer, sizeof(buffer),
                        "Unexpected token in query block at line %d: %s\n",
                        parser->current.line, parser->current.lexeme);
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
