import { useCookies } from "react-cookie"
import { useIntl } from "react-intl"
import { unitsPPM, unitsTemperatureCelsius, unitsTemperatureFahrenheit, unitsRelativeHumidity, unitsCalculated, unitsMI } from "./translationObjects"

import { useEffect, useState } from "react";

// essential variables map 
export const cookieName = 'units-of-measurement'
export const options = {
    'default':'imperial',
    'imperial':'imperial',
    'metric':'metric',
}
export const tempOptions = {
    fahrenheit:'fahrenheit',
    celsius:'celsius'
}

// this is for determining which unit of measurement is in use for the current user
export function useMeasurementSystem () {
    const [cookies] = useCookies()
    let currentSystem = cookies[cookieName] || options.default
    const [system, setSystem] = useState(currentSystem)

    useEffect(()=>{
        setSystem(currentSystem)
    },[currentSystem])

    return system
}

// ----------------CHANGE DEGREES SCALE----------------
export const changeTempScale = (degrees, current_units, desired_units, decimals=2) => {
    // current_units and desired_units except 'tempOptions' or 'options'
    let result = degrees
    if (desired_units === options.imperial || desired_units === tempOptions.fahrenheit) {
        if (current_units === tempOptions.celsius || current_units === options.metric) {
            result = scaleCelsiusToFahrenheit(degrees)
        }
    }
    else{
        if (current_units === tempOptions.fahrenheit || current_units === options.imperial) {
            result = scaleFahrenheitToCelsius(degrees)
        }
    }
    return parseFloat(result).toFixed(decimals)
}

// ---------------CHANGE TEMP FROM F TO C OR C TO F
export const changeTempType = (temperature, current_units, desired_units, decimals=2) => {
    let result = temperature
    if (desired_units === options.imperial || desired_units === tempOptions.fahrenheit) {
        if (current_units === tempOptions.celsius || current_units === options.metric) {
            result = convertCelsiusToFahrenheit(temperature)
        }
    }
    // user wants Celsius
    else{
        if (current_units === tempOptions.fahrenheit || current_units === options.imperial) {
            result = convertFahrenheitToCelsius(temperature)
        }
    }
    return parseFloat(result).toFixed(decimals)
}


// this function will always recieve Celsius because that is the temp system recieved from the AS2
// ...it will look at the preferred MeasurementSystem and return either Fahrenheight or Celsius
export function celsiusToPreferred (system, temp) {return getCustomizedTemperature(temp, system)}

// Does this inverse of 'useCelsiusToPreferred' -> ALWAYS returns CELSIUS
export function preferredToCelsius (system, temp) { return getGuaranteedCELSIUS(temp, system)}

export const unitsSymbolTranslationsMap = {
    temperature:{
        imperial: unitsTemperatureFahrenheit,
        metric: unitsTemperatureCelsius
    },
    ppm:{
        imperial: unitsPPM,
        metric: unitsPPM,
    },
    relativeHumidity:{
        imperial: unitsRelativeHumidity,
        metric: unitsRelativeHumidity,
    },
    calculatedHumidity: {
        imperial: unitsCalculated,
        metric: unitsCalculated,
    },
    mi: {
        imperial: unitsMI,
        metric: unitsMI,
    },
}

// UNIT TRANSLATION GETTERS 
export const UnitsComponent = (props) => {
    const intl = useIntl()
    let systemOfMeasurement = useMeasurementSystem()

    let typeOfData = props.typeOfData

    let unitsTranslation = unitsSymbolTranslationsMap[typeOfData]?.[systemOfMeasurement] 
    // console.log(unitsTranslation, typeOfData, systemOfMeasurement)
    return(
        <>
            {unitsTranslation ? intl.formatMessage(unitsTranslation) : ""}
        </>
    )
}

// UNIT CONVERSIONS
export const stringToFloat = (num) => parseFloat(num).toFixed(1)

// ---------------DEGREE SCALE CONVERTERS-----------------
export const scaleFahrenheitToCelsius = (fahrenheit) => (fahrenheit *(5/9)).toFixed(2)
export const scaleCelsiusToFahrenheit = (celsius) => (celsius *(9/5)).toFixed(2)

// ---------------TEMPERATURE CONVERTERS------------------
export const convertFahrenheitToCelsius = (celsius) => celsius ? ((stringToFloat(celsius) - 32)*(5/9)).toFixed(2) : undefined

export const convertCelsiusToFahrenheit = (fahrenheit) => fahrenheit ? ((stringToFloat(fahrenheit)*(9/5))+32).toFixed(2)  : undefined

//NOTE: do NOT assume all data from the agristar in Metric (celsius), IT CAN BE EITHER
export const getCustomizedTemperature = (value, preferredTempType, deviceTempType=tempOptions.celsius) => {
    // user wants Fahrenheit
    if (preferredTempType === options.imperial) {
        if (deviceTempType === tempOptions.celsius) {
            return parseFloat(convertCelsiusToFahrenheit(value)).toFixed(1)
        }
        return value
    }
    // user wants Celsius
    else{
        if (deviceTempType === tempOptions.fahrenheit) {
            return parseFloat(convertFahrenheitToCelsius(value)).toFixed(1)
        }
        return value
    }
}

export const getGuaranteedCELSIUS = (tempValue, system) => {
    if(system === options.imperial){
        return convertFahrenheitToCelsius(tempValue)
    }
    return tempValue
}