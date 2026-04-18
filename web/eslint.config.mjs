import nextCoreVitals from 'eslint-config-next/core-web-vitals'
import nextTypescript from 'eslint-config-next/typescript'

const eslintConfig = [
  ...nextCoreVitals,
  ...nextTypescript,
  {
    rules: {
      // Keep lint signal high without enforcing React Compiler migration rules yet.
      'react-hooks/static-components': 'off',
      'react-hooks/set-state-in-effect': 'off',
      'react-hooks/refs': 'off',
      'react-hooks/purity': 'off',
      'react/display-name': 'off',
    },
  },
]

export default eslintConfig
