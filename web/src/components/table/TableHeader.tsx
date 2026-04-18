import React from 'react'

interface TableHeaderProps {
  className?: string
  children: React.ReactNode
}

export const TableHeader: React.FC<TableHeaderProps> = ({ className = '', children }) => (
  <thead className={`bg-gray-800 ${className}`}>{children}</thead>
)
