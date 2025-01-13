#include "website_json.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
#include <jansson.h>
#pragma clang diagnostic pop
#include <string.h>

static json_t* pipelineToJson(const PipelineStepNode* pipeline) {
    if (!pipeline) return NULL;
    
    json_t* steps_array = json_array();
    const PipelineStepNode* current = pipeline;
    
    while (current) {
        json_t* step = json_object();
        json_object_set_new(step, "type", json_string(
            current->type == STEP_JQ ? "jq" :
            current->type == STEP_LUA ? "lua" :
            current->type == STEP_SQL ? "sql" : "unknown"
        ));
        if (current->code) json_object_set_new(step, "code", json_string(current->code));
        if (current->name) json_object_set_new(step, "name", json_string(current->name));
        json_object_set_new(step, "is_dynamic", json_boolean(current->is_dynamic));
        json_array_append_new(steps_array, step);
        current = current->next;
    }
    
    return steps_array;
}

static json_t* contentNodeToJson(const ContentNode* node) {
    if (!node) return NULL;
    
    json_t* content_array = json_array();
    const ContentNode* current = node;
    
    while (current) {
        json_t* content = json_object();
        json_object_set_new(content, "type", json_string(current->type));
        if (current->arg1) json_object_set_new(content, "arg1", json_string(current->arg1));
        if (current->arg2) json_object_set_new(content, "arg2", json_string(current->arg2));
        if (current->children) {
            json_t* children = contentNodeToJson(current->children);
            if (children) json_object_set_new(content, "children", children);
        }
        json_array_append_new(content_array, content);
        current = current->next;
    }
    
    return content_array;
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
    
    json_t* styles_array = json_array();
    const StyleBlockNode* current = blocks;
    
    while (current) {
        json_t* block = json_object();
        json_object_set_new(block, "selector", json_string(current->selector));
        json_t* props = stylePropsToJson(current->propHead);
        if (props) json_object_set_new(block, "properties", props);
        json_array_append_new(styles_array, block);
        current = current->next;
    }
    
    return styles_array;
}

static json_t* layoutsToJson(const LayoutNode* layouts) {
    if (!layouts) return NULL;
    
    json_t* layouts_obj = json_object();
    const LayoutNode* current = layouts;
    
    while (current) {
        json_t* layout = json_object();
        if (current->doctype) json_object_set_new(layout, "doctype", json_string(current->doctype));
        json_t* head = contentNodeToJson(current->headContent);
        if (head) json_object_set_new(layout, "head", head);
        json_t* body = contentNodeToJson(current->bodyContent);
        if (body) json_object_set_new(layout, "body", body);
        json_object_set_new(layouts_obj, current->identifier, layout);
        current = current->next;
    }
    
    return layouts_obj;
}

static json_t* pagesToJson(const PageNode* pages) {
    if (!pages) return NULL;
    
    json_t* pages_array = json_array();
    const PageNode* current = pages;
    
    while (current) {
        json_t* page = json_object();
        if (current->identifier) json_object_set_new(page, "identifier", json_string(current->identifier));
        if (current->route) json_object_set_new(page, "route", json_string(current->route));
        if (current->layout) json_object_set_new(page, "layout", json_string(current->layout));
        if (current->title) json_object_set_new(page, "title", json_string(current->title));
        if (current->description) json_object_set_new(page, "description", json_string(current->description));
        json_t* content = contentNodeToJson(current->contentHead);
        if (content) json_object_set_new(page, "content", content);
        if (current->pipeline) {
            json_t* pipeline = pipelineToJson(current->pipeline);
            if (pipeline) json_object_set_new(page, "pipeline", pipeline);
        }
        json_array_append_new(pages_array, page);
        current = current->next;
    }
    
    return pages_array;
}

static json_t* queryParamsToJson(const QueryParam* params) {
    if (!params) return NULL;
    
    json_t* params_array = json_array();
    const QueryParam* current = params;
    
    while (current) {
        json_array_append_new(params_array, json_string(current->name));
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

static json_t* apiFieldsToJson(const ApiField* fields) {
    if (!fields) return NULL;
    
    json_t* fields_obj = json_object();
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
        json_object_set_new(fields_obj, current->name, field);
        current = current->next;
    }
    
    return fields_obj;
}

static json_t* apiEndpointsToJson(const ApiEndpoint* endpoints) {
    if (!endpoints) return NULL;
    
    json_t* endpoints_array = json_array();
    const ApiEndpoint* current = endpoints;
    
    while (current) {
        json_t* endpoint = json_object();
        if (current->route) json_object_set_new(endpoint, "route", json_string(current->route));
        if (current->method) json_object_set_new(endpoint, "method", json_string(current->method));
        
        if (current->uses_pipeline) {
            json_t* pipeline = pipelineToJson(current->pipeline);
            if (pipeline) json_object_set_new(endpoint, "pipeline", pipeline);
        }
        
        json_t* fields = apiFieldsToJson(current->apiFields);
        if (fields) json_object_set_new(endpoint, "fields", fields);
        
        json_array_append_new(endpoints_array, endpoint);
        current = current->next;
    }
    
    return endpoints_array;
}

static json_t* transformsToJson(const TransformNode* transforms) {
    if (!transforms) return NULL;
    
    json_t* transforms_array = json_array();
    const TransformNode* current = transforms;
    
    while (current) {
        json_t* transform = json_object();
        if (current->name) json_object_set_new(transform, "name", json_string(current->name));
        if (current->code) json_object_set_new(transform, "code", json_string(current->code));
        json_object_set_new(transform, "type", json_string("jq"));
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
        json_object_set_new(script, "type", json_string("lua"));
        json_array_append_new(scripts_array, script);
        current = current->next;
    }
    
    return scripts_array;
}

json_t* websiteToJson(const WebsiteNode* website) {
    if (!website) return NULL;
    
    json_t* root = json_object();
    
    // Add basic website properties
    if (website->name) json_object_set_new(root, "name", json_string(website->name));
    if (website->author) json_object_set_new(root, "author", json_string(website->author));
    if (website->version) json_object_set_new(root, "version", json_string(website->version));
    if (website->baseUrl) json_object_set_new(root, "baseUrl", json_string(website->baseUrl));
    if (website->databaseUrl) json_object_set_new(root, "database", json_string(website->databaseUrl));
    if (website->port) json_object_set_new(root, "port", json_integer(website->port));
    
    // Add pages
    json_t* pages = pagesToJson(website->pageHead);
    if (pages) json_object_set_new(root, "pages", pages);
    
    // Add styles
    json_t* styles = styleBlocksToJson(website->styleHead);
    if (styles) json_object_set_new(root, "styles", styles);
    
    // Add layouts
    json_t* layouts = layoutsToJson(website->layoutHead);
    if (layouts) json_object_set_new(root, "layouts", layouts);
    
    // Add API endpoints
    json_t* api = apiEndpointsToJson(website->apiHead);
    if (api) json_object_set_new(root, "api", api);
    
    // Add queries
    json_t* queries = queriesToJson(website->queryHead);
    if (queries) json_object_set_new(root, "queries", queries);
    
    // Add transforms
    json_t* transforms = transformsToJson(website->transformHead);
    if (transforms) json_object_set_new(root, "transforms", transforms);
    
    // Add scripts
    json_t* scripts = scriptsToJson(website->scriptHead);
    if (scripts) json_object_set_new(root, "scripts", scripts);
    
    return root;
}
