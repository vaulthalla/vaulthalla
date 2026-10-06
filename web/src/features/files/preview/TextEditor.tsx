'use client'

import React, { useEffect, useRef } from 'react'
import { Compartment, EditorState, type Extension, type Text } from '@codemirror/state'
import {
  drawSelection,
  EditorView,
  highlightActiveLine,
  highlightActiveLineGutter,
  highlightSpecialChars,
  keymap,
  lineNumbers,
} from '@codemirror/view'
import { defaultKeymap, history, historyKeymap } from '@codemirror/commands'
import { bracketMatching, indentOnInput, syntaxHighlighting } from '@codemirror/language'
import { oneDarkHighlightStyle } from '@codemirror/theme-one-dark'

// CodeMirror 6, assembled from the minimum (no basicSetup: no autocomplete, lint or search panels). Only reachable
// through next/dynamic from TextRenderer, and each language pack is a further chunk loaded by file extension.

export interface EditorHandle {
  getText: () => string
  // Replace the whole document and treat it as the saved baseline (after a reload).
  reset: (text: string) => void
  // The current document is now the saved baseline.
  markSaved: () => void
}

const js = (options: { jsx?: boolean; typescript?: boolean }) => () => import('@codemirror/lang-javascript').then(m => m.javascript(options))

const LANGUAGES: Record<string, () => Promise<Extension>> = {
  js: js({}),
  mjs: js({}),
  cjs: js({}),
  jsx: js({ jsx: true }),
  ts: js({ typescript: true }),
  mts: js({ typescript: true }),
  tsx: js({ jsx: true, typescript: true }),
  json: () => import('@codemirror/lang-json').then(m => m.json()),
  md: () => import('@codemirror/lang-markdown').then(m => m.markdown()),
  markdown: () => import('@codemirror/lang-markdown').then(m => m.markdown()),
  py: () => import('@codemirror/lang-python').then(m => m.python()),
  yaml: () => import('@codemirror/lang-yaml').then(m => m.yaml()),
  yml: () => import('@codemirror/lang-yaml').then(m => m.yaml()),
  c: () => import('@codemirror/lang-cpp').then(m => m.cpp()),
  h: () => import('@codemirror/lang-cpp').then(m => m.cpp()),
  cc: () => import('@codemirror/lang-cpp').then(m => m.cpp()),
  cpp: () => import('@codemirror/lang-cpp').then(m => m.cpp()),
  cxx: () => import('@codemirror/lang-cpp').then(m => m.cpp()),
  hpp: () => import('@codemirror/lang-cpp').then(m => m.cpp()),
  hh: () => import('@codemirror/lang-cpp').then(m => m.cpp()),
  css: () => import('@codemirror/lang-css').then(m => m.css()),
  html: () => import('@codemirror/lang-html').then(m => m.html()),
  htm: () => import('@codemirror/lang-html').then(m => m.html()),
  xml: () => import('@codemirror/lang-xml').then(m => m.xml()),
  svg: () => import('@codemirror/lang-xml').then(m => m.xml()),
  sql: () => import('@codemirror/lang-sql').then(m => m.sql()),
}

// Design tokens only (CSS variables from globals.css).
const theme = EditorView.theme(
  {
    '&': { height: '100%', fontSize: '12.5px', color: 'var(--fg)', backgroundColor: 'transparent' },
    '&.cm-focused': { outline: 'none' },
    '.cm-scroller': { fontFamily: 'var(--font-mono)', lineHeight: '1.6' },
    '.cm-content': { caretColor: 'var(--accent)' },
    '.cm-cursor, .cm-dropCursor': { borderLeftColor: 'var(--accent)' },
    '.cm-gutters': { backgroundColor: 'transparent', color: 'var(--fg-faint)', border: 'none' },
    '.cm-activeLine, .cm-activeLineGutter': { backgroundColor: 'var(--surface-2)' },
    '&.cm-focused > .cm-scroller > .cm-selectionLayer .cm-selectionBackground, .cm-selectionBackground': {
      backgroundColor: 'var(--accent-soft)',
    },
    '.cm-matchingBracket': { backgroundColor: 'var(--accent-soft)', outline: '1px solid var(--accent-line)' },
  },
  { dark: true },
)

export default function TextEditor({
  initial,
  fileName,
  onDirty,
  onSave,
  handleRef,
}: {
  initial: string
  fileName: string
  onDirty: (dirty: boolean) => void
  onSave: () => void
  handleRef: React.MutableRefObject<EditorHandle | null>
}) {
  const host = useRef<HTMLDivElement>(null)
  const callbacks = useRef({ onDirty, onSave })
  callbacks.current = { onDirty, onSave }

  useEffect(() => {
    if (!host.current) return
    const language = new Compartment()
    let baseline: Text
    let dirty = false
    const report = (doc: Text) => {
      const next = !doc.eq(baseline)
      if (next !== dirty) {
        dirty = next
        callbacks.current.onDirty(next)
      }
    }
    const view = new EditorView({
      parent: host.current,
      state: EditorState.create({
        doc: initial,
        extensions: [
          lineNumbers(),
          highlightActiveLineGutter(),
          highlightSpecialChars(),
          history(),
          drawSelection(),
          indentOnInput(),
          bracketMatching(),
          highlightActiveLine(),
          syntaxHighlighting(oneDarkHighlightStyle, { fallback: true }),
          keymap.of([
            {
              key: 'Mod-s',
              preventDefault: true,
              run: () => {
                callbacks.current.onSave()
                return true
              },
            },
            ...defaultKeymap,
            ...historyKeymap,
          ]),
          EditorView.lineWrapping,
          EditorView.contentAttributes.of({ 'aria-label': `Contents of ${fileName}` }),
          theme,
          language.of([]),
          EditorView.updateListener.of(update => {
            if (update.docChanged) report(update.state.doc)
          }),
        ],
      }),
    })
    baseline = view.state.doc
    view.focus()

    handleRef.current = {
      getText: () => view.state.doc.toString(),
      reset: text => {
        view.dispatch({ changes: { from: 0, to: view.state.doc.length, insert: text } })
        baseline = view.state.doc
        report(baseline)
      },
      markSaved: () => {
        baseline = view.state.doc
        report(baseline)
      },
    }

    let cancelled = false
    const ext = fileName.includes('.') ? fileName.split('.').pop()!.toLowerCase() : ''
    LANGUAGES[ext]?.()
      .then(support => {
        if (!cancelled) view.dispatch({ effects: language.reconfigure(support) })
      })
      .catch(() => undefined) // plain text if the language chunk can't load

    return () => {
      cancelled = true
      handleRef.current = null
      view.destroy()
    }
    // The editor owns the document after mount; `initial` is read once.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [fileName, handleRef])

  return <div ref={host} className="h-full min-h-0" data-testid="text-editor" />
}
