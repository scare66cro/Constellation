module.exports = {
	root: true,
	extends: [
		'eslint:recommended',
		'plugin:@typescript-eslint/recommended',
		'plugin:svelte/recommended',
		'prettier'
	],
	parser: '@typescript-eslint/parser',
	plugins: ['@typescript-eslint'],
	parserOptions: {
		sourceType: 'module',
		ecmaVersion: 2020,
		extraFileExtensions: ['.svelte']
	},
	env: {
		browser: true,
		es2017: true,
		node: true
	},
	rules: {
		'no-restricted-globals': [
			'error',
			{
				name: 'Date',
				message: "Use getControllerTimestamp(), getControllerDate(), or parseControllerTime() from '$lib/business/timeUtils' instead of Date() to ensure controller time is used. Only use Date() for parsing controller timestamps."
			}
		],
		'no-restricted-syntax': [
			'error',
			{
				selector: "CallExpression[callee.object.name='Date'][callee.property.name='now']",
				message: "Use getControllerTimestamp() from '$lib/business/timeUtils' instead of Date.now() to ensure controller time is used."
			},
			{
				selector: 'NewExpression[callee.name=\'Date\'][arguments.length=0]',
				message: "Use getControllerDate() from '$lib/business/timeUtils' instead of new Date() to ensure controller time is used. Only use new Date(timestamp) for parsing controller timestamps."
			}
		]
	},
	overrides: [
		{
			files: ['*.svelte'],
			parser: 'svelte-eslint-parser',
			parserOptions: {
				parser: '@typescript-eslint/parser'
			}
		}
	]
};
