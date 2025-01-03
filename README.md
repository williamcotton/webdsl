# WebDSL

WebDSL is an experimental domain-specific language and server implementation for building web applications with integrated PostgreSQL database support. It provides a declarative way to define websites with layouts, pages, styles, and API endpoints.

## Features

### Core Features
- Declarative website configuration
- Layout system with content placeholders
- Page routing and templating
- CSS styling with raw and structured blocks
- REST API endpoints with validation
- Hot reloading of configuration changes

### Database Features
- PostgreSQL integration with connection pooling
- Prepared statement caching
- Named query parameters
- Automatic connection health checking

### Data Processing
- JQ and Lua filtering pipelines
- Request context filtering
- JSON response transformation
- Form validation with multiple types

### Performance & Safety
- Memory-safe arena allocation
- Thread-safe operations
- Connection pooling
- Filter compilation caching

## Quick Start

### Prerequisites

#### macOS
```bash
brew install libmicrohttpd postgresql@14 jq jansson lua
```

#### Linux
```bash
sudo apt-get install libmicrohttpd-dev libpq-dev libjq-dev libjansson-dev liblua5.4-dev
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
    
    preFilter lua {
        -- Transform request context
        local team_id = query.team_id or "{}"
        return {team_id, query.offset, query.limit}
    }
    
    executeQuery "employeesWithPagination"
    
    postFilter jq {
        {
            data: (.rows | map(select(.type == "data"))),
            metadata: (.rows | map(select(.type == "metadata")) | .[0])
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
2. Build request context
3. Execute pre-filter (JQ/Lua)
4. Validate fields
5. Execute database query
6. Execute post-filter (JQ/Lua)
7. Return JSON response

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

