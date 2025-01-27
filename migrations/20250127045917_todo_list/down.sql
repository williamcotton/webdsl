-- Write your migration down SQL here

-- Drop the trigger first
DROP TRIGGER IF EXISTS update_todos_updated_at ON todos;

-- Drop the trigger function
DROP FUNCTION IF EXISTS update_updated_at_column();

-- Drop the todos table
DROP TABLE IF EXISTS todos;
