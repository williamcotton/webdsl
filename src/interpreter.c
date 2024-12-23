#include "interpreter.h"
#include <stdio.h>
#include <string.h>

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

void interpretWebsite(const WebsiteNode *website) {
    printf("Website:\n");
    if (website->name)
        printf("  Name:    %s\n", website->name);
    if (website->author)
        printf("  Author:  %s\n", website->author);
    if (website->version)
        printf("  Version: %s\n", website->version);

    // Print Layouts
    printf("\nLayouts:\n");
    LayoutNode *l = website->layoutHead;
    while (l) {
        printf("  Layout '%s':\n", l->identifier);
        printf("    Content:\n");
        printContent(l->contentHead, 0);
        l = l->next;
    }

    // Print Pages
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

    // Print Styles
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
