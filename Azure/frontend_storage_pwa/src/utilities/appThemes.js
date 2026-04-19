import { ThemeProvider } from '@material-ui/core';
import { createTheme } from '@material-ui/core/styles';
import {unstable_createMuiStrictModeTheme} from "@material-ui/core";
import { useCookies } from "react-cookie"

// this 'in production check' is for dealing with the findDOMnode deprication error
const createThemeHelper = process.env.NODE_ENV === 'production' ? 
    createTheme : unstable_createMuiStrictModeTheme;

// const appTheme = createTheme({
const appTheme = {
    palette: {
        primary: {
            main: '#004e7c',
        },
        secondary: {
            main: '#a8353a',
        },
        default: {
            main: '#bebfc2'
        },
        background:{
            main: '#FFFFFF'
        },
        success:{
            main:'#45bf32'
        },
        error: {
            main: '#e6301c'
        }
    },
};

const fontSmall = createThemeHelper({
    ...appTheme,
    typography:{fontSize:12}
})
const fontMedium = createThemeHelper({
    ...appTheme,
    typography:{fontSize:14}
})
const fontLarge = createThemeHelper({
    ...appTheme,
    typography:{fontSize:16}
})
const fontXL = createThemeHelper({
    ...appTheme,
    typography:{fontSize:18}
})

const CustomThemeProvider = (props) => {
    const [cookies, setCookie] = useCookies()
    const getFontSize = () => {
        const customSize = cookies['font-scale']
        switch(customSize) {
            case 'small':
                return fontSmall
            case 'medium':
                return fontMedium
            case 'large':
                return fontLarge
            case 'extra-large':
                return fontXL
            default:
                setCookie('font-scale', 'medium', {path:'/'})
                return fontMedium
        } 
    }

    return(
        // <ThemeProvider theme={appTheme}>
            <ThemeProvider theme={getFontSize()} >
                {props.children}
            </ThemeProvider>
        // </ThemeProvider>
    )
} 
export default CustomThemeProvider