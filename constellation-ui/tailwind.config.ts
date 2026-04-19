import { join } from 'path';
import type { Config } from 'tailwindcss'
import forms from '@tailwindcss/forms'
import typography from '@tailwindcss/typography'
import { skeleton } from '@skeletonlabs/tw-plugin'
import { gellertTheme } from './gellert-theme';
import plugin from 'tailwindcss/plugin';

const config = {
	// 2. Opt for dark mode to be handled via the class method
	darkMode: 'class',
	content: [
		'./src/**/*.{html,js,svelte,ts}',
		// 3. Append the path to the Skeleton package
		join(require.resolve(
			'@skeletonlabs/skeleton'),
			'../**/*.{html,js,svelte,ts}'
		)
	],
	theme: {
		extend: {
			width: {
				'128': '32rem',
				'144': '36rem',
			},
			screens: {
				'3xl': '1920px',
			},
			fontSize: {
				'xl': ['1.5rem', { lineHeight: '1.75rem', letterSpacing: '-0.01em' }],
				'2xl': ['1.75rem', { lineHeight: '2rem', letterSpacing: '-0.01em' }],
			}
		},
	},
	plugins: [
		forms,
		typography,
		// Ensure the `file:` variant (including font-size utilities) is available for styling file inputs
		plugin(({ addVariant }) => {
			// Keep file: utilities working across browsers
			addVariant('file', ['&::file-selector-button', '&::-webkit-file-upload-button']);
		}),
		skeleton({
			themes: {
				custom: [gellertTheme]
			},
		}),
	],
} satisfies Config;

export default config;
