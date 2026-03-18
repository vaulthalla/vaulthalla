DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1
        FROM pg_type
        WHERE typname = 'permission_category'
    ) THEN
CREATE TYPE permission_categories AS ENUM ('admin', 'vault');
END IF;
END
$$;
