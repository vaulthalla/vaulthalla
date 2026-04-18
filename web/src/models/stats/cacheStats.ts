export interface IOpCacheStats {
  count: number
  total_us: number
  max_us: number
}

export interface ICacheStats {
  hits: number
  misses: number
  evictions: number
  inserts: number
  invalidations: number
  bytes_read: number
  bytes_written: number
  used_bytes: number
  capacity_bytes: number
  op: IOpCacheStats
}

export class OpCacheStats implements IOpCacheStats {
  count: number = 0
  total_us: number = 0
  max_us: number = 0

  constructor(data?: Partial<ICacheStats>) {
    if (data) Object.assign(this, data)
  }
}

export class CacheStats implements ICacheStats {
  hits: number = 0
  misses: number = 0
  evictions: number = 0
  inserts: number = 0
  invalidations: number = 0
  bytes_read: number = 0
  bytes_written: number = 0
  used_bytes: number = 0
  capacity_bytes: number = 0
  op: IOpCacheStats = new OpCacheStats()

  constructor(data: Partial<ICacheStats>) {
    if (data) Object.assign(this, data)
  }
}
