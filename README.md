# WebDSL

WebDSL is an experimental domain-specific language and server implementation for building web applications with integrated PostgreSQL database support. It provides a declarative way to define websites with layouts, pages, styles, and API endpoints.

## Features

- Declarative website configuration
- Layout system with content placeholders
- Page routing and templating 
- CSS styling with raw CSS blocks
- REST API endpoints with validation
- PostgreSQL database integration
- JQ filtering of JSON responses
- Form handling and validation
- Hot reloading of configuration changes
- CORS support
- Memory-safe arena allocation
- Pre-filtering of API request context
- Request context access (query params, headers, cookies, body)
- Advanced JQ filtering with pre and post filters
- Named query parameters
- Raw HTML and SQL blocks
- Structured and raw CSS blocks
- Form validation with multiple validation types
- Database connection pooling
- Thread-safe JQ filter caching
- Lua scripting support for request/response filtering
- Advanced database connection pooling
- Thread-safe prepared statement caching
- Support for both JQ and Lua filter pipelines
- Request context available in Lua environment
- Automatic statement preparation and caching
- Arena-based memory management for Lua state

## Prerequisites

- C compiler (clang recommended)
- libmicrohttpd
- PostgreSQL development libraries
- libjq (JQ JSON processor)
- libjansson (JSON parser)
- Make

### macOS

```bash
brew install libmicrohttpd postgresql@14 jq jansson
```

### Linux

```bash
sudo apt-get install libmicrohttpd-dev libpq-dev libjq-dev libjansson-dev
```

## Building

```bash
make build/webdsl
```

## Running

1. Create a `.webdsl` configuration file (see example below)
2. Run the server:

```bash
./build/webdsl app.webdsl
```

The server will automatically reload when changes are made to the configuration file.

## Language Features

### Website Configuration

The root `website` block defines the overall configuration:

```webdsl
website {
    name "My Website"
    author "John Smith" 
    version "1.0"
    port 3123
    database "postgresql://localhost/mydb"
    
    // Website contents...
}
```

### Layouts

Layouts provide reusable page templates with content placeholders:

```webdsl
layouts {
    "main" {
        html {
            <html>
                <head>
                    <title>My Site</title>
                </head>
                <body>
                    <nav>
                        <a href="/">Home</a>
                        <a href="/about">About</a>
                    </nav>
                    
                    <!-- content -->
                    
                    <footer>Copyright 2024</footer>
                </body>
            </html>
        }
    }
}
```

The `<!-- content -->` marker indicates where page content will be inserted.

### Pages

Pages define routes and content:

```webdsl
pages {
    page "home" {
        route "/"
        layout "main"
        content {
            h1 "Welcome!"
            p "This is my homepage"
            
            div {
                h2 "Features"
                ul {
                    li "Easy to use"
                    li "Fast and reliable"
                }
            }
        }
    }
    
    page "about" {
        route "/about"
        layout "main"
        html {
            <div class="about">
                <h1>About Us</h1>
                <p>We build awesome websites!</p>
            </div>
        }
    }
}
```

Pages can use either the structured `content` syntax or raw HTML with the `html` block.

### Styles

CSS styles can be defined in multiple ways:

```webdsl
styles {
    // Raw CSS block
    css {
        body {
            margin: 0;
            padding: 20px;
            font-family: sans-serif;
        }
        
        .container {
            max-width: 960px;
            margin: 0 auto;
        }
    }
    
    // Structured style blocks
    "h1" {
        "color" "#333"
        "font-size" "32px"
    }
    
    ".button" {
        "background" "#007bff"
        "color" "white"
        "padding" "10px 20px"
    }
}
```

### API Endpoints

API endpoints connect routes to database queries with optional validation:

```webdsl
api {
    route "/api/v1/employees"
    method "GET"
    executeQuery "employees"
    jq {
        // Transform response to include metadata
        {
            data: (.rows | map(select(.type == "data"))),
            metadata: {
                total: (.rows | map(select(.type == "metadata")) | .[0].total_count)
            }
        }
    }
}

api {
    route "/api/v1/employees" 
    method "POST"
    fields {
        "name" {
            type "string"
            required true
            length 2..100
        }
        "email" {
            type "string"
            required true
            format "email"
        }
        "team_id" {
            type "number"
            required false
        }
    }
    executeQuery "insertEmployee" [name, email, team_id]
}
```

### Lua Filters

API endpoints can use Lua scripts for pre and post filtering:

```webdsl
api {
    route "/api/v2/employees"
    method "GET"
    
    // Pre-filter using Lua
    preFilter lua {
        -- Access request context
        local team_id = query.team_id
        local offset = query.offset
        local limit = query.limit
        
        -- Handle empty/null values
        if not team_id or team_id == "" then
            team_id = "{}"
        end
        
        -- Return array of values for SQL query
        return {team_id, offset, limit}
    }
    
    executeQuery "employeesWithPagination"
    
    // Post-filter using Lua
    postFilter lua {
        -- Transform the response
        local result = {
            data = {},
            metadata = {}
        }
        
        -- Process rows (provided by API handler)
        for _, row in ipairs(rows) do
            if row.type == "data" then
                table.insert(result.data, {
                    name = row.name,
                    email = row.email,
                    team_id = row.team_id
                })
            elseif row.type == "metadata" then
                result.metadata = {
                    total = row.total_count,
                    team_id_query = row.team_id_query,
                    offset = row.offset,
                    limit = row.limit,
                    has_more = row.has_more
                }
            end
        end
        
        return result
    }
}
```

The Lua environment provides access to:
- `query` - Query parameters table
- `headers` - Request headers table
- `body` - POST request body table
- `rows` - Query result rows (in post-filter)

### Database Features

#### Connection Pooling

The server implements advanced connection pooling:

```webdsl
website {
    database "postgresql://localhost/mydb"
    // Pool settings are automatically configured:
    // - Initial pool size: 20 connections
    // - Maximum pool size: 50 connections
    // - Connection timeout: 30 seconds
}
```

Features:
- Automatic connection management
- Connection health checking
- Statement preparation and caching
- Thread-safe pool access
- Automatic reconnection
- Connection reuse

#### Prepared Statements

SQL queries are automatically prepared and cached:

```webdsl
query {
    name "insertUser"
    params [name, email, team_id]
    sql {
        INSERT INTO users (name, email, team_id)
        VALUES ($1, $2, $3)
        RETURNING id, name, email
    }
}
```

The server:
- Automatically prepares statements
- Caches prepared statements per connection
- Handles statement invalidation
- Provides parameter type safety
- Manages statement lifecycle

### Request Pipeline

The full request processing pipeline:

1. Parse incoming request
2. Build request context (query params, headers, cookies, body)
3. Execute pre-filter (JQ or Lua)
4. Validate request fields
5. Execute database query
6. Execute post-filter (JQ or Lua)
7. Return JSON response

### Database Queries

Named SQL queries that can be referenced by API endpoints:

```webdsl
query {
    name "employees"
    sql {
        SELECT * FROM (
            -- Metadata row with total count
            SELECT 'metadata' as type, COUNT(*) as total_count, 
                   NULL as id, NULL as name, NULL as email
            FROM employees
            
            UNION ALL
            
            -- Data rows with pagination
            SELECT 'data' as type, NULL as total_count,
                   id, name, email
            FROM employees 
            ORDER BY id 
            LIMIT 20
        ) results
        ORDER BY type DESC;
    }
}

query {
    name "insertEmployee"
    sql {
        INSERT INTO employees (name, email, team_id)
        VALUES ($1, $2, $3)
        RETURNING id, name, email
    }
}
```

### Field Validation

The validation system supports multiple validation types:

```webdsl
fields {
    "username" {
        type "string"
        required true
        length 3..50
    }
    
    "email" {
        type "string" 
        required true
        format "email"
    }
    
    "phone" {
        type "string"
        format "phone"
        required false
    }
    
    "age" {
        type "number"
        required true
        validate {
            range 13..120
        }
    }
    
    "website" {
        type "string"
        format "url"
    }
    
    "uuid" {
        type "string"
        format "uuid"
    }
}
```

Supported format validations:
- `email` - Valid email address
- `url` - Valid HTTP/HTTPS URL  
- `date` - Date in YYYY-MM-DD format
- `time` - Time in HH:MM or HH:MM:SS format
- `phone` - Phone number with optional +, (), -, and spaces
- `uuid` - UUID in standard format
- `ipv4` - IPv4 address

### JQ Filtering

API responses can be transformed using JQ filters:

```webdsl
api {
    route "/api/v1/data"
    method "GET" 
    executeQuery "getData"
    jq {
        {
            items: (.rows | map({
                id: .id,
                name: .name,
                // Computed fields
                fullName: "\(.first_name) \(.last_name)",
                age: (now - (.dob | fromdateiso8601) | . / (365.25 * 86400) | floor)
            })),
            metadata: {
                count: (.rows | length),
                generated: (now | todateiso8601)
            }
        }
    }
}
```

### Request Context

API endpoints have access to the full request context through pre-filters:

```webdsl
api {
    route "/api/v1/data"
    method "GET"
    preFilter jq {
        {
            // Extract values from request context
            user_id: .headers["X-User-Id"],
            team_id: .query.team_id,
            role: .cookies.role,
            filters: {
                status: (.query.status // "active"),
                limit: (.query.limit | tonumber // 20)
            }
        }
    }
    executeQuery "getData"
    filter jq {
        // Transform the response
        .rows | map(select(.status == "active"))
    }
}
```

The request context includes:
- `query` - Query parameters
- `headers` - Request headers
- `cookies` - Request cookies 
- `body` - POST request body
- `method` - Request method
- `url` - Request URL
- `version` - HTTP version

### Named Query Parameters

Queries can define named parameters that are referenced in the SQL:

```webdsl
query {
    name "searchUsers"
    params [status, team_id, limit]
    sql {
        SELECT * FROM users
        WHERE status = $1
        AND team_id = $2
        LIMIT $3
    }
}
```

The parameters are populated from the pre-filter output in order.

### Advanced JQ Filtering

APIs support both pre and post filtering of data:

```webdsl
api {
    route "/api/v1/stats"
    method "GET"
    
    // Pre-filter request context
    preFilter jq {
        {
            start_date: .query.start,
            end_date: .query.end,
            group_by: .query.grouping
        }
    }
    
    executeQuery "getStats"
    
    // Post-filter response data
    filter jq {
        .rows | group_by(.group_by) | map({
            key: .[0].group_by,
            count: length,
            total: map(.value) | add
        })
    }
}
```

### Raw Blocks

The language supports raw blocks for HTML, SQL and CSS:

```webdsl
page "about" {
    route "/about"
    html {
        <div class="about">
            <h1>About Us</h1>
            <!-- Raw HTML content -->
        </div>
    }
}

query {
    name "complexQuery"
    sql {
        WITH RECURSIVE hierarchy AS (
            -- Raw SQL query
        )
        SELECT * FROM hierarchy
    }
}

styles {
    css {
        /* Raw CSS block */
        body {
            margin: 0;
            padding: 20px;
        }
    }
}
```

## Runtime Features

- Hot reloading of configuration changes
- Memory-safe arena allocation
- Connection pooling for PostgreSQL
- CORS support
- Structured error responses
- Request validation
- JSON response transformation
- Static file serving
- Form data handling

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Architecture

Key architectural features:

- Arena-based memory management for request lifecycle
- Thread-safe connection pooling for database access
- Cached JQ filter compilation with thread-local storage
- Hot reload support with clean shutdown
- Structured error handling and validation
- CORS and security headers
- Request context building and filtering
- JSON response transformation pipeline

Key components:

- Thread-local JQ filter cache
- Lua state management with arena allocation
- Prepared statement caching per connection
- Connection pool with health checking
- Request context building and filtering
- Automatic hot reload with clean shutdown
- Memory-safe Lua integration
- Thread-safe database operations

## Performance Features

- Connection pooling reduces database connection overhead
- Prepared statement caching improves query performance
- Thread-local caching of compiled JQ filters
- Arena allocation reduces memory fragmentation
- Efficient Lua state management
- Request context filtering optimization
- Automatic connection health checking

