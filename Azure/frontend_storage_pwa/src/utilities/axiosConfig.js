// https://stackoverflow.com/questions/51794553/how-do-i-create-configuration-for-axios-for-default-request-headers-in-every-htt

// First we need to import axios.js
import axios from 'axios';
import getCookie from './cookieGetter' 

const axiosInstance = () => {
    const csrfToken = getCookie('csrftoken')

    // Next we make an 'instance' of it
    // .. where we make our configurations
    const instance = axios.create({
        withCredentials: true,  // Send cookies with cross-origin requests
    });
    
    // Where you would set stuff like your 'Authorization' or 'crsf' header, etc ...
    instance.defaults.headers.common['X-XSRF-TOKEN'] = csrfToken;
    
    // Also add/ configure interceptors && all the other cool stuff
    // instance.interceptors.request...
    instance.interceptors.response.use(function (response) {
        // Do something with success response data
        return response;
    }, function (error) {
        // Do something with error response
        return Promise.reject(error); //must return as promise to get full bit
    });

    return instance
}

export default axiosInstance();