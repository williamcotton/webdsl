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

#endif // AST_H
