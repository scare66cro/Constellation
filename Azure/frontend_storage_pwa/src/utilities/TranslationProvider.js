import {IntlProvider} from "react-intl";
import { useCookies } from 'react-cookie';
import {translations, muiPickersLocale} from '../assets/translations/index'
import '@formatjs/intl-numberformat/polyfill-force';
import '@formatjs/intl-numberformat/locale-data/en';
import '@formatjs/intl-numberformat/locale-data/zh';

import DateFnsUtils from "@date-io/date-fns";
import { MuiPickersUtilsProvider } from "@material-ui/pickers";

const TranslationProvider = (props) => {
    
    const getLocale = (cookiesObj) => {
        let locale
        // check redux user profile for locale
        
        // check for locale-preferred cookie
        // locale cookie gets set when user logs in.... it is set by users profile settings from db
        locale = cookiesObj['locale-preferred'] 
        if(locale !== undefined && locale !== ''){
            return locale
        }
        // else use browser default
        locale = window.navigator.language
        setCookie('locale-preferred', locale.split("-")[0], {path:'/'})
        return locale
    }
    
    /* const getTranslation = (locale) => {
        // look for exact match first
        let translation = translations[locale]
        
        // if an exact match does not exist for locale
        if(translation === undefined || translations === ''){
            // return one that at least has the same language
            translation = translations[locale.split("-")[0]]
        }
        // if no languages match, return English
        if(translation === undefined || translations === ''){
            return translations['en']
        }
        return translation
    } */
    
    const [cookies, setCookie] = useCookies();
    
    const myLocale = getLocale(cookies)
    
    // React.useEffect(()=>{
    //     // console.log("Locale:", myLocale, getTranslation(myLocale))
    //     console.log("Locale:", myLocale)
    //     console.log("cookies changed...",cookies)
    // },[cookies]) //NOTE** listen for changes in redux 'selected language'
    
    return(

        <IntlProvider 
            locale={myLocale}
            key={myLocale}
            messages={translations[myLocale]}
            defaultLocale='en'
        >

        {/* // https://material-ui-pickers.dev/localization/date-fns */}
        
            <MuiPickersUtilsProvider 
                utils={DateFnsUtils} 
                locale={muiPickersLocale[myLocale] || muiPickersLocale['en']}
            >
                {props.children}       
            </MuiPickersUtilsProvider>
        
        </IntlProvider>
    )
}
export default TranslationProvider 