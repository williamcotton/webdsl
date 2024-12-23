/***************************************************
 *  dsl_interpreter.c
 ***************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------
 *                   ARENA ALLOCATION
 * -------------------------------------------------- */

#define ARENA_SIZE (1024 * 1024)  // 1MB arena

typedef struct {
    char *buffer;
    size_t size;
    size_t used;
} Arena;

static Arena* createArena(size_t size) {
    Arena *arena = malloc(sizeof(Arena));
    arena->buffer = malloc(size);
    arena->size = size;
    arena->used = 0;
    return arena;
}

static void* arenaAlloc(Arena *arena, size_t size) {
    // Align to 8 bytes
    size = (size + 7) & ~((size_t)7);
    
    if (arena->used + size > arena->size) {
        fputs("Arena out of memory\n", stderr);
        exit(1);
    }
    
    void *ptr = arena->buffer + arena->used;
    arena->used += size;
    return ptr;
}

static char* arenaDupString(Arena *arena, const char *str) {
    size_t len = strlen(str) + 1;
    char *dup = arenaAlloc(arena, len);
    memcpy(dup, str, len);
    return dup;
}

static void freeArena(Arena *arena) {
    free(arena->buffer);
    free(arena);
}

/* --------------------------------------------------
 *                 TOKEN DEFINITIONS
 * -------------------------------------------------- */

typedef enum {
  TOKEN_WEBSITE,
  TOKEN_PAGES,
  TOKEN_PAGE,
  TOKEN_STYLES,
  TOKEN_ROUTE,
  TOKEN_LAYOUT,
  TOKEN_CONTENT,
  TOKEN_NAME,
  TOKEN_AUTHOR,
  TOKEN_VERSION,
  TOKEN_ALT,
  TOKEN_LAYOUTS,

  TOKEN_STRING,
  TOKEN_OPEN_BRACE,
  TOKEN_CLOSE_BRACE,
  TOKEN_OPEN_PAREN,
  TOKEN_CLOSE_PAREN,
  TOKEN_EOF,
  TOKEN_UNKNOWN
} TokenType;

static const char* getTokenTypeName(TokenType type) {
    switch (type) {
        case TOKEN_WEBSITE: return "WEBSITE";
        case TOKEN_PAGES: return "PAGES";
        case TOKEN_PAGE: return "PAGE";
        case TOKEN_STYLES: return "STYLES";
        case TOKEN_ROUTE: return "ROUTE";
        case TOKEN_LAYOUT: return "LAYOUT";
        case TOKEN_CONTENT: return "CONTENT";
        case TOKEN_NAME: return "NAME";
        case TOKEN_AUTHOR: return "AUTHOR";
        case TOKEN_VERSION: return "VERSION";
        case TOKEN_ALT: return "ALT";
        case TOKEN_STRING: return "STRING";
        case TOKEN_OPEN_BRACE: return "OPEN_BRACE";
        case TOKEN_CLOSE_BRACE: return "CLOSE_BRACE";
        case TOKEN_OPEN_PAREN: return "OPEN_PAREN";
        case TOKEN_CLOSE_PAREN: return "CLOSE_PAREN";
        case TOKEN_EOF: return "EOF";
        case TOKEN_UNKNOWN: return "UNKNOWN";
        case TOKEN_LAYOUTS: return "LAYOUTS";
    }
    return "INVALID";
}

typedef struct {
  char *lexeme;
  TokenType type;
  int line;
  uint64_t : 64; // Padding to 8 bytes
} Token;

/* --------------------------------------------------
 *                   LEXER (TOKENIZER)
 * -------------------------------------------------- */

struct Parser;  // Forward declaration
struct Lexer;   // Forward declaration

typedef struct Lexer {
  const char *start;   
  const char *current;
  struct Parser *parser;
  int line;
  uint32_t : 32;
} Lexer;

typedef struct Parser {
  Lexer lexer;        // Now Lexer is fully defined
  Token current;
  Token previous;
  Arena *arena;
  int hadError;
  uint32_t : 32;
} Parser;

static void initLexer(Lexer *lexer, const char *source, Parser *parser) {
  lexer->start = source;
  lexer->current = source;
  lexer->line = 1;
  lexer->parser = parser;
}

static int isAlpha(char c) {
  return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c == '_') ||
          (c == '-'));
}

static int isDigit(char c) { return (c >= '0' && c <= '9'); }

static int isAtEnd(Lexer *lexer) { return *(lexer->current) == '\0'; }

static char advance(Lexer *lexer) {
  lexer->current++;
  return lexer->current[-1];
}

static char peek(Lexer *lexer) { return *(lexer->current); }

static char peekNext(Lexer *lexer) {
  if (isAtEnd(lexer))
    return '\0';
  return lexer->current[1];
}

static void skipWhitespace(Lexer *lexer) {
  for (;;) {
    char c = peek(lexer);
    switch (c) {
    case ' ':
    case '\r':
    case '\t':
      advance(lexer);
      break;
    case '\n':
      lexer->line++;
      advance(lexer);
      break;
    case '/':
      // Support "//" style comments
      if (peekNext(lexer) == '/') {
        while (peek(lexer) != '\n' && !isAtEnd(lexer)) {
          advance(lexer);
        }
      } else {
        return;
      }
      break;
    default:
      return;
    }
  }
}

static Token makeToken(Lexer *lexer, TokenType type) {
  Token token;
  token.type = type;
  size_t length = (size_t)(lexer->current - lexer->start);
  token.lexeme = arenaAlloc(lexer->parser->arena, length + 1);
  memcpy(token.lexeme, lexer->start, length);
  token.lexeme[length] = '\0';
  token.line = lexer->line;
  return token;
}

static Token errorToken(Parser *parser, const char *message, int line) {
  Token token;
  token.type = TOKEN_UNKNOWN;
  token.lexeme = arenaDupString(parser->arena, message);
  token.line = line;
  return token;
}

static TokenType checkKeyword(const char *start, size_t length) {
#define KW_MATCH(kw, ttype)                                                    \
  if (strncmp(start, kw, length) == 0 && strlen(kw) == length)                 \
    return ttype;

  KW_MATCH("website", TOKEN_WEBSITE)
  KW_MATCH("pages", TOKEN_PAGES)
  KW_MATCH("page", TOKEN_PAGE)
  KW_MATCH("styles", TOKEN_STYLES)
  KW_MATCH("route", TOKEN_ROUTE)
  KW_MATCH("layout", TOKEN_LAYOUT)
  KW_MATCH("content", TOKEN_CONTENT)
  KW_MATCH("name", TOKEN_NAME)
  KW_MATCH("author", TOKEN_AUTHOR)
  KW_MATCH("version", TOKEN_VERSION)
  KW_MATCH("alt", TOKEN_ALT)
  KW_MATCH("layouts", TOKEN_LAYOUTS)

  return TOKEN_UNKNOWN;
}

static Token identifierOrKeyword(Lexer *lexer) {
  while (isAlpha(peek(lexer)) || isDigit(peek(lexer)) || peek(lexer) == '_' ||
         peek(lexer) == '-') {
    advance(lexer);
  }

  size_t length = (size_t)(lexer->current - lexer->start);
  TokenType type = checkKeyword(lexer->start, length);
  if (type == TOKEN_UNKNOWN) {
    // It's an unknown identifier, treat it as a string for this DSL
    type = TOKEN_STRING;
  }
  return makeToken(lexer, type);
}

static Token stringLiteral(Lexer *lexer) {
  while (peek(lexer) != '"' && !isAtEnd(lexer)) {
    if (peek(lexer) == '\n') {
      lexer->line++;
    }
    advance(lexer);
  }

  if (isAtEnd(lexer)) {
    return errorToken(lexer->parser, "Unterminated string.", lexer->line);
  }
  // Consume the closing quote
  advance(lexer);
  return makeToken(lexer, TOKEN_STRING);
}

static Token getNextToken(Lexer *lexer) {
    skipWhitespace(lexer);
    lexer->start = lexer->current;

    if (isAtEnd(lexer)) {
        Token token = makeToken(lexer, TOKEN_EOF);
        // printf("TOKEN: %-12s  LINE: %d  LEXEME: 'EOF'\n", 
        //        getTokenTypeName(token.type), token.line);
        return token;
    }

    char c = advance(lexer);

    Token token;
    switch (c) {
    case '{':
        token = makeToken(lexer, TOKEN_OPEN_BRACE);
        break;
    case '}':
        token = makeToken(lexer, TOKEN_CLOSE_BRACE);
        break;
    case '(':
        token = makeToken(lexer, TOKEN_OPEN_PAREN);
        break;
    case ')':
        token = makeToken(lexer, TOKEN_CLOSE_PAREN);
        break;
    case '"':
        token = stringLiteral(lexer);
        break;
    default:
        if (isAlpha(c)) {
            token = identifierOrKeyword(lexer);
        } else {
            token = errorToken(lexer->parser, "Unexpected character.", lexer->line);
        }
        break;
    }

    printf("TOKEN: %-12s  LINE: %d  LEXEME: '%s'\n", 
           getTokenTypeName(token.type), token.line, token.lexeme);
    return token;
}

/* --------------------------------------------------
 *                      AST NODES
 * -------------------------------------------------- */

typedef struct ContentNode {
  char *type;
  char *arg1;
  char *arg2;
  struct ContentNode *next;
  struct ContentNode *children;
} ContentNode;

typedef struct PageNode {
  char *identifier;
  char *route;
  char *layout;
  ContentNode *contentHead;
  struct PageNode *next;
} PageNode;

typedef struct StylePropNode {
  char *property;
  char *value;
  struct StylePropNode *next;
} StylePropNode;

typedef struct StyleBlockNode {
  char *selector;
  StylePropNode *propHead;
  struct StyleBlockNode *next;
} StyleBlockNode;

typedef struct LayoutNode {
    char *identifier;
    ContentNode *contentHead;
    struct LayoutNode *next;
} LayoutNode;

typedef struct WebsiteNode {
  char *name;
  char *author;
  char *version;

  PageNode *pageHead;
  StyleBlockNode *styleHead;
  LayoutNode *layoutHead;
} WebsiteNode;

/* --------------------------------------------------
 *                      PARSER
 * -------------------------------------------------- */

static void initParser(Parser *parser, const char *source) {
  initLexer(&parser->lexer, source, parser);
  parser->current.type = TOKEN_UNKNOWN;
  parser->previous.type = TOKEN_UNKNOWN;
  parser->hadError = 0;
  parser->arena = createArena(ARENA_SIZE);
}

/* Forward declarations */
static WebsiteNode *parseProgram(Parser *parser);
static void advanceParser(Parser *parser);
static void consume(Parser *parser, TokenType type, const char *errorMsg);
static PageNode *parsePages(Parser *parser);
static PageNode *parsePage(Parser *parser);
static ContentNode *parseContent(Parser *parser);
static StyleBlockNode *parseStyles(Parser *parser);
static StyleBlockNode *parseStyleBlock(Parser *parser);
static StylePropNode *parseStyleProps(Parser *parser);
static char *copyString(Parser *parser, const char *source);
static LayoutNode *parseLayouts(Parser *parser);
static LayoutNode *parseLayout(Parser *parser);

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

/* Copy a token’s lexeme into a new heap-allocated string. */
static char *copyString(Parser *parser, const char *source) {
  return arenaDupString(parser->arena, source);
}

/* The top-level parse: parse `website { ... }` */
static WebsiteNode *parseProgram(Parser *parser) {
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
    #pragma clang diagnostic pop
      case TOKEN_NAME: {
        advanceParser(parser); // consume 'name'
        consume(parser, TOKEN_STRING, "Expected string after 'name'.");
        website->name = copyString(parser, parser->previous.lexeme);
        break;
      }
      case TOKEN_AUTHOR: {
        advanceParser(parser); // consume 'author'
        consume(parser, TOKEN_STRING, "Expected string after 'author'.");
        website->author = copyString(parser, parser->previous.lexeme);
        break;
      }
      case TOKEN_VERSION: {
        advanceParser(parser); // consume 'version'
        consume(parser, TOKEN_STRING, "Expected string after 'version'.");
        website->version = copyString(parser, parser->previous.lexeme);
        break;
      }
      case TOKEN_PAGES: {
        advanceParser(parser); // consume 'pages'
        consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'pages'.");
        PageNode *pagesHead = parsePages(parser);
        website->pageHead = pagesHead;
        break;
      }
      case TOKEN_STYLES: {
        advanceParser(parser); // consume 'styles'
        consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'styles'.");
        StyleBlockNode *styleHead = parseStyles(parser);
        website->styleHead = styleHead;
        break;
      }
      case TOKEN_LAYOUTS: {
        advanceParser(parser); // consume 'layouts'
        consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'layouts'.");
        LayoutNode *layoutHead = parseLayouts(parser);
        website->layoutHead = layoutHead;
        break;
      }
      default: {
        // Catch-all for tokens we didn't explicitly handle
        char buffer[256];
        snprintf(buffer, sizeof(buffer), 
                "Parse error at line %d: Unexpected token '%s'\n",
                parser->current.line, parser->current.lexeme);
        fputs(buffer, stderr);
        parser->hadError = 1;
        break;
      }
    }
  }

  consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' at end of website block.");

  return website;
}

/* Parse multiple `page "identifier" { ... }` blocks. */
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
  // Expect `}` for `pages { ... }`
  consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' at end of pages block.");
  return head;
}

/* Parse a single `page "identifier" { ... }` block */
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
    #pragma clang diagnostic pop
      case TOKEN_ROUTE: {
        advanceParser(parser); // consume 'route'
        consume(parser, TOKEN_STRING, "Expected string after 'route'.");
        page->route = copyString(parser, parser->previous.lexeme);
        break;
      }
      case TOKEN_LAYOUT: {
        advanceParser(parser); // consume 'layout'
        consume(parser, TOKEN_STRING, "Expected string after 'layout'.");
        page->layout = copyString(parser, parser->previous.lexeme);
        break;
      }
      case TOKEN_CONTENT: {
        advanceParser(parser); // consume 'content'
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
  }

  consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' after page block.");
  return page;
}

/* Parse content block:
   content {
     h1 "Welcome!"
     p "Paragraph text"
     link "/some-url" "Some link"
   }
*/
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
          // Just store it as "content" without quotes
          node->type = "content";
        } else {
          // Unexpected quoted string
          char buffer[256];
          snprintf(buffer, sizeof(buffer),
                  "Unexpected quoted string '%s' in content block\n", value);
          fputs(buffer, stderr);
          parser->hadError = 1;
          free(node);
          break;
        }
      }
      // Otherwise it's a tag type that needs arguments
      else if (parser->current.type == TOKEN_OPEN_BRACE) {
        advanceParser(parser); // consume '{'
        node->children = parseContent(parser);
      } else {
        // All tags require at least one string argument if not nested
        consume(parser, TOKEN_STRING, "Expected string after tag");
        node->arg1 = copyString(parser, parser->previous.lexeme);

        // Special handling for tags that need two arguments (like links)
        if (strcmp(node->type, "link") == 0) {
          consume(parser, TOKEN_STRING, "Expected link text after URL string");
          node->arg2 = copyString(parser, parser->previous.lexeme);
        }
        // Handle alt text for images
        else if (strcmp(node->type, "image") == 0 && parser->current.type == TOKEN_ALT) {
          advanceParser(parser);
          consume(parser, TOKEN_STRING, "Expected alt text after 'alt'");
          node->arg2 = copyString(parser, parser->previous.lexeme);
        }
      }

      // Link into the content list
      if (!head) {
        head = node;
        tail = node;
      } else {
        tail->next = node;
        tail = node;
      }
    } else {
      free(node);
      break;
    }
  }

  consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' at end of content block");
  return head;
}

/* Parse styles block:
   styles {
       body {
           background "#fff"
           color "#333"
       }
       h1 {
           color "#ff6600"
       }
   }
*/
static StyleBlockNode *parseStyles(Parser *parser) {
    StyleBlockNode *head = NULL;
    StyleBlockNode *tail = NULL;

    while (!parser->hadError) {
        // We expect either a selector (STRING) or a closing brace
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

/* Parse a single style block:
   body {
       background "#fff"
       color "#333"
   }
*/
static StyleBlockNode *parseStyleBlock(Parser *parser) {
    StyleBlockNode *block = arenaAlloc(parser->arena, sizeof(StyleBlockNode));
    memset(block, 0, sizeof(StyleBlockNode));
    
    // Now we just check for STRING token for all selectors
    if (parser->current.type != TOKEN_STRING) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer),
                "Expected style selector at line %d\n",
                parser->current.line);
        fputs(buffer, stderr);
        parser->hadError = 1;
        free(block);
        return NULL;
    }
    
    block->selector = copyString(parser, parser->current.lexeme);
    advanceParser(parser);
    
    consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after style selector.");
    
    StylePropNode *propHead = parseStyleProps(parser);
    block->propHead = propHead;
    
    return block;
}

/* Parse a list of style properties in a single block:
   background "#fff"
   color "#333"
*/
static StylePropNode *parseStyleProps(Parser *parser) {
  StylePropNode *head = NULL;
  StylePropNode *tail = NULL;

  while (parser->current.type != TOKEN_CLOSE_BRACE &&
         parser->current.type != TOKEN_EOF && !parser->hadError) {
    if (parser->current.type == TOKEN_STRING) {
      // e.g. property name
      StylePropNode *prop = arenaAlloc(parser->arena, sizeof(StylePropNode));
      memset(prop, 0, sizeof(StylePropNode));
      prop->property = copyString(parser, parser->current.lexeme);

      advanceParser(parser); // consume property

      // Expect a string for the value
      if (parser->current.type == TOKEN_STRING) {
        prop->value = copyString(parser, parser->current.lexeme);
        advanceParser(parser);
      } else {
        fputs("Expected string value after style property.\n", stderr);
        free(prop);
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
      // Possibly a '}' or unknown token
      break;
    }
  }

  consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' at end of style block.");
  return head;
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
    layout->contentHead = parseContent(parser);

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

/* --------------------------------------------------
 *                INTERPRET / VISIT AST
 * -------------------------------------------------- */

static void printContent(const ContentNode *cn, int indent) {
  while (cn) {
    for (int i = 0; i < indent; i++) printf("  ");
    
    if (cn->children) {
      if (strcmp(cn->type, "p") == 0) {
        printf("      <p>\n");
        printContent(cn->children, indent + 1);
        for (int i = 0; i < indent; i++) printf("  ");
        printf("      </p>\n");
      }
    } else {
      if (strcmp(cn->type, "content") == 0) {
        printf("      <!-- Page Content Goes Here -->\n");
      } else if (strcmp(cn->type, "h1") == 0) {
        printf("      <h1>%s</h1>\n", cn->arg1);
      } else if (strcmp(cn->type, "p") == 0) {
        printf("      <p>%s</p>\n", cn->arg1);
      } else if (strcmp(cn->type, "link") == 0) {
        printf("      <a href=\"%s\">%s</a>\n", cn->arg1, cn->arg2);
      } else if (strcmp(cn->type, "image") == 0) {
        printf("      <img src=\"%s\" alt=\"%s\"/>\n", cn->arg1,
               cn->arg2 ? cn->arg2 : "");
      }
    }
    cn = cn->next;
  }
}

static void interpretWebsite(const WebsiteNode *website) {
  printf("Website:\n");
  if (website->name)
    printf("  Name:    %s\n", website->name);
  if (website->author)
    printf("  Author:  %s\n", website->author);
  if (website->version)
    printf("  Version: %s\n", website->version);

  // Pages
  printf("\nPages:\n");
  PageNode *p = website->pageHead;
  while (p) {
    printf("  Page '%s':\n", p->identifier);
    if (p->route)
      printf("    Route:  %s\n", p->route);
    if (p->layout)
      printf("    Layout: %s\n", p->layout);

    printf("    Content:\n");
    printContent(p->contentHead, 0);
    p = p->next;
  }

  // Styles
  printf("\nStyles:\n");
  StyleBlockNode *sb = website->styleHead;
  while (sb) {
    printf("  Selector '%s' {\n", sb->selector);
    StylePropNode *sp = sb->propHead;
    while (sp) {
      printf("    %s: %s;\n", sp->property, sp->value);
      sp = sp->next;
    }
    printf("  }\n\n");
    sb = sb->next;
  }

  // Print Layouts
  printf("\nLayouts:\n");
  LayoutNode *l = website->layoutHead;
  while (l) {
    printf("  Layout '%s':\n", l->identifier);
    printf("    Content:\n");
    printContent(l->contentHead, 0);
    l = l->next;
  }
}

/* --------------------------------------------------
 *                       MAIN
 * -------------------------------------------------- */

int main(void) {
  // Example DSL input:
  const char *sourceCode = "website {\n"
                          "    name \"My Awesome Site\"\n"
                          "    author \"John Smith\"\n"
                          "    version \"1.0\"\n"
                          "\n"
                          "    layouts {\n"
                          "        \"main\" {\n"
                          "            content {\n"
                          "                h1 \"Site Header\"\n"
                          "                p \"Welcome to our website\"\n"
                          "                \"content\"\n"
                          "                p \"Footer text\"\n"
                          "            }\n"
                          "        }\n"
                          "        \"blog\" {\n"
                          "            content {\n"
                          "                h1 \"Blog Layout\"\n"
                          "                \"content\"\n"
                          "                p \"Blog footer\"\n"
                          "            }\n"
                          "        }\n"
                          "    }\n"
                          "\n"
                          "    pages {\n"
                          "        page \"home\" {\n"
                          "            route \"/\"\n"
                          "            layout \"main\"\n"
                          "            content {\n"
                          "                h1 \"Welcome!\"\n"
                          "                p {\n"
                          "                    link \"/about\" \"Learn more about our site\"\n"
                          "                }\n"
                          "                p \"This is a regular paragraph.\"\n"
                          "            }\n"
                          "        }\n"
                          "        page \"blog\" {\n"
                          "            route \"/blog\"\n"
                          "            layout \"blog\"\n"
                          "            content {\n"
                          "                h1 \"Latest Posts\"\n"
                          "                p \"Check out our latest blog posts!\"\n"
                          "            }\n"
                          "        }\n"
                          "    }\n"
                          "\n"
                          "    styles {\n"
                          "        body {\n"
                          "            background \"#ffffff\"\n"
                          "            color \"#333\"\n"
                          "        }\n"
                          "        h1 {\n"
                          "            color \"#ff6600\"\n"
                          "        }\n"
                          "    }\n"
                          "}\n";

  Parser parser;
  initParser(&parser, sourceCode);

  WebsiteNode *website = parseProgram(&parser);

  if (!parser.hadError) {
    interpretWebsite(website);
  } else {
    fputs("\nParsing failed due to errors.\n", stderr);
  }

  // Free all memory at once
  freeArena(parser.arena);

  return parser.hadError ? 1 : 0;
}
