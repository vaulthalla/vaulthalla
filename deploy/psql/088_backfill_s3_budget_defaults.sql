-- One-time legacy upgrade backfill: rows created before S3 budget columns
-- existed must not retain an unbounded LIST fallback after upgrade.
UPDATE rsync
SET s3_budget_list_requests = 100,
    s3_budget_head_requests = 1000,
    s3_budget_get_requests = 1000,
    s3_budget_put_requests = 1000,
    s3_budget_copy_requests = 100,
    s3_budget_delete_requests = 1000,
    s3_budget_downloaded_bytes = 10737418240
WHERE s3_budget_list_requests IS NULL
  AND s3_budget_head_requests IS NULL
  AND s3_budget_get_requests IS NULL
  AND s3_budget_put_requests IS NULL
  AND s3_budget_copy_requests IS NULL
  AND s3_budget_delete_requests IS NULL
  AND s3_budget_downloaded_bytes IS NULL;
