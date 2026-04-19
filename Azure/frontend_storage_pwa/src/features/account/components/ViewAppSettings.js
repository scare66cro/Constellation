import { Accordion, AccordionActions, AccordionDetails, AccordionSummary, 
    ButtonGroup, Button,
    IconButton, List, ListItem, makeStyles, 
    Typography 
} from "@material-ui/core"
import { FormatSize, Language, PhonelinkSetup, Translate, QueryBuilder } from "@material-ui/icons"
import { useCookies } from "react-cookie"
import { FormattedMessage, useIntl } from "react-intl"
import { FlagChina, FlagRussia, FlagUkraine, FlagUSA } from "../../../common/Icons"
import PageView from "../../../common/PageView"
import { useClockFormat } from "../../../utilities/appTime"
import { cookieName, options, useMeasurementSystem } from "../../../utilities/appUnitsOfMeasurements"
import { bottomNavAppSettings, appSettingsLanguage, appSettingsFontSize } from "../../../utilities/translationObjects"

const useStyles = makeStyles((theme)=>({
    accordion:{
        '&.Mui-expanded':{
            margin:'0px auto'
        },
        "&.MuiAccordion-root.Mui-disabled":{
            backgroundColor:'inherit'
        }
    }
}))

const AppSettingsView = (props) => {
    const classes = useStyles()
    const intl = useIntl()

    const [cookies, setCookie] = useCookies()

    const languageHandler = (language) => {
        setCookie('locale-preferred', language, {path:'/'})
    }

    const fontSizeHandler = (size) => {
        setCookie('font-scale', size, {path:'/'})
        window.location.reload()
    }

    const currentClockFormat = useClockFormat()
    const timeFormatHandler = (timeFormat) => {
        setCookie('time-format', timeFormat, {path:'/'})
    }

    const currentMeasurementSystem = useMeasurementSystem()
    const unitsOfMeasurementHandler = (preferredUnits) => {
        setCookie(cookieName, preferredUnits, {path:'/'})
    }

    const checkCookie = (cookieField, value) => {
        return cookies[cookieField] === value
    }

    return(

        <PageView 
            pageIcon={<PhonelinkSetup  />}
            pageTitle={intl.formatMessage(bottomNavAppSettings)}
        >
            {/* <Divider /> */}
            <Accordion square 
                className={classes.accordion}
            >
                <AccordionSummary  >

                    <Translate style={{marginRight:'30px'}} />
                    <Typography>
                        {/* Languages */}
                        {intl.formatMessage(appSettingsLanguage)}
                    </Typography>
                    
                    <FlagUSA style={{margin: '-2px 0px 0px 30px', backgroundColor:'rgba(0, 0, 0, 0.08)', padding: '5px', borderRadius:'3px'}} hidden={cookies['locale-preferred'] !== 'en'} />
                    <FlagRussia style={{margin: '-2px 0px 0px 30px', backgroundColor:'rgba(0, 0, 0, 0.08)', padding: '5px', borderRadius:'3px'}} hidden={cookies['locale-preferred'] !== 'ru'} />
                    <FlagUkraine style={{margin: '-2px 0px 0px 30px', backgroundColor:'rgba(0, 0, 0, 0.08)', padding: '5px', borderRadius:'3px'}} hidden={cookies['locale-preferred'] !== 'uk'} />
                    <FlagChina style={{margin: '-2px 0px 0px 30px', backgroundColor:'rgba(0, 0, 0, 0.08)', padding: '5px', borderRadius:'3px'}} hidden={cookies['locale-preferred'] !== 'zh'} />
                
                </AccordionSummary>
                <AccordionDetails >

                    <List style={{width:'100%'}} >
                        <ListItem button onClick={()=>languageHandler('en')} selected={cookies['locale-preferred'] === 'en'}>
                            <FlagUSA style={{paddingRight: '30px'}} />
                            English
                        </ListItem>
                        <ListItem button onClick={()=>languageHandler('zh')} selected={cookies['locale-preferred'] === 'zh'}>
                            <FlagChina style={{paddingRight: '30px'}} />
                            Chinese
                        </ListItem>
                        {/* <ListItem button onClick={()=>languageHandler('ru')} selected={cookies['locale-preferred'] === 'ru'}>
                            <FlagRussia style={{paddingRight: '30px'}} />
                            русский
                        </ListItem>
                        <ListItem button onClick={()=>languageHandler('uk')} selected={cookies['locale-preferred'] === 'uk'}>
                            <FlagUkraine style={{paddingRight:'30px'}} />
                            Український
                        </ListItem> */}
                    </List>
                
                </AccordionDetails>
            </Accordion>
            {/* <Divider /> */}
            <Accordion  square
                className={classes.accordion}
                expanded={false}
            >
        {/* Measurement System */}
                <AccordionSummary >
                <div style={{marginRight:'30px'}}>
                    <Language />
                </div>
                    {/* <Typography>
                        {intl.formatMessage(appSettingsDisplayUnits)}
                    </Typography> */}
                    <ButtonGroup size='small' fullWidth>
                        <Button 
                            onClick={()=>unitsOfMeasurementHandler(options.imperial)} 
                            color='primary' variant={currentMeasurementSystem === 'imperial' ? 'contained' : undefined}
                        >
                            <FormattedMessage 
                                id='appSettings.unit-of-measurement-Imperial'
                                defaultMessage={`Imperial (\u00B0F)`}
                                description='Button label to indicate selecting Imperial as the base of measurements for the in app experience'
                                />    
                        </Button>
                        <Button 
                            onClick={()=>unitsOfMeasurementHandler(options.metric)} 
                            color='primary' variant={currentMeasurementSystem === 'metric' ? 'contained' : undefined}
                        >
                            <FormattedMessage 
                                id='appSettings.unit-of-measurement-metric'
                                defaultMessage={`Metric (\u00B0C)`}
                                description='Button label to indicate selecting Metric as the base of measurements for the in app experience'
                            />
                        </Button>
                    </ButtonGroup>
                </AccordionSummary>
                <AccordionDetails>
                </AccordionDetails>
            </Accordion>
            {/* <Divider /> */}
            <Accordion  square
                className={classes.accordion}
            >
                <AccordionSummary >
                <div style={{marginRight:'30px'}}>
                    <FormatSize />
                </div>
                <Typography>
                    {/* Font Size */}
                    {intl.formatMessage(appSettingsFontSize)}
                </Typography>
                </AccordionSummary>
                <AccordionActions style={{display:'flex', justifyContent:'center'}} >
                    <IconButton disabled={checkCookie('font-scale', 'small')} onClick={()=>fontSizeHandler('small')} >
                            <div style={{fontSize:'14px', fontWeight:'bolder'}}>
                                A
                            </div>
                    </IconButton>
                    <IconButton
                        disabled={checkCookie('font-scale', 'medium')} 
                        onClick={()=>fontSizeHandler('medium')}
                    >
                        <div style={{fontSize:'18px', fontWeight:'bolder'}}>
                            A
                        </div>
                    </IconButton>
                    <IconButton 
                        disabled={checkCookie('font-scale', 'large')}
                        onClick={()=>fontSizeHandler('large')}
                    >
                        <div style={{fontSize:'22px', fontWeight:'bolder'}}>
                            A
                        </div>
                    </IconButton>
                    <IconButton 
                        disabled={checkCookie('font-scale', 'extra-large')}
                        onClick={()=>fontSizeHandler('extra-large')}
                    >
                        <div style={{fontSize:'26px', fontWeight:'bolder'}}>
                            A
                        </div>
                    </IconButton>
                </AccordionActions>
            </Accordion>

            <Accordion square 
                className={classes.accordion}
                expanded={false}
            >
                <AccordionSummary  >

                    <QueryBuilder style={{marginRight:'30px'}} />
                    
                    <ButtonGroup size='small' fullWidth>
                        <Button onClick={()=>timeFormatHandler('h12')} color='primary' variant={currentClockFormat === 'h12' ? 'contained' : undefined} >12 HR</Button>
                        <Button onClick={()=>timeFormatHandler('h24')} color='primary' variant={currentClockFormat === 'h24' ? 'contained' : undefined}>24 HR</Button>
                    </ButtonGroup>
                    
                </AccordionSummary>
            </Accordion>
        </PageView>
    )
}
export default AppSettingsView