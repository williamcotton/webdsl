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
    jsonResponse "employees"
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
    jsonResponse "insertEmployee" [name, email, team_id]
}
```

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
    jsonResponse "getData"
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

