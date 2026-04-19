import { useDispatch } from "react-redux"
import React from "react"
import { useRouteMatch } from "react-router-dom";
import { setSelectedSite } from "../actions"

// NOTE this is NOT 'SiteView.js'
const SiteView = (props) => {
    let { params } = useRouteMatch()
    const dispatch = useDispatch()

    React.useEffect(()=>{
        dispatch(setSelectedSite(params.site_id))
    // eslint-disable-next-line react-hooks/exhaustive-deps
    },[params.site_id])

    return(
        <>
            <div> Site view</div>
        </>
    )
}
export default SiteView
