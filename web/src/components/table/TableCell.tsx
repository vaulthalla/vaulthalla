import React from 'react'

interface TableCellProps {
  className?: string
  children: React.ReactNode
  onClick?: () => void
  onMouseEnter?: () => void
  onMouseLeave?: () => void
}

export const TableCell: React.FC<TableCellProps> = ({
  className = '',
  children,
  onClick,
  onMouseEnter,
  onMouseLeave,
}) => (
  <td
    className={`text-md p-2 text-gray-300 ${className}`}
    onClick={onClick}
    onMouseEnter={onMouseEnter}
    onMouseLeave={onMouseLeave}>
    {children}
  </td>
)
