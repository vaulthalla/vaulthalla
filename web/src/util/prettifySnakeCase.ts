export const prettifySnakeCase = (str: string): string => {
  // Replace underscores with spaces and capitalize the first letter of each word
  return str
    .replace(/_/g, ' ')
    .split(' ')
    .map(word => word.charAt(0).toUpperCase() + word.slice(1))
    .join(' ')
}
