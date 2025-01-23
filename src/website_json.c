#include "website_json.h"
#include "ast.h"
#include <jansson.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
#pragma clang diagnostic pop
#include <string.h>

// Forward declarations
static json_t* templateNodeToJson(const TemplateNode* node);
static json_t* layoutToJson(const LayoutNode* layout);
static json_t* responseBlockToJson(const ResponseBlockNode* block);
static json_t* apiFieldsToJson(const ApiField* fields);
static json_t* pipelineToJson(const PipelineStepNode* pipeline);

static json_t* templateNodeToJson(const TemplateNode* node) {
    if (!node) return NULL;
    
    json_t* template = json_object();
    json_object_set_new(template, "type", json_integer(node->type));
    json_object_set_new(template, "content", json_string(node->content));
    return template;
}

static json_t* layoutToJson(const LayoutNode* layout) {
    if (!layout) return NULL;
    
    json_t* layouts = json_array();
    const LayoutNode* current = layout;
    
    while (current) {
        json_t* layout_obj = json_object();
        if (current->identifier) json_object_set_new(layout_obj, "identifier", json_string(current->identifier));
        if (current->doctype) json_object_set_new(layout_obj, "doctype", json_string(current->doctype));
        
        json_t* head = templateNodeToJson(current->headTemplate);
        if (head) json_object_set_new(layout_obj, "head", head);
        
        json_t* body = templateNodeToJson(current->bodyTemplate);
        if (body) json_object_set_new(layout_obj, "body", body);
        
        json_array_append_new(layouts, layout_obj);
        current = current->next;
    }
    
    return layouts;
}

static json_t* pageNodeToJson(const PageNode* current) {
    if (!current) return json_null();
    
    json_t* pages = json_array();
    
    while (current) {
        json_t* page = json_object();
        if (current->identifier) json_object_set_new(page, "name", json_string(current->identifier));
        if (current->route) json_object_set_new(page, "route", json_string(current->route));
        if (current->layout) json_object_set_new(page, "layout", json_string(current->layout));
        if (current->title) json_object_set_new(page, "title", json_string(current->title));
        if (current->description) json_object_set_new(page, "description", json_string(current->description));
        if (current->method) json_object_set_new(page, "method", json_string(current->method));
        if (current->redirect) json_object_set_new(page, "redirect", json_string(current->redirect));
        
        json_t* error = responseBlockToJson(current->errorBlock);
        if (error) json_object_set_new(page, "error", error);
        
        json_t* success = responseBlockToJson(current->successBlock);
        if (success) json_object_set_new(page, "success", success);
        
        if (current->template) json_object_set_new(page, "template", templateNodeToJson(current->template));
        if (current->fields) json_object_set_new(page, "fields", apiFieldsToJson(current->fields));
        if (current->pipeline) json_object_set_new(page, "pipeline", pipelineToJson(current->pipeline));
        
        json_array_append_new(pages, page);
        current = current->next;
    }
    
    return pages;
}

static json_t* pipelineToJson(const PipelineStepNode* pipeline) {
    if (!pipeline) return json_null();
    
    json_t* steps = json_array();
    const PipelineStepNode* current = pipeline;
    
    while (current) {
        json_t* step = json_object();
        
        switch (current->type) {
            case STEP_JQ:
                json_object_set_new(step, "type", json_string("jq"));
                break;
            case STEP_LUA:
                json_object_set_new(step, "type", json_string("lua"));
                break;
            case STEP_SQL:
                json_object_set_new(step, "type", json_string("sql"));
                break;
            case STEP_DYNAMIC_SQL:
                json_object_set_new(step, "type", json_string("dynamic_sql"));
                break;
        }
        
        if (current->code) {
            json_object_set_new(step, "code", json_string(current->code));
        }
        if (current->name) {
            json_object_set_new(step, "name", json_string(current->name));
        }
        json_object_set_new(step, "is_dynamic", json_boolean(current->is_dynamic));
        
        json_array_append_new(steps, step);
        current = current->next;
    }
    
    return steps;
}

static json_t* stylePropsToJson(const StylePropNode* props) {
    if (!props) return NULL;
    
    json_t* props_obj = json_object();
    const StylePropNode* current = props;
    
    while (current) {
        json_object_set_new(props_obj, current->property, json_string(current->value));
        current = current->next;
    }
    
    return props_obj;
}

static json_t* styleBlocksToJson(const StyleBlockNode* blocks) {
    if (!blocks) return NULL;
    
    json_t* styles = json_array();
    const StyleBlockNode* current = blocks;
    
    while (current) {
        json_t* block = json_object();
        if (current->selector) json_object_set_new(block, "selector", json_string(current->selector));
        
        json_t* props = stylePropsToJson(current->propHead);
        if (props) json_object_set_new(block, "properties", props);
        
        json_array_append_new(styles, block);
        current = current->next;
    }
    
    return styles;
}

static json_t* apiFieldsToJson(const ApiField* fields) {
    if (!fields) return json_null();
    
    json_t* json = json_object();
    const ApiField* current = fields;
    
    while (current) {
        json_t* field = json_object();
        if (current->type) json_object_set_new(field, "type", json_string(current->type));
        if (current->format) json_object_set_new(field, "format", json_string(current->format));
        json_object_set_new(field, "required", json_boolean(current->required));
        if (current->minLength || current->maxLength) {
            json_t* length = json_object();
            json_object_set_new(length, "min", json_integer(current->minLength));
            json_object_set_new(length, "max", json_integer(current->maxLength));
            json_object_set_new(field, "length", length);
        }
        
        json_object_set_new(json, current->name, field);
        current = current->next;
    }
    
    return json;
}

static json_t* apiEndpointsToJson(const ApiEndpoint* endpoints) {
    if (!endpoints) return NULL;
    
    json_t* apis = json_array();
    const ApiEndpoint* current = endpoints;
    
    while (current) {
        json_t* endpoint = json_object();
        if (current->route) json_object_set_new(endpoint, "route", json_string(current->route));
        if (current->method) json_object_set_new(endpoint, "method", json_string(current->method));
        
        json_t* pipeline = pipelineToJson(current->pipeline);
        if (pipeline) json_object_set_new(endpoint, "pipeline", pipeline);
        
        json_t* fields = apiFieldsToJson(current->apiFields);
        if (fields) json_object_set_new(endpoint, "fields", fields);
        
        json_array_append_new(apis, endpoint);
        current = current->next;
    }
    
    return apis;
}

static json_t* queryParamsToJson(const QueryParam* params) {
    if (!params) return NULL;
    
    json_t* params_array = json_array();
    const QueryParam* current = params;
    
    while (current) {
        json_t* param = json_object();
        if (current->name) json_object_set_new(param, "name", json_string(current->name));
        json_array_append_new(params_array, param);
        current = current->next;
    }
    
    return params_array;
}

static json_t* queriesToJson(const QueryNode* queries) {
    if (!queries) return NULL;
    
    json_t* queries_array = json_array();
    const QueryNode* current = queries;
    
    while (current) {
        json_t* query = json_object();
        if (current->name) json_object_set_new(query, "name", json_string(current->name));
        if (current->sql) json_object_set_new(query, "sql", json_string(current->sql));
        
        json_t* params = queryParamsToJson(current->params);
        if (params) json_object_set_new(query, "params", params);
        
        json_array_append_new(queries_array, query);
        current = current->next;
    }
    
    return queries_array;
}

static json_t* transformsToJson(const TransformNode* transforms) {
    if (!transforms) return NULL;
    
    json_t* transforms_array = json_array();
    const TransformNode* current = transforms;
    
    while (current) {
        json_t* transform = json_object();
        if (current->name) json_object_set_new(transform, "name", json_string(current->name));
        if (current->code) json_object_set_new(transform, "code", json_string(current->code));
        json_object_set_new(transform, "type", json_integer(current->type));
        
        json_array_append_new(transforms_array, transform);
        current = current->next;
    }
    
    return transforms_array;
}

static json_t* scriptsToJson(const ScriptNode* scripts) {
    if (!scripts) return NULL;
    
    json_t* scripts_array = json_array();
    const ScriptNode* current = scripts;
    
    while (current) {
        json_t* script = json_object();
        if (current->name) json_object_set_new(script, "name", json_string(current->name));
        if (current->code) json_object_set_new(script, "code", json_string(current->code));
        json_object_set_new(script, "type", json_integer(current->type));
        
        json_array_append_new(scripts_array, script);
        current = current->next;
    }
    
    return scripts_array;
}

static json_t* routeMapToJson(const RouteMap* routes) {
    if (!routes) return NULL;
    
    json_t* routes_array = json_array();
    const RouteMap* current = routes;
    
    while (current) {
        json_t* route = json_object();
        if (current->route) json_object_set_new(route, "route", json_string(current->route));
        
        json_array_append_new(routes_array, route);
        current = current->next;
    }
    
    return routes_array;
}

static json_t* layoutMapToJson(const LayoutMap* layouts) {
    if (!layouts) return NULL;
    
    json_t* layouts_array = json_array();
    const LayoutMap* current = layouts;
    
    while (current) {
        json_t* layout = json_object();
        if (current->identifier) json_object_set_new(layout, "identifier", json_string(current->identifier));
        
        json_array_append_new(layouts_array, layout);
        current = current->next;
    }
    
    return layouts_array;
}

static json_t* includeNodesToJson(const IncludeNode* includes) {
    if (!includes) return NULL;
    
    json_t* includes_array = json_array();
    const IncludeNode* current = includes;
    
    while (current) {
        json_t* include = json_object();
        if (current->filepath) json_object_set_new(include, "filepath", json_string(current->filepath));
        json_object_set_new(include, "line", json_integer(current->line));
        
        json_array_append_new(includes_array, include);
        current = current->next;
    }
    
    return includes_array;
}

static json_t* responseBlockToJson(const ResponseBlockNode* block) {
    if (!block) return json_null();
    
    json_t* json = json_object();
    if (block->redirect) {
        json_object_set_new(json, "redirect", json_string(block->redirect));
    }
    if (block->template) {
        json_object_set_new(json, "template", templateNodeToJson(block->template));
    }
    return json;
}

char* websiteToJson(Arena *arena, const WebsiteNode* website) {
    if (!website) return NULL;
    
    json_t* root = json_object();
    
    if (website->name) json_object_set_new(root, "name", json_string(website->name));
    if (website->author) json_object_set_new(root, "author", json_string(website->author));
    if (website->version) json_object_set_new(root, "version", json_string(website->version));
    if (website->baseUrl) json_object_set_new(root, "baseUrl", json_string(website->baseUrl));
    
    char *dbUrl = resolveString(arena, &website->databaseUrl);
    if (dbUrl) {
        json_object_set_new(root, "databaseUrl", json_string(dbUrl));
    }
    
    int portNum;
    if (resolveNumber(&website->port, &portNum)) {
        json_object_set_new(root, "port", json_integer(portNum));
    }
    
    json_t* pages = pageNodeToJson(website->pageHead);
    if (pages) json_object_set_new(root, "pages", pages);
    
    json_t* styles = styleBlocksToJson(website->styleHead);
    if (styles) json_object_set_new(root, "styles", styles);
    
    json_t* layouts = layoutToJson(website->layoutHead);
    if (layouts) json_object_set_new(root, "layouts", layouts);
    
    json_t* apis = apiEndpointsToJson(website->apiHead);
    if (apis) json_object_set_new(root, "apis", apis);
    
    json_t* queries = queriesToJson(website->queryHead);
    if (queries) json_object_set_new(root, "queries", queries);
    
    json_t* transforms = transformsToJson(website->transformHead);
    if (transforms) json_object_set_new(root, "transforms", transforms);
    
    json_t* scripts = scriptsToJson(website->scriptHead);
    if (scripts) json_object_set_new(root, "scripts", scripts);
    
    json_t* routes = routeMapToJson(website->routeMap);
    if (routes) json_object_set_new(root, "routes", routes);
    
    json_t* layout_map = layoutMapToJson(website->layoutMap);
    if (layout_map) json_object_set_new(root, "layoutMap", layout_map);
    
    json_t* includes = includeNodesToJson(website->includeHead);
    if (includes) json_object_set_new(root, "includes", includes);
    
    char* json_str = json_dumps(root, JSON_INDENT(2));
    json_decref(root);
    
    return json_str;
}
