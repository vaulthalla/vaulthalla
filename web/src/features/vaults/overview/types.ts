// Raw stats.vault.* payloads as the daemon sends them. Every field is optional: a missing value renders as
// "not available", never as 0. Statuses are backend vocabulary and only ever become tones through severityTone().

type N = number | null | undefined
type S = string | null | undefined
type When = number | string | null | undefined

export interface CapacityStats {
  capacity?: N
  logical_size?: N
  physical_size?: N
  cache_size?: N
  free_space?: N
  file_count?: N
  directory_count?: N
  average_file_size?: N
  largest_file_size?: N
  top_file_extensions?: Record<string, number> | null
}

export interface VaultStatsPayload {
  capacity?: CapacityStats | null
}

export interface SyncHealthStats {
  overall_status?: S
  current_state?: S
  sync_enabled?: boolean | null
  sync_interval_seconds?: N
  configured_strategy?: S
  conflict_policy?: S
  s3_budget_warning?: S
  s3_budget_unlimited_legacy?: boolean | null
  last_sync_at?: When
  last_success_at?: When
  last_success_age_seconds?: N
  active_run_count?: N
  pending_run_count?: N
  running_run_count?: N
  stalled_run_count?: N
  error_count_24h?: N
  error_count_7d?: N
  failed_ops_24h?: N
  retry_count_24h?: N
  conflict_count_open?: N
  conflict_count_24h?: N
  bytes_up_24h?: N
  bytes_down_24h?: N
  bytes_total_24h?: N
  bytes_total_7d?: N
  divergence_detected?: boolean | null
  hash_mismatch?: boolean | null
  remote_index_object_count?: N
  remote_index_stale?: boolean | null
  remote_index_indexed_at?: When
  last_error_code?: S
  last_error_message?: S
  last_stall_reason?: S
  checked_at?: When
}

export interface RecoveryStats {
  recovery_readiness?: S
  backup_status?: S
  backup_enabled?: boolean | null
  backup_policy_present?: boolean | null
  backup_interval_seconds?: N
  backup_stale?: boolean | null
  last_backup_at?: When
  last_success_at?: When
  next_expected_backup_at?: When
  missed_backup_count_estimate?: N
  retention_seconds?: N
  retry_count?: N
  last_error?: S
  checked_at?: When
}

export interface SecurityStats {
  overall_status?: S
  encryption_status?: S
  current_key_version?: N
  key_age_days?: N
  key_created_at?: When
  file_count?: N
  files_current_key_version?: N
  files_legacy_key_version?: N
  files_unknown_key_version?: N
  trashed_key_versions_count?: N
  integrity_check_status?: S
  last_integrity_check_at?: When
  checksum_mismatch_count?: N
  unauthorized_access_attempts_24h?: N
  rate_limited_attempts_24h?: N
  last_denied_access_at?: When
  last_denied_access_reason?: S
  last_permission_change_at?: When
  checked_at?: When
}

export interface ActivityEvent {
  source?: S
  action?: S
  path?: S
  user_id?: N
  user_name?: S
  status?: S
  error?: S
  bytes?: N
  occurred_at?: When
}

export interface ActivityStats {
  uploads_24h?: N
  uploads_7d?: N
  deletes_24h?: N
  deletes_7d?: N
  moves_24h?: N
  moves_7d?: N
  renames_24h?: N
  renames_7d?: N
  copies_24h?: N
  copies_7d?: N
  restores_24h?: N
  restores_7d?: N
  bytes_added_24h?: N
  bytes_removed_24h?: N
  last_activity_at?: When
  last_activity_action?: S
  recent_activity?: ActivityEvent[] | null
  top_active_users?: { user_id?: N; user_name?: S; count?: N }[] | null
  top_touched_paths?: { path?: S; action?: S; count?: N; bytes?: N }[] | null
  checked_at?: When
}

export interface ShareStats {
  active_links?: N
  public_links?: N
  email_validated_links?: N
  expired_links?: N
  revoked_links?: N
  links_created_24h?: N
  links_revoked_24h?: N
  downloads_24h?: N
  uploads_24h?: N
  denied_attempts_24h?: N
  failed_attempts_24h?: N
  rate_limited_attempts_24h?: N
  top_links_by_access?: { share_id?: S; label?: S; root_path?: S; access_count?: N; download_count?: N }[] | null
  checked_at?: When
}

export interface OperationStats {
  overall_status?: S
  pending_operations?: N
  in_progress_operations?: N
  stalled_operations?: N
  failed_operations_24h?: N
  cancelled_operations_24h?: N
  active_share_uploads?: N
  stalled_share_uploads?: N
  failed_share_uploads_24h?: N
  oldest_pending_operation_age_seconds?: N
  oldest_in_progress_operation_age_seconds?: N
  upload_bytes_expected_active?: N
  upload_bytes_received_active?: N
  recent_operation_errors?: { operation?: S; status?: S; path?: S; target?: S; error?: S; occurred_at?: When }[] | null
  checked_at?: When
}

export interface RetentionStats {
  cleanup_status?: S
  trashed_files_count?: N
  trashed_bytes_total?: N
  trashed_files_past_retention_count?: N
  trashed_bytes_past_retention?: N
  trash_retention_days?: N
  oldest_trashed_age_seconds?: N
  cache_entries_total?: N
  cache_entries_expired?: N
  cache_bytes_total?: N
  cache_max_size_bytes?: N
  cache_eviction_candidates?: N
  cache_expiry_days?: N
  sync_events_total?: N
  sync_events_past_retention_count?: N
  sync_event_retention_days?: N
  audit_log_entries_total?: N
  audit_log_entries_past_retention_count?: N
  audit_log_retention_days?: N
  checked_at?: When
}

export interface PricingStats {
  currency?: S
  current_monthly_spend?: string | number | null
  projected_monthly_spend?: string | number | null
  active_policies?: N
  warning_notifications?: N
  critical_notifications?: N
  unacknowledged_notifications?: N
  blocked_syncs_24h?: N
  pending_overrides?: N
}

export interface TrendPoint {
  created_at: number | string
  value: number | null
}

export interface TrendSeries {
  key: string
  label: string
  unit?: string
  snapshot_type?: string
  points: TrendPoint[]
}

export interface TrendsStats {
  window_hours?: N
  series?: TrendSeries[] | null
  checked_at?: When
}
