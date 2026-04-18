import React from 'react'

interface TableRowProps {
  className?: string
  children: React.ReactNode
}

export const TableRow: React.FC<TableRowProps> = ({ className = '', children }) => (
  <tr className={`transition-colors hover:bg-gray-700 ${className}`}>{children}</tr>
)
