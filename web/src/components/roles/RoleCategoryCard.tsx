import Link from 'next/link'
import clsx from 'clsx'

export const RoleCategoryCard = ({
  title,
  description,
  href,
  Icon,
}: {
  title: string
  description: string
  href: string
  Icon: React.ComponentType<React.SVGProps<SVGSVGElement>>
}) => {
  return (
    <Link
      href={href}
      className={clsx(
        'group rounded-2xl border border-white/10 bg-white/5 p-5 shadow-lg backdrop-blur-xl',
        'transition hover:border-cyan-400/30 hover:bg-white/10 hover:shadow-[0_0_40px_10px_rgba(100,255,255,0.08)]',
      )}>
      <div className="flex items-start gap-4">
        <div className="rounded-xl border border-white/10 bg-black/20 p-3">
          <Icon className="h-7 w-7 fill-current text-cyan-300 transition group-hover:text-cyan-100" />
        </div>

        <div className="min-w-0">
          <div className="flex items-center gap-2">
            <h2 className="text-lg font-semibold text-cyan-100">{title}</h2>
            <span className="text-cyan-500 transition group-hover:translate-x-0.5">→</span>
          </div>

          <p className="mt-1 text-sm text-cyan-300/80">{description}</p>
        </div>
      </div>
    </Link>
  )
}
