# WebDSL Syntax Guide

WebDSL is a domain-specific language designed for building web applications. This guide covers the core syntax elements and structures.

## Basic Structure

### Website Configuration
```webdsl
website {
    name "My Website"
    author "John Smith" 
    version "1.0"
    port 3123
    database "postgresql://localhost/mydb"
    include "other-file.webdsl"
}
```

### Pages
```webdsl
page {
    name "page-name"
    route "/path"
    layout "layout-name"
    method "GET"  // Optional, defaults to GET
    pipeline {
        // Data processing steps
    }
    mustache {
        // Template content
    }
}
```

### Layouts
```webdsl
layout {
    name "layout-name"
    mustache {
        <!DOCTYPE html>
        <html>
            <head>
                <title>{{pageTitle}}</title>
            </head>
            <body>
                <!-- content -->
            </body>
        </html>
    }
}
```

### API Endpoints
```webdsl
api {
    route "/api/v1/resource"
    method "GET"
    pipeline {
        // Processing steps
    }
}
```

## Pipeline Components

### JQ Transformations
```webdsl
jq {
    {
        data: (.rows | map({id: .id, name: .name})),
        metadata: {
            total: .total_count,
            offset: .offset
        }
    }
}
```

### Lua Scripts
```webdsl
lua {
    local result = sqlQuery("SELECT * FROM table")
    return {
        data = result.rows
    }
}
```

### SQL Queries
```webdsl
sql {
    SELECT * FROM table
    WHERE id = $1
}
```

## Field Validation
```webdsl
fields {
    "email" {
        type "string"
        required true
        format "email"
    }
    "age" {
        type "number"
        validate {
            range 13..120
        }
    }
}
```

## Partials
```webdsl
partial {
    name "component-name"
    mustache {
        // Reusable template content
    }
}
```

## Styles
```webdsl
styles {
    css {
        body {
            background: #ffffff;
            color: #333;
        }
    }
}
```

## Query Builder
```webdsl
lua {
    local qb = querybuilder.new()
    local result = qb
        :select("*")
        :from("table")
        :where_if(condition, "field = ?", value)
        :limit(limit)
        :offset(offset)
        :build()
}
```

## Reference Data
```webdsl
referenceData {
    lua {
        // Data available to templates
        return { key = value }
    }
}
```

## Success/Error Handlers
```webdsl
success {
    mustache {
        // Template for success response
    }
}

error {
    mustache {
        // Template for error response
    }
}
```

## HTMX Integration
WebDSL has built-in support for HTMX attributes:
```webdsl
mustache {
    <button hx-get="/api/endpoint" 
            hx-target="#element"
            hx-swap="innerHTML">
        Click me
    </button>
}
``` 