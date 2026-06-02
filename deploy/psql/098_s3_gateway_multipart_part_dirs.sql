ALTER TABLE s3_gateway_multipart_upload
    ADD COLUMN IF NOT EXISTS parts_dir_id TEXT;

UPDATE s3_gateway_multipart_upload
SET parts_dir_id = upload_id
WHERE parts_dir_id IS NULL OR parts_dir_id = '';

ALTER TABLE s3_gateway_multipart_upload
    ALTER COLUMN parts_dir_id SET NOT NULL;

CREATE UNIQUE INDEX IF NOT EXISTS idx_s3_gateway_multipart_upload_parts_dir_id
    ON s3_gateway_multipart_upload(parts_dir_id);
