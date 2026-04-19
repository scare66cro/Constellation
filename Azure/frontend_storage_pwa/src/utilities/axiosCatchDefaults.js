import { appError } from "../features/account/actions"
import { authorizationFailed403, networkErrorMessage, serverErrorMessage } from "./translationObjects"

// Sets up default error handlers for all axios calls
// ...handles things like network calls, unauthenticated requests, server errors etc...

    // possible outcomes:
    // - success -      200 - 299
    // - failed auth -  403 -      doesn't realy apply to login
    // - user error -   401 - 
    // - client error - 401 - 499 
    // - server error - 500 - 599
    // - network error - undefined response

export function axiosCatchDefaults (error, dispatch, msg503) {
    const resp = error.response
    if(resp === undefined){
        return dispatch(appError(networkErrorMessage.id))
    }
    else{

        switch(resp.status){
            case 400:
                break;
            case 401:
                console.log(401)
                break;
            case 403:
                console.log(403)
                dispatch(appError(authorizationFailed403.id))
                break;
            case 500:
                console.log(500)
                dispatch(appError(serverErrorMessage.id))
                break;
            case 503:
                console.log(503)
                dispatch(appError( msg503 || networkErrorMessage.id))
                break;
            default:
                // dispatch(appError(serverErrorMessage.id))
                console.log(serverErrorMessage.id)
                break;
        }

    }
}

export default axiosCatchDefaults