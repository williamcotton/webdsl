/***************************************************
 *  dsl_interpreter.c
 ***************************************************/
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  TOKEN_H1,
  TOKEN_P,
  TOKEN_LINK,
  TOKEN_IMAGE,
  TOKEN_ALT,

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
        case TOKEN_H1: return "H1";
        case TOKEN_P: return "P";
        case TOKEN_LINK: return "LINK";
        case TOKEN_IMAGE: return "IMAGE";
        case TOKEN_ALT: return "ALT";
        case TOKEN_STRING: return "STRING";
        case TOKEN_OPEN_BRACE: return "OPEN_BRACE";
        case TOKEN_CLOSE_BRACE: return "CLOSE_BRACE";
        case TOKEN_OPEN_PAREN: return "OPEN_PAREN";
        case TOKEN_CLOSE_PAREN: return "CLOSE_PAREN";
        case TOKEN_EOF: return "EOF";
        case TOKEN_UNKNOWN: return "UNKNOWN";
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

typedef struct {
  const char *start;   // token start
  const char *current; // current char
  int line;
  uint32_t : 32; // Padding to 4 bytes
} Lexer;

static void initLexer(Lexer *lexer, const char *source) {
  lexer->start = source;
  lexer->current = source;
  lexer->line = 1;
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
  token.lexeme = (char *)malloc(length + 1);
  memcpy(token.lexeme, lexer->start, length);
  token.lexeme[length] = '\0';
  token.line = lexer->line;
  return token;
}

static Token errorToken(const char *message, int line) {
  Token token;
  token.type = TOKEN_UNKNOWN;
  token.lexeme = strdup(message);
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
  KW_MATCH("h1", TOKEN_H1)
  KW_MATCH("p", TOKEN_P)
  KW_MATCH("link", TOKEN_LINK)
  KW_MATCH("image", TOKEN_IMAGE)
  KW_MATCH("alt", TOKEN_ALT)

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
    return errorToken("Unterminated string.", lexer->line);
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
        printf("TOKEN: %-12s  LINE: %d  LEXEME: 'EOF'\n", 
               getTokenTypeName(token.type), token.line);
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
            token = errorToken("Unexpected character.", lexer->line);
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

typedef struct WebsiteNode {
  char *name;
  char *author;
  char *version;

  PageNode *pageHead;
  StyleBlockNode *styleHead;
} WebsiteNode;

/* --------------------------------------------------
 *                      PARSER
 * -------------------------------------------------- */

typedef struct {
  Lexer lexer;
  Token current;
  Token previous;
  int hadError;
  uint32_t : 32; // Padding to 4 bytes
} Parser;

static void initParser(Parser *parser, const char *source) {
  initLexer(&parser->lexer, source);
  parser->current.type = TOKEN_UNKNOWN;
  parser->previous.type = TOKEN_UNKNOWN;
  parser->hadError = 0;
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
static char *copyString(const char *source);

static void advanceParser(Parser *parser) {
  parser->previous = parser->current;
  parser->current = getNextToken(&parser->lexer);
}

static void consume(Parser *parser, TokenType type, const char *errorMsg) {
  if (parser->current.type == type) {
    advanceParser(parser);
    return;
  }
  fprintf(stderr, "Parse error at line %d: %s (got \"%s\")\n",
          parser->current.line, errorMsg, parser->current.lexeme);
  parser->hadError = 1;
}

/* Copy a token’s lexeme into a new heap-allocated string. */
static char *copyString(const char *source) {
  char *out = (char *)malloc(strlen(source) + 1);
  strcpy(out, source);
  return out;
}

/* The top-level parse: parse `website { ... }` */
static WebsiteNode *parseProgram(Parser *parser) {
  WebsiteNode *website = (WebsiteNode *)calloc(1, sizeof(WebsiteNode));
  advanceParser(parser); // read the first token

  consume(parser, TOKEN_WEBSITE, "Expected 'website' at start.");
  consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'website'.");

  while (parser->current.type != TOKEN_CLOSE_BRACE &&
         parser->current.type != TOKEN_EOF && !parser->hadError) {
    switch (parser->current.type) {
      case TOKEN_NAME: {
        advanceParser(parser); // consume 'name'
        consume(parser, TOKEN_STRING, "Expected string after 'name'.");
        website->name = copyString(parser->previous.lexeme);
        break;
      }
      case TOKEN_AUTHOR: {
        advanceParser(parser); // consume 'author'
        consume(parser, TOKEN_STRING, "Expected string after 'author'.");
        website->author = copyString(parser->previous.lexeme);
        break;
      }
      case TOKEN_VERSION: {
        advanceParser(parser); // consume 'version'
        consume(parser, TOKEN_STRING, "Expected string after 'version'.");
        website->version = copyString(parser->previous.lexeme);
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
      default: {
        // Catch-all for tokens we didn't explicitly handle
        fprintf(stderr, "Parse error at line %d: Unexpected token '%s'\n",
                parser->current.line, parser->current.lexeme);
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
  PageNode *page = (PageNode *)calloc(1, sizeof(PageNode));
  advanceParser(parser); // consume 'page'
  consume(parser, TOKEN_STRING, "Expected string for page identifier.");
  page->identifier = copyString(parser->previous.lexeme);

  consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after page identifier.");

  while (parser->current.type != TOKEN_CLOSE_BRACE &&
         parser->current.type != TOKEN_EOF && !parser->hadError) {
    switch (parser->current.type) {
      case TOKEN_ROUTE: {
        advanceParser(parser); // consume 'route'
        consume(parser, TOKEN_STRING, "Expected string after 'route'.");
        page->route = copyString(parser->previous.lexeme);
        break;
      }
      case TOKEN_LAYOUT: {
        advanceParser(parser); // consume 'layout'
        consume(parser, TOKEN_STRING, "Expected string after 'layout'.");
        page->layout = copyString(parser->previous.lexeme);
        break;
      }
      case TOKEN_CONTENT: {
        advanceParser(parser); // consume 'content'
        consume(parser, TOKEN_OPEN_BRACE, "Expected '{' after 'content'.");
        page->contentHead = parseContent(parser);
        break;
      }
      default: {
        fprintf(stderr,
                "Parse error at line %d: Unexpected token '%s' in page.\n",
                parser->current.line, parser->current.lexeme);
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
    ContentNode *node = (ContentNode *)calloc(1, sizeof(ContentNode));

    switch (parser->current.type) {
      case TOKEN_H1: {
        node->type = copyString("h1");
        advanceParser(parser); // consume 'h1'
        consume(parser, TOKEN_STRING, "Expected string after 'h1'.");
        node->arg1 = copyString(parser->previous.lexeme);
        break;
      }
      case TOKEN_P: {
        node->type = copyString("p");
        advanceParser(parser); // consume 'p'
        consume(parser, TOKEN_STRING, "Expected string after 'p'.");
        node->arg1 = copyString(parser->previous.lexeme);
        break;
      }
      case TOKEN_LINK: {
        node->type = copyString("link");
        advanceParser(parser); // consume 'link'
        // Expect 2 strings: URL and link text
        consume(parser, TOKEN_STRING, "Expected URL string after 'link'.");
        node->arg1 = copyString(parser->previous.lexeme);

        consume(parser, TOKEN_STRING, "Expected link text after URL string.");
        node->arg2 = copyString(parser->previous.lexeme);
        break;
      }
      case TOKEN_IMAGE: {
        node->type = copyString("image");
        advanceParser(parser); // consume 'image'
        // Expect 1 string for image source
        consume(parser, TOKEN_STRING, "Expected string after 'image'.");
        node->arg1 = copyString(parser->previous.lexeme);

        // Optional alt
        if (parser->current.type == TOKEN_ALT) {
          advanceParser(parser); // consume 'alt'
          consume(parser, TOKEN_STRING, "Expected alt text after 'alt'.");
          node->arg2 = copyString(parser->previous.lexeme);
        }
        break;
      }
      default: {
        // We can handle a close brace or unknown token here
        if (parser->current.type == TOKEN_CLOSE_BRACE) {
          // If it's a '}', content block is done
          free(node);
          node = NULL;
          break;
        }
        fprintf(stderr,
                "Parse error at line %d: Unexpected token '%s' in content.\n",
                parser->current.line, parser->current.lexeme);
        free(node);
        node = NULL;
        parser->hadError = 1;
        break;
      }
    }

    if (node) {
      // Link into the content list
      if (!head) {
        head = node;
        tail = node;
      } else {
        tail->next = node;
        tail = node;
      }
    }

    if (parser->hadError)
      break;
    // IMPORTANT: Do NOT advance again here; we've already advanced
    // in each case (and 'consume' calls advanceParser too).
  }

  consume(parser, TOKEN_CLOSE_BRACE, "Expected '}' at end of content block.");
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
        } else if (parser->current.type == TOKEN_STRING || 
                   parser->current.type == TOKEN_H1) {  // Allow H1 as a selector too
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
            fprintf(stderr, "Expected style selector or '}' at line %d\n", 
                    parser->current.line);
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
    StyleBlockNode *block = (StyleBlockNode *)calloc(1, sizeof(StyleBlockNode));
    
    // Save the selector (which can be either a STRING or H1 token)
    if (parser->current.type != TOKEN_STRING && parser->current.type != TOKEN_H1) {
        fprintf(stderr, "Expected style selector at line %d\n", parser->current.line);
        parser->hadError = 1;
        free(block);
        return NULL;
    }
    
    block->selector = copyString(parser->current.lexeme);
    advanceParser(parser); // consume the selector
    
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
      StylePropNode *prop = (StylePropNode *)calloc(1, sizeof(StylePropNode));
      prop->property = copyString(parser->current.lexeme);

      advanceParser(parser); // consume property

      // Expect a string for the value
      if (parser->current.type == TOKEN_STRING) {
        prop->value = copyString(parser->current.lexeme);
        advanceParser(parser);
      } else {
        fprintf(stderr, "Expected string value after style property.\n");
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

/* --------------------------------------------------
 *                INTERPRET / VISIT AST
 * -------------------------------------------------- */

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
    ContentNode *cn = p->contentHead;
    while (cn) {
      if (strcmp(cn->type, "h1") == 0) {
        printf("      <h1>%s</h1>\n", cn->arg1);
      } else if (strcmp(cn->type, "p") == 0) {
        printf("      <p>%s</p>\n", cn->arg1);
      } else if (strcmp(cn->type, "link") == 0) {
        printf("      <a href=\"%s\">%s</a>\n", cn->arg1, cn->arg2);
      } else if (strcmp(cn->type, "image") == 0) {
        printf("      <img src=\"%s\" alt=\"%s\"/>\n", cn->arg1,
               cn->arg2 ? cn->arg2 : "");
      }
      cn = cn->next;
    }
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
                           "    pages {\n"
                           "        page \"home\" {\n"
                           "            route \"/\"\n"
                           "            layout \"main\"\n"
                           "            content {\n"
                           "                h1 \"Welcome!\"\n"
                           "                p \"This is the home page.\"\n"
                           "                link \"/about\" \"Learn more\"\n"
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
    fprintf(stderr, "\nParsing failed due to errors.\n");
  }

  // NOTE: In real code, you'd free all allocated memory here.

  return 0;
}
