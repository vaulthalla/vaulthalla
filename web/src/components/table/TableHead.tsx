import React from 'react'

interface TableHeadProps {
  className?: string
  children: React.ReactNode
}

export const TableHead: React.FC<TableHeadProps> = ({ className = '', children }) => (
  <th className={`p-2 text-sm font-semibold text-gray-400 ${className}`}>{children}</th>
)
