import { extendTailwindMerge } from 'tailwind-merge';

/**
 * Custom twMerge that recognizes our custom component classes
 */
export const twMerge = extendTailwindMerge({
  extend: {
    classGroups: {
      'font-size': [
        'text-size',
        'text-size-large', 
        'text-size-xl'
      ],
    },
  },
});
