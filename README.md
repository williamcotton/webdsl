![Test Suite](https://github.com/williamcotton/webdsl/workflows/Test%20Suite/badge.svg)

# WebDSL

WebDSL is an experimental domain-specific language and server implementation for building web applications with integrated PostgreSQL, Lua, jq, and mustache. It provides a mainly declarative way to define websites with pages and API endpoints.

## Examples

### API Endpoint

```webdsl
website {
    port 3123
    database "postgresql://localhost/test"
    api {
        route "/api/v1/team"
        method "GET"
        pipeline {
            jq { { params: [.query.id] } }
            sql { SELECT * FROM teams WHERE id = $1 }
            jq { { data: (.rows | map({id: .id, name: .name})) } }
        }
    }
}
```

### HTMX Demo

```webdsl
layout {
    name "htmx"
    mustache {
        <!DOCTYPE html>
        <html>
            <head>
                <script src="https://unpkg.com/htmx.org@1.9.10"></script>
                <script src="https://cdn.tailwindcss.com"></script>
                <link rel="stylesheet" href="/styles.css">
                <title>{{pageTitle}}</title>
                {{head}}
            </head>
            <body class="bg-gray-100 min-h-screen">
                <div class="container mx-auto px-4 py-8">
                    <!-- content -->
                </div>
            </body>
        </html>
    }
}

page {
    name "htmx-demo"
    route "/htmx"
    layout "htmx"
    html {
        <div class="max-w-2xl mx-auto bg-white rounded-lg shadow-md p-8">
            <h1 class="text-3xl font-bold text-gray-800 mb-6">HTMX Demo</h1>
            <button hx-get="/htmx/time" 
                    hx-target="#time-container" 
                    hx-swap="innerHTML"
                    class="bg-blue-500 hover:bg-blue-600 text-white font-semibold py-2 px-4 rounded-md transition duration-200 ease-in-out mb-4">
                Click for Server Time
            </button>
            <div id="time-container" class="p-4 bg-gray-50 rounded-md text-gray-600">
                Click the button to load the time...
            </div>
        </div>
    }
}

page {
    name "htmx-time"
    route "/htmx/time"
    pipeline {
        lua {
            local time = os.date("%H:%M:%S")
            return {
                time = time
            }
        }
    }
    mustache {
        <div class="font-medium">
            The server time is: <strong class="text-blue-600">{{time}}</strong>
        </div>
    }
} 
```

## Features

- Declarative configuration for routes, layouts, and pages
- Component-based templating with content placeholders and hot reloading
- Modular file organization with include system
- Mustache templates with conditional rendering and loops

### API Features
- REST API endpoints with field validation
- Pipeline-based request processing
- Reusable script and transform blocks
- JQ and Lua transformation steps
- External API integration with fetch support
- Comprehensive request context access (query, headers, body)

### Database Features
- Native PostgreSQL integration with connection pooling
- Query builder with fluent interface
- Automatic pagination with metadata
- Prepared statement caching
- Direct SQL execution in Lua

### Development Features
- Memory-safe arena allocation
- Thread-safe request handling
- Live configuration reloading
- Comprehensive validation rules
- Detailed error messages
- Hot module reloading

## Installation

### Using Homebrew (macOS)

```bash
# Add the WebDSL tap
brew tap williamcotton/webdsl

# Install WebDSL
brew install webdsl
```

To update to the latest version:
```bash
brew reinstall webdsl
```

## Building From Source

### Prerequisites

#### macOS
```bash
brew install \
  llvm \
  postgresql@14 \
  libmicrohttpd \
  jansson \
  jq \
  lua \
  uthash \
  libbsd \
  openssl \
  curl
```

#### Linux
```bash
sudo apt-get install -y \
  clang \
  libmicrohttpd-dev \
  libpq-dev \
  libjansson-dev \
  libjq-dev \
  liblua5.4-dev \
  postgresql-client \
  uthash-dev \
  libbsd-dev \
  libcurl4-openssl-dev \
  valgrind
```

### Building
```bash
make build/webdsl
```

### Running
```bash
./build/webdsl app.webdsl
```

## Language Examples

### Basic Website Structure
```webdsl
website {
    name "My Website"
    author "John Smith" 
    version "1.0"
    port 3123
    database "postgresql://localhost/mydb"
}
```

### Page Definition
```webdsl
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
```

### API Endpoint with Filtering
```webdsl
api {
    route "/api/v2/employees"
    method "GET"
    
    pipeline {
        lua {
            -- Transform request context
            local qb = querybuilder.new()
            local result = qb
                :select("*")
                :from("employees")
                :where_if(query.team_id, "team_id = ?", query.team_id)
                :limit(query.limit or 20)
                :offset(query.offset or 0)
                :with_metadata()
                :build()
            return result
        }
        
        executeQuery dynamic
        
        jq {
            {
                data: (.rows | map(select(.type == "data"))),
                metadata: (.rows | map(select(.type == "metadata")) | .[0])
            }
        }
    }
}
```

### Field Validation
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

## Architecture

### Request Pipeline
1. Parse incoming request
2. Build request context (query params, headers, body)
3. Validate field definitions (if specified)
4. Execute pipeline steps in sequence:
   - Each step receives previous step's output as input
   - Lua steps can access request context directly
   - JQ steps can transform JSON data
   - Dynamic SQL queries can be built and executed
   - Multiple transformations can be chained
5. Return final JSON response

## Documentation

For detailed documentation on:
- [Language Syntax](docs/syntax.md)
- [Database Integration](docs/database.md)
- [API Features](docs/api.md)
- [Validation Rules](docs/validation.md)

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## License

This project is licensed under the MIT License - see the LICENSE file for details.

