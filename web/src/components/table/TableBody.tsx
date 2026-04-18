import React from 'react'

interface TableBodyProps {
  className?: string
  children: React.ReactNode
}

export const TableBody: React.FC<TableBodyProps> = ({ className = '', children }) => (
  <tbody className={className}>{children}</tbody>
)
