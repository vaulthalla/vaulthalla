ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_price_estimate_mode TEXT DEFAULT NULL;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_price_free_tier_policy TEXT DEFAULT NULL;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_price_free_tiers_applied BOOLEAN DEFAULT NULL;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_budget_estimated_cost TEXT DEFAULT NULL;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_budget_estimated_cost_currency VARCHAR(8) DEFAULT NULL;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_budget_estimate_mode TEXT DEFAULT NULL;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_budget_free_tier_policy TEXT DEFAULT NULL;
ALTER TABLE sync_event ADD COLUMN IF NOT EXISTS s3_budget_free_tiers_applied BOOLEAN DEFAULT NULL;
