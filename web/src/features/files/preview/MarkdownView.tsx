'use client'

import React from 'react'
import Markdown, { type Components } from 'react-markdown'
import remarkGfm from 'remark-gfm'

// Markdown rendered as React elements: raw HTML is dropped (skipHtml, no rehype-raw), unsafe link protocols are
// stripped by react-markdown's default urlTransform, links open in a new tab without an opener or referrer, and
// images are never fetched (a vault document must not make the viewer's browser call out).
const components: Components = {
  a: ({ href, children }) => (
    <a href={href} target="_blank" rel="noopener noreferrer">
      {children}
    </a>
  ),
  img: ({ alt, src }) => <span className="text-fg-subtle">[image{alt ? `: ${alt}` : typeof src === 'string' && src ? `: ${src}` : ''}]</span>,
}

const prose = [
  'text-sm leading-relaxed text-fg-muted break-words',
  '[&_h1]:mt-6 [&_h1]:mb-3 [&_h1]:text-xl [&_h1]:font-semibold [&_h1]:text-fg',
  '[&_h2]:mt-6 [&_h2]:mb-2 [&_h2]:text-lg [&_h2]:font-semibold [&_h2]:text-fg',
  '[&_h3]:mt-5 [&_h3]:mb-2 [&_h3]:font-semibold [&_h3]:text-fg',
  '[&_h4]:mt-4 [&_h4]:mb-1 [&_h4]:font-medium [&_h4]:text-fg',
  '[&_p]:my-3 [&_ul]:my-3 [&_ul]:list-disc [&_ul]:pl-6 [&_ol]:my-3 [&_ol]:list-decimal [&_ol]:pl-6 [&_li]:my-1',
  '[&_a]:text-accent-text [&_a]:underline [&_a]:underline-offset-2',
  '[&_blockquote]:my-3 [&_blockquote]:border-l-2 [&_blockquote]:border-line-strong [&_blockquote]:pl-4 [&_blockquote]:text-fg-subtle',
  '[&_code]:rounded [&_code]:bg-surface-2 [&_code]:px-1 [&_code]:py-0.5 [&_code]:font-mono [&_code]:text-xs [&_code]:text-fg',
  '[&_pre]:my-3 [&_pre]:overflow-x-auto [&_pre]:rounded-card [&_pre]:border [&_pre]:border-line [&_pre]:bg-surface-1 [&_pre]:p-3',
  '[&_pre_code]:bg-transparent [&_pre_code]:p-0',
  '[&_table]:my-3 [&_table]:w-full [&_table]:border-collapse [&_table]:text-xs',
  '[&_th]:border [&_th]:border-line [&_th]:px-2 [&_th]:py-1 [&_th]:text-left [&_th]:text-fg',
  '[&_td]:border [&_td]:border-line [&_td]:px-2 [&_td]:py-1',
  '[&_hr]:my-6 [&_hr]:border-line [&_strong]:text-fg',
].join(' ')

export default function MarkdownView({ text }: { text: string }) {
  return (
    <div className={prose} data-testid="markdown-view">
      <Markdown remarkPlugins={[remarkGfm]} components={components} skipHtml>
        {text}
      </Markdown>
    </div>
  )
}
