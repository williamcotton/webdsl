#include "parser.h"
#include "include.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

// Forward declarations
static void advanceParser(Parser *parser);
static void consume(Parser *parser, TokenType type, const char *errorMsg);
static PageNode *parsePage(Parser *parser);
static StyleBlockNode *parseStyleBlock(Parser *parser);
static StylePropNode *parseStyleProps(Parser *parser);
static LayoutNode *parseLayout(Parser *parser);
static char *copyString(Parser *parser, const char *source);
static bool parsePort(const char* str, int* result);
static ApiEndpoint *parseApi(Parser *parser);
static QueryNode *parseQuery(Parser *parser);
static ApiField *parseApiFields(Parser *parser);
static QueryParam *parseQueryParams(Parser *parser);
static PipelineStepNode* parsePipelineStep(Parser *parser);
static PipelineStepNode* parsePipeline(Parser *parser);
static TransformNode* parseTransform(Parser *parser);
static ScriptNode* parseScript(Parser *parser);
static IncludeNode* parseInclude(Parser *parser);

// Forward declaration of setupStepExecutor from api.c
void setupStepExecutor(PipelineStepNode *step);

void initParser(Parser *parser, const char *source) {
    initLexer(&parser->lexer, source, parser);
    parser->current.type = TOKEN_UNKNOWN;
    parser->previous.type = TOKEN_UNKNOWN;
    parser->hadError = 0;
    parser->arena = createArena(1024 * 1024); // 1MB arena
    parser->lexer.line = 1;  // Reset line number
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

static TemplateNode *parseTemplate(Parser *parser, TokenType templateType) {
    TemplateNode *node = arenaAlloc(parser->arena, sizeof(TemplateNode));
    memset(node, 0, sizeof(TemplateNode));

    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wswitch-enum"
    // Set template type based on token
    switch (templateType) {
        case TOKEN_MUSTACHE:
            node->type = TEMPLATE_MUSTACHE;
            break;
        case TOKEN_HTML:
            node->type = TEMPLATE_HTML;
            break;
        default:
            node->type = TEMPLATE_RAW;
            break;
    }
    #pragma clang diagnostic pop
    
    // Parse the template content
    if (parser->current.type == TOKEN_RAW_BLOCK || parser->current.type == TOKEN_STRING) {
        node->content = copyString(parser, parser->current.lexeme);
        advanceParser(parser);  // consume raw block or string
    } else {
        char buffer[256];
        snprintf(buffer, sizeof(buffer),
                "Expected template content at line %d\n",
                parser->current.line);
        fputs(buffer, stderr);
        parser->hadError = 1;
        return NULL;
    }
    
    return node;
}

static LayoutNode *parseLayout(Parser *parser) {
    LayoutNode *layout = arenaAlloc(parser->arena, sizeof(LayoutNode));
    memset(layout, 0, sizeof(LayoutNode));

    consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'layout'.");

    while (parser->current.type != TOKEN_CLOSE_BRACE &&
           parser->current.type != TOKEN_EOF && !parser->hadError) {
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wswitch-enum"
        switch (parser->current.type) {
            case TOKEN_NAME: {
                advanceParser(parser);
                consume(parser, TOKEN_STRING, "Expected string after 'name'.");
                layout->identifier = copyString(parser, parser->previous.lexeme);
                break;
            }
            case TOKEN_HTML: {
                advanceParser(parser);
                layout->bodyTemplate = parseTemplate(parser, TOKEN_HTML);
                break;
            }
            case TOKEN_MUSTACHE: {
                advanceParser(parser);
                layout->bodyTemplate = parseTemplate(parser, TOKEN_MUSTACHE);
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

static PageNode *parsePage(Parser *parser) {
    PageNode *page = arenaAlloc(parser->arena, sizeof(PageNode));
    memset(page, 0, sizeof(PageNode));
    
    consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'page'.");

    while (parser->current.type != TOKEN_CLOSE_BRACE &&
           parser->current.type != TOKEN_EOF && !parser->hadError) {
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wswitch-enum"
        switch (parser->current.type) {
            case TOKEN_NAME: {
                advanceParser(parser);
                consume(parser, TOKEN_STRING, "Expected string after 'name'.");
                page->identifier = copyString(parser, parser->previous.lexeme);
                break;
            }
            case TOKEN_ROUTE: {
                advanceParser(parser);
                consume(parser, TOKEN_STRING, "Expected string after 'route'.");
                page->route = copyString(parser, parser->previous.lexeme);
                break;
            }
            case TOKEN_METHOD: {
                advanceParser(parser);
                consume(parser, TOKEN_STRING, "Expected string after 'method'.");
                page->method = copyString(parser, parser->previous.lexeme);
                break;
            }
            case TOKEN_FIELDS: {
                advanceParser(parser);
                consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'fields'.");
                page->fields = parseApiFields(parser);
                break;
            }
            case TOKEN_LAYOUT: {
                advanceParser(parser);
                consume(parser, TOKEN_STRING, "Expected string after 'layout'.");
                page->layout = copyString(parser, parser->previous.lexeme);
                break;
            }
            case TOKEN_HTML: {
                advanceParser(parser);
                page->template = parseTemplate(parser, TOKEN_HTML);
                break;
            }
            case TOKEN_MUSTACHE: {
                advanceParser(parser);
                page->template = parseTemplate(parser, TOKEN_MUSTACHE);
                break;
            }
            case TOKEN_ERROR: {
                advanceParser(parser);
                consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'error'.");
                consume(parser, TOKEN_MUSTACHE, "Expected 'mustache' after '{'.");
                page->errorTemplate = parseTemplate(parser, TOKEN_MUSTACHE);
                consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' after error block.");
                break;
            }
            case TOKEN_SUCCESS: {
                advanceParser(parser);
                consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'success'.");
                consume(parser, TOKEN_MUSTACHE, "Expected 'mustache' after '{'.");
                page->successTemplate = parseTemplate(parser, TOKEN_MUSTACHE);
                consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' after success block.");
                break;
            }
            case TOKEN_CONTENT: {
                advanceParser(parser);
                consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'content'.");
                page->template = parseTemplate(parser, TOKEN_HTML);
                break;
            }
            case TOKEN_REDIRECT: {
                advanceParser(parser);
                consume(parser, TOKEN_STRING, "Expected string after 'redirect'.");
                page->redirect = copyString(parser, parser->previous.lexeme);
                break;
            }
            case TOKEN_PIPELINE: {
                advanceParser(parser);
                page->pipeline = parsePipeline(parser);
                break;
            }
            default: {
                char buffer[256];
                snprintf(buffer, sizeof(buffer),
                        "Parse error at line %d: Unexpected token '%s' in page.\n",
                        parser->current.line, parser->current.lexeme);
                fputs(buffer, stderr);
                parser->hadError = 1;
                advanceParser(parser);
                break;
            }
        }
        #pragma clang diagnostic pop
    }

    consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' after page block.");
    return page;
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

static PipelineStepNode* parsePipelineStep(Parser *parser) {
    PipelineStepNode *step = arenaAlloc(parser->arena, sizeof(PipelineStepNode));
    memset(step, 0, sizeof(PipelineStepNode));
    
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wswitch-enum"
    switch (parser->current.type) {
        case TOKEN_JQ:
            step->type = STEP_JQ;
            advanceParser(parser);
            if (parser->current.type == TOKEN_RAW_BLOCK || parser->current.type == TOKEN_STRING) {
                step->code = copyString(parser, parser->current.lexeme);
                advanceParser(parser);
            } else {
                parser->hadError = 1;
                fputs("Expected JQ filter code block\n", stderr);
            }
            break;
            
        case TOKEN_LUA:
            step->type = STEP_LUA;
            step->is_dynamic = true;
            advanceParser(parser);
            if (parser->current.type == TOKEN_RAW_BLOCK || parser->current.type == TOKEN_STRING) {
                step->code = copyString(parser, parser->current.lexeme);
                advanceParser(parser);
            } else {
                parser->hadError = 1;
                fputs("Expected Lua code block\n", stderr);
            }
            break;
            
        case TOKEN_EXECUTE_QUERY:
            step->type = STEP_SQL;
            step->is_dynamic = false;
            advanceParser(parser);
            if (parser->current.type == TOKEN_STRING) {
                step->name = copyString(parser, parser->current.lexeme);
                advanceParser(parser);
            } else if (parser->current.type == TOKEN_DYNAMIC) {
                step->is_dynamic = true;
                advanceParser(parser);
            } else {
                parser->hadError = 1;
                fputs("Expected query name or 'dynamic' after executeQuery\n", stderr);
            }
            break;

        case TOKEN_EXECUTE_TRANSFORM:
            step->type = STEP_JQ;
            step->is_dynamic = false;
            advanceParser(parser);
            if (parser->current.type == TOKEN_STRING) {
                step->name = copyString(parser, parser->current.lexeme);
                advanceParser(parser);
            } else {
                parser->hadError = 1;
                fputs("Expected transform name after executeTransform\n", stderr);
            }
            break;

        case TOKEN_EXECUTE_SCRIPT:
            step->type = STEP_LUA;
            step->is_dynamic = false;
            advanceParser(parser);
            if (parser->current.type == TOKEN_STRING) {
                step->name = copyString(parser, parser->current.lexeme);
                advanceParser(parser);
            } else {
                parser->hadError = 1;
                fputs("Expected script name after executeScript\n", stderr);
            }
            break;

        case TOKEN_SQL:
            step->type = STEP_SQL;
            step->is_dynamic = false;
            advanceParser(parser);
            if (parser->current.type == TOKEN_RAW_BLOCK || 
                parser->current.type == TOKEN_STRING) {
                step->code = copyString(parser, parser->current.lexeme);
                advanceParser(parser);
            } else {
                parser->hadError = 1;
                fputs("Expected SQL query block\n", stderr);
            }
            break;
            
        default:
            parser->hadError = 1;
            fputs("Expected pipeline step (jq, lua, executeQuery, executeTransform, executeScript)\n", stderr);
            break;
    }
    #pragma clang diagnostic pop
    
    setupStepExecutor(step);
    return step;
}

static PipelineStepNode* parsePipeline(Parser *parser) {
    PipelineStepNode *head = NULL;
    PipelineStepNode *tail = NULL;
    
    consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after pipeline");
    
    while (parser->current.type != TOKEN_CLOSE_BRACE && 
           parser->current.type != TOKEN_EOF && 
           !parser->hadError) {
        
        PipelineStepNode *step = parsePipelineStep(parser);
        if (step) {
            if (!head) {
                head = step;
                tail = step;
            } else {
                tail->next = step;
                tail = step;
            }
        }
    }
    
    consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' after pipeline block");
    return head;
}

static ApiEndpoint *parseApi(Parser *parser) {
    consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'api'.");
    
    ApiEndpoint *endpoint = arenaAlloc(parser->arena, sizeof(ApiEndpoint));
    memset(endpoint, 0, sizeof(ApiEndpoint));
    
    while (parser->current.type != TOKEN_CLOSE_BRACE && 
           parser->current.type != TOKEN_EOF && 
           !parser->hadError) {
        
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wswitch-enum"
        switch (parser->current.type) {
            case TOKEN_ROUTE:
                advanceParser(parser);
                consume(parser, TOKEN_STRING, "Expected string after 'route'");
                endpoint->route = copyString(parser, parser->previous.lexeme);
                break;
                
            case TOKEN_METHOD:
                advanceParser(parser);
                consume(parser, TOKEN_STRING, "Expected string after 'method'");
                endpoint->method = copyString(parser, parser->previous.lexeme);
                break;
                
            case TOKEN_PIPELINE:
                advanceParser(parser);
                endpoint->uses_pipeline = true;
                endpoint->pipeline = parsePipeline(parser);
                break;
            
            case TOKEN_FIELDS:
                advanceParser(parser);
                consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'fields'.");
                endpoint->apiFields = parseApiFields(parser);
                break;
                
            default: {
                char buffer[256];
                snprintf(buffer, sizeof(buffer),
                        "Unexpected token in api endpoint at line %d\n",
                        parser->current.line);
                fputs(buffer, stderr);
                parser->hadError = 1;
                break;
            }
        }
        #pragma clang diagnostic pop
    }
    
    consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' after api endpoint");
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

static TransformNode* parseTransform(Parser *parser) {
    TransformNode *transform = arenaAlloc(parser->arena, sizeof(TransformNode));
    memset(transform, 0, sizeof(TransformNode));
    
    consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'transform'");
    
    while (parser->current.type != TOKEN_CLOSE_BRACE &&
           parser->current.type != TOKEN_EOF && !parser->hadError) {
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wswitch-enum"
        switch (parser->current.type) {
            case TOKEN_NAME: {
                advanceParser(parser);
                consume(parser, TOKEN_STRING, "Expected string after 'name'.");
                transform->name = copyString(parser, parser->previous.lexeme);
                break;
            }
            case TOKEN_JQ: {
                advanceParser(parser);
                transform->type = FILTER_JQ;
                if (parser->current.type == TOKEN_RAW_BLOCK || parser->current.type == TOKEN_STRING) {
                    transform->code = copyString(parser, parser->current.lexeme);
                    advanceParser(parser);
                } else {
                    parser->hadError = 1;
                    fputs("Expected JQ code block\n", stderr);
                }
                break;
            }
            default: {
                char buffer[256];
                snprintf(buffer, sizeof(buffer),
                        "Parse error at line %d: Unexpected token in transform.\n",
                        parser->current.line);
                fputs(buffer, stderr);
                parser->hadError = 1;
                break;
            }
        }
        #pragma clang diagnostic pop
    }
    
    consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' after transform block");
    return transform;
}

static ScriptNode* parseScript(Parser *parser) {
    ScriptNode *script = arenaAlloc(parser->arena, sizeof(ScriptNode));
    memset(script, 0, sizeof(ScriptNode));
    
    consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'script'");
    
    while (parser->current.type != TOKEN_CLOSE_BRACE &&
           parser->current.type != TOKEN_EOF && !parser->hadError) {
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wswitch-enum"
        switch (parser->current.type) {
            case TOKEN_NAME: {
                advanceParser(parser);
                consume(parser, TOKEN_STRING, "Expected string after 'name'.");
                script->name = copyString(parser, parser->previous.lexeme);
                break;
            }
            case TOKEN_LUA: {
                advanceParser(parser);
                script->type = FILTER_LUA;
                if (parser->current.type == TOKEN_RAW_BLOCK || parser->current.type == TOKEN_STRING) {
                    script->code = copyString(parser, parser->current.lexeme);
                    advanceParser(parser);
                } else {
                    parser->hadError = 1;
                    fputs("Expected Lua code block\n", stderr);
                }
                break;
            }
            default: {
                char buffer[256];
                snprintf(buffer, sizeof(buffer),
                        "Parse error at line %d: Unexpected token in script.\n",
                        parser->current.line);
                fputs(buffer, stderr);
                parser->hadError = 1;
                break;
            }
        }
        #pragma clang diagnostic pop
    }
    
    consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' after script block");
    return script;
}

static IncludeNode* parseInclude(Parser *parser) {
    IncludeNode *include = arenaAlloc(parser->arena, sizeof(IncludeNode));
    memset(include, 0, sizeof(IncludeNode));
    
    // Store the line number for error reporting
    include->line = parser->current.line;
    
    // Expect and consume the filepath string
    consume(parser, TOKEN_STRING, "Expected file path after 'include'");
    include->filepath = copyString(parser, parser->previous.lexeme);
    
    return include;
}

static void parseWebsiteNode(Parser *parser, WebsiteNode *website) {
    while (parser->current.type != TOKEN_EOF && 
           parser->current.type != TOKEN_CLOSE_BRACE && 
           !parser->hadError) {
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wswitch-enum"
        switch (parser->current.type) {
            case TOKEN_PAGE: {
                advanceParser(parser);
                PageNode *page = parsePage(parser);
                if (!website->pageHead) {
                    website->pageHead = page;
                } else {
                    PageNode *current = website->pageHead;
                    while (current->next) {
                        current = current->next;
                    }
                    current->next = page;
                }
                break;
            }
            case TOKEN_LAYOUT: {
                advanceParser(parser);
                LayoutNode *layout = parseLayout(parser);
                if (!website->layoutHead) {
                    website->layoutHead = layout;
                } else {
                    LayoutNode *current = website->layoutHead;
                    while (current->next) {
                        current = current->next;
                    }
                    current->next = layout;
                }
                break;
            }
            case TOKEN_STYLES: {
                advanceParser(parser);
                consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'styles'.");
                
                while (parser->current.type != TOKEN_CLOSE_BRACE && 
                       parser->current.type != TOKEN_EOF && 
                       !parser->hadError) {
                    
                    if (parser->current.type == TOKEN_CSS || parser->current.type == TOKEN_RAW_BLOCK) {
                        StyleBlockNode *block = arenaAlloc(parser->arena, sizeof(StyleBlockNode));
                        memset(block, 0, sizeof(StyleBlockNode));
                        
                        StylePropNode *prop = arenaAlloc(parser->arena, sizeof(StylePropNode));
                        memset(prop, 0, sizeof(StylePropNode));
                        prop->property = "raw_css";
                        
                        if (parser->current.type == TOKEN_CSS) {
                            advanceParser(parser);
                            consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'css'");
                        }
                        
                        prop->value = copyString(parser, parser->current.lexeme);
                        block->propHead = prop;
                        
                        advanceParser(parser);
                        
                        if (!website->styleHead) {
                            website->styleHead = block;
                        } else {
                            StyleBlockNode *current = website->styleHead;
                            while (current->next) {
                                current = current->next;
                            }
                            current->next = block;
                        }
                    } else if (parser->current.type == TOKEN_STRING) {
                        StyleBlockNode *block = parseStyleBlock(parser);
                        if (block) {
                            if (!website->styleHead) {
                                website->styleHead = block;
                            } else {
                                StyleBlockNode *current = website->styleHead;
                                while (current->next) {
                                    current = current->next;
                                }
                                current->next = block;
                            }
                        }
                    } else {
                        char buffer[256];
                        snprintf(buffer, sizeof(buffer),
                                "Expected style selector or CSS block at line %d\n",
                                parser->current.line);
                        fputs(buffer, stderr);
                        parser->hadError = 1;
                        break;
                    }
                }
                
                consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' after styles block");
                break;
            }
            case TOKEN_API: {
                advanceParser(parser);
                ApiEndpoint *endpoint = parseApi(parser);
                if (!website->apiHead) {
                    website->apiHead = endpoint;
                } else {
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
                    QueryNode *current = website->queryHead;
                    while (current->next) {
                        current = current->next;
                    }
                    current->next = query;
                }
                break;
            }
            case TOKEN_TRANSFORM: {
                advanceParser(parser);
                TransformNode *transform = parseTransform(parser);
                if (!website->transformHead) {
                    website->transformHead = transform;
                } else {
                    TransformNode *current = website->transformHead;
                    while (current->next) {
                        current = current->next;
                    }
                    current->next = transform;
                }
                break;
            }
            case TOKEN_SCRIPT: {
                advanceParser(parser);
                ScriptNode *script = parseScript(parser);
                if (!website->scriptHead) {
                    website->scriptHead = script;
                } else {
                    ScriptNode *current = website->scriptHead;
                    while (current->next) {
                        current = current->next;
                    }
                    current->next = script;
                }
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
}

WebsiteNode *parseProgram(Parser *parser) {
    WebsiteNode *website = arenaAlloc(parser->arena, sizeof(WebsiteNode));
    memset(website, 0, sizeof(WebsiteNode));
    advanceParser(parser); // read the first token

    // Initialize include state
    IncludeState includeState;
    initIncludeState(&includeState);

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
            case TOKEN_INCLUDE: {
                advanceParser(parser);
                IncludeNode *include = parseInclude(parser);
                if (!processInclude(parser, website, include->filepath, &includeState)) {
                    parser->hadError = 1;
                }
                if (!website->includeHead) {
                    website->includeHead = include;
                } else {
                    IncludeNode *current = website->includeHead;
                    while (current->next) {
                        current = current->next;
                    }
                    current->next = include;
                }
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
            case TOKEN_DATABASE: {
                advanceParser(parser);
                consume(parser, TOKEN_STRING, "Expected string after 'database'.");
                website->databaseUrl = copyString(parser, parser->previous.lexeme);
                break;
            }
            default:
                // Handle all other website content
                parseWebsiteNode(parser, website);
                break;
        }
        #pragma clang diagnostic pop
    }

    consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' at end of website block.");

    // Free include state
    for (int i = 0; i < includeState.num_included; i++) {
        free(includeState.included_files[i]);
    }
    free(includeState.included_files);

    return website;
}

WebsiteNode *parseWebsiteContent(Parser *parser) {
    WebsiteNode *website = arenaAlloc(parser->arena, sizeof(WebsiteNode));
    memset(website, 0, sizeof(WebsiteNode));
    
    advanceParser(parser); // read the first token
    parseWebsiteNode(parser, website);
    return website;
}
