DO $$
DECLARE
    had_alertable_fuse_errors BOOLEAN;
BEGIN
    SELECT EXISTS (
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = current_schema()
          AND table_name = 'stats_fuse_op_sample'
          AND column_name = 'alertable_error_delta'
    ) INTO had_alertable_fuse_errors;

    ALTER TABLE stats_fuse_op_sample
        ADD COLUMN IF NOT EXISTS expected_error_delta BIGINT NOT NULL DEFAULT 0 CHECK (expected_error_delta >= 0),
        ADD COLUMN IF NOT EXISTS alertable_error_delta BIGINT NOT NULL DEFAULT 0 CHECK (alertable_error_delta >= 0),
        ADD COLUMN IF NOT EXISTS expected_error_rate DOUBLE PRECISION,
        ADD COLUMN IF NOT EXISTS alertable_error_rate DOUBLE PRECISION;

    IF NOT had_alertable_fuse_errors THEN
        UPDATE stats_fuse_op_sample
        SET alertable_error_delta = error_delta,
            alertable_error_rate = error_rate
        WHERE error_delta > 0 OR error_rate IS NOT NULL;
    END IF;
END $$;
