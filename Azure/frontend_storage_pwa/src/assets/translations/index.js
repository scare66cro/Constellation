// Here is a link to the docs for the react-intl library
// https://formatjs.io/docs/getting-started/message-extraction

// - To extract all Message components labeled for translations 
//   **NOTE the file produced is not useable as a translation itself, 
//   .....it is only for people to reference during creation of a new translation
// 1. npm i -D @formatjs/cli
// 2. add the following to the scripts object in package.json ->    "extract": "formatjs extract"
// 3. $ npm run extract -- 'src/**/*.js*' --out-file src/assets/translations/index.json --id-interpolation-pattern '[sha512:contenthash:base64:6]'

// this should produce a file named index.json with a format like the following:
// {
//     "app.login-title": {
//         "defaultMessage": "Login to Agristor.com @ {time}",
//         "description": "Login to Agristor.com"
//     },
//     "app.login-title": {
//         "defaultMessage": "Login to Agristor.com @ {time}",
//         "description": "Login to Agristor.com"
//     }
// }

// An actual translation file should look like:
// {
//   "app.login-title": "Some German translation goes here... Current time is {time}"
// }

import en from './en.json'
import ru from './ru.json'
import uk from './uk.json'
import zh from './zh.json'

import enLocale from "date-fns/locale/en-US";
import ruLocale from "date-fns/locale/ru";
import ukLocale from "date-fns/locale/uk";
import zhLocale from "date-fns/locale/zh-CN"

export const translations = {
    'en' : en,
    'en-US' : en,
    'ru' : ru,
    'uk': uk,
    'zh': zh,
}

export const muiPickersLocale = {
    'en': enLocale,
    'en-US': enLocale,
    'ru': ruLocale,
    'uk': ukLocale,
    'zh': zhLocale,
}
