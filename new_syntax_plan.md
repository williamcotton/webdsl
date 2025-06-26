Of course. Here is a complete and comprehensive plan for rewriting the WebDSL codebase to implement the new flow-based syntax.

### **Project Goal**

The primary objective is to transition the WebDSL language from its current block-based configuration to a more modern, readable, and maintainable flow-based syntax using pipe operators (`|>`). This rewrite must be accomplished **without any modifications to the existing Abstract Syntax Tree (AST) structure defined in `ast.h`**. The project involves a complete overhaul of the lexer and parser, a migration of all test files to the new syntax, and an update to all relevant documentation. DO NOT WORRY ABOUT BACKWARDS COMPATIBILITY. DO NOT IMPLEMENT BACKWARDS COMPATIBILITY.

---

### **Phase 1: Core Compiler Rewrite**

This phase focuses on updating the compiler's frontend—the lexer and parser—to understand and process the new syntax.

#### **Step 1: Lexer Modification (`lexer.c`, `lexer.h`)**

The lexer is the first component that needs to be adapted. It must be updated to recognize the new syntactic elements.

* **Token Definition (`lexer.h`):**
    * Introduce new tokens to represent the core elements of the flow-based syntax:
        * `TOKEN_PIPE` for the pipe operator `|>`
        * `TOKEN_ARROW` for the route definition operator `->`
    * Add tokens for keywords that are now used in new contexts, such as `query`, `template`, `validate`, and `data`.
    * Review and potentially deprecate tokens that are no longer used in the new syntax to keep the lexer clean.

* **Token Recognition (`lexer.c`):**
    * The main `getNextToken()` function will be heavily modified. Its new logic will need to correctly identify and differentiate between the new operators, keywords, and existing literals.
    * Implement logic to handle the new backtick-quoted strings for multiline templates and code blocks (e.g., `template: \`...\``).
    * Update whitespace and comment handling to ensure it's compatible with the new, less-nested code style.

#### **Step 2: Parser Rewrite (`parser.c`, `parser.h`)**

This is the most critical part of the project. The parser must be re-architected to understand the new flow-based grammar and map it correctly to the *existing* AST nodes.

* **Top-Level Parsing:**
    * The main parsing function, `parseProgram()`, will be refactored. Instead of looking for a single `website` block, it will now parse a series of top-level declarations, including `website`, `layout`, `query`, `partial`, `include`, and route definitions.

* **Route and Pipeline Parsing (New Functions):**
    * A new central function, let's call it `parseRouteDefinition()`, will be created to handle route declarations (e.g., `GET /path -> layout { ... }`). This function will:
        1.  Parse the HTTP method (`GET`, `POST`, etc.).
        2.  Parse the URL pattern.
        3.  Determine if it's a page (with a layout) or an API endpoint (no layout) and create the corresponding `PageNode` or `ApiEndpoint` from the AST.
        4.  Parse the block `{...}` containing the processing pipeline.
    * A `parsePipeline()` function will be implemented to process the chain of operations connected by the `|>` operator. It will iterate through each step and:
        * Parse the step type (e.g., `data`, `jq`, `lua`, `query`, `validate`, `template`).
        * Populate the appropriate fields in the `PageNode` or `ApiEndpoint` (e.g., `template`, `fields`).
        * Construct the `PipelineStepNode` linked list for data-processing steps.

* **Declaration Parsing:**
    * Create new parsing functions for the updated declaration syntax for layouts, queries, and partials (e.g., `layout main_layout = \`...\``). These functions will create the corresponding `LayoutNode`, `QueryNode`, and `PartialNode` objects.

* **AST Compatibility:**
    * A key challenge is mapping the linear, flow-based syntax to the existing, potentially more nested, AST structure. For example, a validation block in the new syntax (`validate: { name: string(1..10) }`) must be parsed and transformed into the `ApiField` linked-list structure expected by the AST, without altering `ast.h`. This will require careful planning in the parser logic.

---

### **Phase 2: Test Suite Migration**

To ensure the new compiler works correctly and to prevent regressions, the entire test suite must be migrated to the new syntax.

* **Step 3: Unit Test Overhaul:**
    * **Lexer Tests (`test_lexer.c`):** Rewrite tests to validate the tokenization of the new syntax, including `|>`, `->`, backtick strings, and new keywords.
    * **Parser Tests (`test_parser.c`):** This will be a complete rewrite. The new tests will provide sample strings of the flow-based syntax and assert that the parser generates the correct AST structure. Test cases should cover every feature specified in `new_syntax.md`.

* **Step 4: Integration and End-to-End Test Migration:**
    * The test configuration strings in files like `test_e2e.c` and `test_server.c` are currently defined in the old WebDSL syntax. They must all be rewritten using the new flow syntax.
    * The underlying test logic (e.g., making HTTP requests and asserting on the response) will likely remain the same, but it will now be testing an application defined with the new, cleaner syntax.

* **Step 5: JSON Snapshot Tests (`test_website_json.c`):**
    * Update these tests to parse a configuration file written in the new syntax and assert that the resulting JSON output (a serialization of the AST) is correct. This is a crucial step for verifying that the new parser correctly populates the existing AST structure.

---

### **Phase 3: Documentation & Cleanup**

The final phase is to update all user-facing documentation and clean up the codebase.

* **Step 6: Documentation Update:**
    * Replace the content of `docs/syntax.md` with a comprehensive guide to the new flow-based syntax, using `new_syntax.md` as the source of truth.
    * Review `README.md`, `docs/api.md`, `docs/database.md`, and `docs/validation.md` to update all code examples to the new syntax. The goal is to completely remove any trace of the old syntax from the documentation.

* **Step 7: Code Refinement:**
    * Once the new parser is stable and all tests pass, remove any dead code (unused functions, old parsing logic) from `parser.c` and `lexer.c`.
    * Add comments to clarify any complex mapping logic between the new syntax and the old AST.

By following this structured, phased approach, the WebDSL language can be successfully migrated to the new, more expressive syntax while guaranteeing backward compatibility with the stable, underlying AST.