import React from 'react'

interface AdminPageBaseProps {
  title?: string
  description?: string
  children: React.ReactNode
}

export const AdminPage = ({ title, description, children }: AdminPageBaseProps) => {
  const Title = () => (title ? <h1 className="text-3xl font-semibold text-cyan-100">{title}</h1> : null)
  const Description = () => (description ? <p className="mt-2 text-cyan-300/80">{description}</p> : null)
  const Header = () =>
    (title || description) && (
      <div className="mb-1">
        <Title />
        <Description />
      </div>
    )

  return (
    <div>
      <Header />
      {children}
    </div>
  )
}
