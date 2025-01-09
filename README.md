![Test Suite](https://github.com/williamcotton/webdsl/workflows/Test%20Suite/badge.svg)

# WebDSL

WebDSL is an experimental domain-specific language and server implementation for building web applications with integrated PostgreSQL database support. It provides a declarative way to define websites with layouts, pages, styles, and API endpoints.

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

## Features

- Declarative configuration for routes, layouts, and pages
- Component-based templating with content placeholders and hot reloading
- Built-in CSS styling with both raw and structured syntax support
- REST API endpoints with field validation and pipeline-based processing
- JQ and Lua transformation steps for request/response handling
- Native PostgreSQL integration with connection pooling and prepared statement caching
- Memory-safe arena allocation and thread-safe request handling
- Comprehensive validation rules with detailed error messages
- Live configuration reloading for rapid development

## Quick Start

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

### Key Components
- Thread-local filter caching
- Connection pooling with health checks
- Prepared statement caching
- Arena-based memory management
- Hot reload support
- CORS and security headers

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

