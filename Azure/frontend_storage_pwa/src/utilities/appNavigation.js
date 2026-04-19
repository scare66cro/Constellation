export const navHandlerExternal = (destination) => {
    window.open(destination)
}

export const appURLs ={
    siteURLs: {
        index: "/sites",
        sitesMapView:"/sites/map",
        siteView:(id) =>`/sites/${id}`,
        siteDeviceView: (site_id, device_id) =>`/sites/${site_id}/devices/${device_id}`,
    },
    deviceURLs:{
        index:"devices",
        deviceView:(id)=>`/devices/${id}`
    },
    eventUrls: {
        index: "/events",
        eventsCalendar: "/events/calendar"
    },
    appSettings: {
        index: "/app-settings"
    },
    userProfile: {
        index: "/profile"
    },
    manageUsers: {
        index: "/manage-users"
    },
    manageSites: {
        index: "/manage-sites"
    },
}
