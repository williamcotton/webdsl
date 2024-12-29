# WebDSL

WebDSL is an experimental domain-specific language and server implementation for building web applications with integrated PostgreSQL database support. It provides a declarative way to define websites with layouts, pages, styles, and API endpoints.

## Features

- Declarative website configuration
- Layout system with content placeholders
- Page routing and templating
- CSS styling support
- REST API endpoints
- PostgreSQL database integration
- Hot reloading of configuration changes
- CORS support
- Form handling

## Prerequisites

- C compiler (clang recommended)
- libmicrohttpd
- PostgreSQL development libraries
- Make

### macOS

```bash
brew install libmicrohttpd postgresql@14
```

### Linux

```bash
sudo apt-get install libmicrohttpd libpq-dev
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

## Configuration Example

```webdsl
website {
    name "My Website"
    port 3123

    layouts {
        "blog" {
            html {
                <header>
                    <h1>Blog Layout</h1>
                    <nav>
                        <a href="/">Home</a> |
                        <a href="/blog">Blog</a>
                    </nav>
                </header>
                <!-- content -->
                <footer>
                    <p>Blog footer - Copyright 2024</p>
                </footer>
            }
        }
    }

    pages {
        page "blog" {
            route "/blog"
            layout "blog"
            html {
                <article>
                    <h1>Latest Posts</h1>
                    <p>Check out our latest blog posts!</p>
                    <div class="post">
                        <h2>First Post</h2>
                        <p>This is our first blog post using raw HTML.</p>
                    </div>
                </article>
            }
        }
    }
}
```

## API Example

This example demonstrates how to define an API endpoint that returns a list of employees from a PostgreSQL database.

### GET /api/v1/employees

```webdsl
website {
    name "Employee API"
    port 3123
    database "postgresql://localhost/demo"

    api {
        route "/api/v1/employees"
        method "GET"
        response "employees"
    }

    query {
        name "employees"
        sql {
            SELECT id, name, email FROM employees ORDER BY id
        }
    }
}
```

### POST /api/v1/employees

This example demonstrates how to define an API endpoint that inserts a new employee into the database.

```webdsl
website {
    name "Employee API"
    port 3123
    database "postgresql://localhost/demo"

    api {
        route "/api/v1/employees"
        method "POST"
        response "insertEmployee" [name, email, team_id]
    }

    query {
        name "insertEmployee"
        sql {
            INSERT INTO employees (name, email, team_id) VALUES ($1, $2, $3)
        }
    }
}
```

## Field Validation

The WebDSL language supports rich field validation for API endpoints:

```webdsl
api {
    route "/api/v1/users"
    method "POST"
    fields {
        "username" {
            type "string"
            required true
            length 3..50
            validate {
                match "^[a-zA-Z][a-zA-Z0-9_]{2,}$"  // Must start with letter, then alphanumeric
            }
        }
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
        "phone" {
            type "string"
            format "phone"
            required false
        }
    }
    response "createUser" [username, email]
}
```

### Available Validations

#### String Fields
- `required` - Field must be present and non-empty
- `length min..max` - String length must be between min and max characters
- `format` - Predefined format validation:
  - `email` - Valid email address
  - `url` - Valid HTTP/HTTPS URL
  - `date` - Date in YYYY-MM-DD format
  - `time` - Time in HH:MM or HH:MM:SS format
  - `phone` - Phone number with optional +, (), -, and spaces
  - `uuid` - UUID in standard format
  - `ipv4` - IPv4 address
- `validate.match` - Custom regex pattern validation

#### Number Fields
- `required` - Field must be present
- `validate.range min..max` - Number must be between min and max values

### Validation Response

When validation fails, the API returns a JSON response with validation errors:

```json
{
  "errors": {
    "username": "Must start with letter, then alphanumeric",
    "email": "Invalid email format",
    "age": "Number must be between 13 and 120"
  }
}
```

