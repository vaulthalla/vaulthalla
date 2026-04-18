import React from 'react'

interface ScrollAreaProps {
  className?: string
  children: React.ReactNode
}

export const ScrollArea: React.FC<ScrollAreaProps> = ({ className = '', children }) => {
  return (
    <div className={`overflow-auto ${className} scrollbar-thin scrollbar-thumb-gray-700 scrollbar-track-gray-900`}>
      {children}
    </div>
  )
}
