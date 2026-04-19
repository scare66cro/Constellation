
import type { CustomThemeConfig } from '@skeletonlabs/tw-plugin';

export const gellertTheme: CustomThemeConfig = {
    name: 'gellert-theme',
    properties: {
		// =~= Theme Properties =~=
		"--theme-font-family-base": "system-ui",
		"--theme-font-family-heading": "system-ui",
		"--theme-font-color-base": "0 0 0",
		"--theme-font-color-dark": "255 255 255",
		"--theme-rounded-base": "9999px",
		"--theme-rounded-container": "8px",
		"--theme-border-base": "1px",
		// =~= Theme On-X Colors =~=
		"--on-primary": "0 0 0",
		"--on-secondary": "255 255 255",
		"--on-tertiary": "0 0 0",
		"--on-success": "0 0 0",
		"--on-warning": "0 0 0",
		"--on-error": "255 255 255",
		"--on-surface": "255 255 255",
		// =~= Theme Colors  =~=
		// primary | #0367A6 
		"--color-primary-50": "238 249 255", // #eef9ff
		"--color-primary-100": "220 243 255", // #dcf3ff
		"--color-primary-200": "178 233 255", // #b2e9ff
		"--color-primary-300": "109 216 255", // #6dd8ff
		"--color-primary-400": "32 197 255", // #20c5ff
		"--color-primary-500": "0 174 255", // #00aeff
		"--color-primary-600": "0 139 223", // #008bdf
		"--color-primary-700": "0 110 180", // #006eb4
		"--color-primary-800": "0 93 148", // #005d94
		"--color-primary-900": "0 78 124", // #004e7c <== Gellert Blue
		// secondary | #BF1523 
		"--color-secondary-50": "245 220 222", // #fcf6f4
		"--color-secondary-100": "242 208 211", // #faebe9
		"--color-secondary-200": "239 197 200", // #f5d8d6
		"--color-secondary-300": "229 161 167", // #edb8b4
		"--color-secondary-400": "210 91 101", // #e1908b
		"--color-secondary-500": "191 21 35", // #d26561
		"--color-secondary-600": "172 19 32", // #bd4143
		"--color-secondary-700": "143 16 26", // #a8353a <== Gellert Red
		"--color-secondary-800": "115 13 21", // #852c33
		"--color-secondary-900": "94 10 17", // #722930
		// tertiary | #8FC4D9 
		"--color-tertiary-50": "238 246 249", // #eef6f9
		"--color-tertiary-100": "233 243 247", // #e9f3f7
		"--color-tertiary-200": "227 240 246", // #e3f0f6
		"--color-tertiary-300": "210 231 240", // #d2e7f0
		"--color-tertiary-400": "177 214 228", // #b1d6e4
		"--color-tertiary-500": "143 196 217", // #8FC4D9
		"--color-tertiary-600": "129 176 195", // #81b0c3
		"--color-tertiary-700": "107 147 163", // #6b93a3
		"--color-tertiary-800": "86 118 130", // #567682
		"--color-tertiary-900": "70 96 106", // #46606a
		// success | #84cc16 
		"--color-success-50": "237 247 220", // #edf7dc
		"--color-success-100": "230 245 208", // #e6f5d0
		"--color-success-200": "224 242 197", // #e0f2c5
		"--color-success-300": "206 235 162", // #ceeba2
		"--color-success-400": "169 219 92", // #a9db5c
		"--color-success-500": "132 204 22", // #84cc16
		"--color-success-600": "119 184 20", // #77b814
		"--color-success-700": "99 153 17", // #639911
		"--color-success-800": "79 122 13", // #4f7a0d
		"--color-success-900": "65 100 11", // #41640b
		// warning | #EAB308 
		"--color-warning-50": "252 244 218", // #fcf4da
		"--color-warning-100": "251 240 206", // #fbf0ce
		"--color-warning-200": "250 236 193", // #faecc1
		"--color-warning-300": "247 225 156", // #f7e19c
		"--color-warning-400": "240 202 82", // #f0ca52
		"--color-warning-500": "234 179 8", // #EAB308
		"--color-warning-600": "211 161 7", // #d3a107
		"--color-warning-700": "176 134 6", // #b08606
		"--color-warning-800": "140 107 5", // #8c6b05
		"--color-warning-900": "115 88 4", // #735804
		// error | #ff3333 
		"--color-error-50": "255 224 224", // #ffe0e0
		"--color-error-100": "255 214 214", // #ffd6d6
		"--color-error-200": "255 204 204", // #ffcccc
		"--color-error-300": "255 173 173", // #ffadad
		"--color-error-400": "255 112 112", // #ff7070
		"--color-error-500": "255 51 51", // #ff3333
		"--color-error-600": "230 46 46", // #e62e2e
		"--color-error-700": "191 38 38", // #bf2626
		"--color-error-800": "153 31 31", // #991f1f
		"--color-error-900": "125 25 25", // #7d1919
		// surface | #0378A6 
		"--color-surface-50": "217 235 242", // #d9ebf2
		"--color-surface-100": "205 228 237", // #cde4ed
		"--color-surface-200": "192 221 233", // #c0dde9
		"--color-surface-300": "154 201 219", // #9ac9db
		"--color-surface-400": "79 161 193", // #4fa1c1
		"--color-surface-500": "3 120 166", // #0378A6
		"--color-surface-600": "3 108 149", // #036c95
		"--color-surface-700": "2 90 125", // #025a7d
		"--color-surface-800": "2 72 100", // #024864
		"--color-surface-900": "1 59 81", // #013b51
	}
}