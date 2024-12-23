#ifndef AST_H
#define AST_H

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
    char *title;
    char *description;
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
    char *doctype;
    ContentNode *headContent;
    ContentNode *bodyContent;
    struct LayoutNode *next;
} LayoutNode;

typedef struct RouteMap {
    PageNode *page;
    char *route;
    struct RouteMap *next;
} RouteMap;

typedef struct LayoutMap {
    LayoutNode *layout;
    char *identifier;
    struct LayoutMap *next;
} LayoutMap;

typedef struct WebsiteNode {
    char *name;
    char *author;
    char *version;
    char *base_url;
    int port;
    int _padding;
    PageNode *pageHead;
    StyleBlockNode *styleHead;
    LayoutNode *layoutHead;
    RouteMap *routeMap;
    LayoutMap *layoutMap;
} WebsiteNode;

#endif // AST_H
