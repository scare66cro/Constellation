//NOTE: if error exists return TRUE!

//UNDEFINED returns TRUE if value is undefined
export const errorUndefined = (value) => {
    let hasError = false
    if(value === undefined){
        hasError=true
    }
    if(value === ""){
        hasError=true
    }
    return hasError
}

// IS NOT A NUMBER -> returns TRUE if value is not a number
export const errorNonNumber = (value) => {
    // isNaN returns TRUE if value is not a number
    return isNaN(value)
}

export function isValidSensor(value) {
    return (value !== undefined && value !== null && value !== 'dis' && value !== '--') ? true : false;
}