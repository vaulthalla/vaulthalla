import { FSEntry } from '@/models/fsEntry'

export interface IFile extends FSEntry {
  size_bytes: number
  mime_type?: string
}

export class File implements IFile {
  id: number = 0
  vault_id: number = 0
  parent_id?: number
  name: string = ''
  created_by: number = 0
  created_at: number = 0
  updated_at: number = 0
  last_modified_by?: number
  size_bytes: number = 0
  path?: string
  mime_type?: string

  constructor(data?: Partial<IFile>) {
    if (data) Object.assign(this, data)
  }
}

export interface IFileUpload {
  vault_id: number
  path: string
  size?: number
}
