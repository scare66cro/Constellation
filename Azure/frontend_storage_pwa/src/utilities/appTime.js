
// eventually migrate all things dynamic time format here

import { useEffect, useState } from "react";
import { useCookies } from "react-cookie";

const cookieName='time-format'
const cookieOptions = {
    h12:'h12',
    h24:'h24'
}

export const clockFormatOptions = cookieOptions

export function useClockFormat () {
    const [cookies] = useCookies()
    let preferredFormat = cookies[cookieName] || cookieOptions.h12
    const [clockFormat, setClockFormat] = useState(preferredFormat)

    useEffect(()=>{
        setClockFormat(preferredFormat)
    },[preferredFormat])

    return clockFormat
}