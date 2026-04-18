import React from 'react'

interface TableProps {
  className?: string
  children: React.ReactNode
}

export const Table: React.FC<TableProps> = ({ className = '', children }) => (
  <table className={`w-full border-collapse text-left ${className}`}>{children}</table>
)
