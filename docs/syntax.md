# WebDSL Syntax Guide

## Basic Structure

A WebDSL file consists of a root `website` block containing various configuration sections:

```webdsl
website {
    // Website configuration
    name "My Website"
    author "John Smith"
    version "1.0"
    port 3123
    database "postgresql://localhost/mydb"

    // Content sections
    layouts { ... }
    pages { ... }
    styles { ... }
    api { ... }
    query { ... }
}
```

## Layouts

Layouts define reusable page templates:

```webdsl
layouts {
    "main" {
        html {
            <html>
                <head>
                    <title>My Site</title>
                </head>
                <body>
                    <!-- content -->
                </body>
            </html>
        }
    }
}
```

The `<!-- content -->` marker indicates where page content will be inserted.

## Pages

Pages define routes and content:

```webdsl
pages {
    page "home" {
        route "/"
        layout "main"
        content {
            h1 "Welcome"
            p "Content here"
        }
    }

    page "about" {
        route "/about"
        layout "main"
        html {
            <div class="about">
                <h1>About Us</h1>
            </div>
        }
    }
}
```

## Mustache Templates

Pages can use Mustache templates with dynamic data:

```webdsl
page {
    name "employees"
    route "/employees"
    pipeline {
        jq {
            {
                employees: .data[0].rows,
                hasEmployees: (.data[0].rows | length > 0)
            }
        }
    }
    mustache {
        <h2>Employees</h2>
        {{#hasEmployees}}
        <ul>
            {{#employees}}
            <li>{{name}}</li>
            {{/employees}}
        </ul>
        {{/hasEmployees}}
        {{^hasEmployees}}
        <p>No employees found.</p>
        {{/hasEmployees}}
    }
}
```

### Template Features
- Conditional rendering with `{{#condition}}` and `{{^condition}}`
- Loop iteration with `{{#array}}`
- Variable interpolation with `{{variable}}`
- Nested object access with `{{object.property}}`

## Enhanced Layout System

### Content Placeholders
```webdsl
layout {
    name "master"
    html {
        <html>
            <head>
                <title>{{title}}</title>
            </head>
            <body>
                <nav>
                    <!-- Navigation content -->
                </nav>
                <!-- content -->
                <footer>
                    <!-- Footer content -->
                </footer>
            </body>
        </html>
    }
}
```

### Layout Inheritance
```webdsl
page {
    name "blog"
    route "/blog"
    layout "master"  // Inherits from master layout
    content {
        // Page-specific content
    }
}
```

## File Organization

### Include System
```webdsl
website {
    include "api.webdsl"    // Include API definitions
    include "pages.webdsl"  // Include page definitions
}
```

This allows for modular organization of:
- API endpoints
- Page definitions
- Layout templates
- Query definitions
- Style definitions

## Styles

CSS can be defined in two ways:

### Structured Blocks
```webdsl
styles {
    "body" {
        "background-color" "#fff"
        "color" "#000"
    }
}
```

### Raw CSS
```webdsl
styles {
    css {
        body {
            background: #fff;
            color: #000;
        }
    }
}
```

## Content Elements

Content can be defined using:

### Basic Elements
```webdsl
h1 "Title"
p "Paragraph"
div {
    // Nested content
}
```

### Links
```webdsl
link "/about" "About Us"
```

### Images
```webdsl
image "/logo.png" "Company Logo"
```

### Raw HTML
```webdsl
html {
    <div class="custom">
        <span>Custom HTML</span>
    </div>
}
```

## Comments

Single-line comments:
```webdsl
// This is a comment
```

## Best Practices

1. Use meaningful identifiers for pages and layouts
2. Keep layouts focused on structure
3. Use raw HTML blocks sparingly
4. Prefer structured CSS when possible
5. Group related pages together
6. Use consistent indentation
7. Comment complex configurations

## Common Patterns

### Master Layout
```webdsl
layouts {
    "master" {
        html {
            <html>
                <head>
                    <title>Site Title</title>
                    <meta charset="utf-8">
                    <meta name="viewport" content="width=device-width">
                </head>
                <body>
                    <nav>
                        <a href="/">Home</a>
                        <a href="/about">About</a>
                    </nav>
                    <!-- content -->
                    <footer>
                        <p>Copyright 2024</p>
                    </footer>
                </body>
            </html>
        }
    }
}
```

### Form Page
```webdsl
page "contact" {
    route "/contact"
    layout "master"
    html {
        <form action="/api/contact" method="POST">
            <div class="form-group">
                <label for="name">Name:</label>
                <input type="text" id="name" name="name" required>
            </div>
            <div class="form-group">
                <label for="email">Email:</label>
                <input type="email" id="email" name="email" required>
            </div>
            <button type="submit">Send</button>
        </form>
    }
}
```

### Responsive Layout
```webdsl
styles {
    css {
        @media (max-width: 768px) {
            .container {
                padding: 10px;
            }
        }
    }
}
``` 