import React, { useState } from 'react';
import Dialog from '@material-ui/core/Dialog';
import Container from '@material-ui/core/Container';
import {useSelector, useDispatch} from 'react-redux'
import { _loggedIn, _loginFailedMSG } from '../selectors'

import { loadAccount, login } from '../actions'

import ButtonBasic from '../../../common/ButtonBasic'
import TextFieldBase from '../../../common/TextFieldBase'
import gellert_logo_full from '../../../assets/images/gellert_logo_full_en.png'
import google_logo from '../../../assets/images/google_logo.svg'
import microsoft_logo from '../../../assets/images/Microsoft_logo.svg'

import { FormattedMessage, useIntl } from 'react-intl'
import { getTranslationObjById, 
    loginPassword, loginUsername, loginWithGoogle, loginWithMicrosoft 
} from '../../../utilities/translationObjects'

import { Button, Grid, makeStyles } from '@material-ui/core';

/*
    1. Mount Component if loggedIn is false
    2. Attempt to get user account via /api/my-account/
    3. if response 401 proceed with login 
    4. otherwise put response data in redux store and mark user as logged in
    5. Unmount Component
*/
const styles = makeStyles(theme => ({
    root: {
      flexGrow: 1,
    },
}));

const LoginPopup = () => {
    const classes = styles()

    const dispatch = useDispatch()
    const isLoggedIn = useSelector(_loggedIn)
    const [accountChecked, setAccountChecked] = useState(false)
    const loginErrorMSG = useSelector(_loginFailedMSG)

    const intl = useIntl()

    React.useEffect(()=>{
        if(isLoggedIn === false){
            dispatch(loadAccount(() => setAccountChecked(true)))
        }
    },[isLoggedIn, dispatch])

    const [loginForm, setLoginForm] = useState({})

    const updateLoginForm = (field, value) => {
        
        setLoginForm({...loginForm, [field]:value})
    }

    const attemptLogin = (e, form) => {
        e.preventDefault()
        dispatch(login(form))
    }

    return(
        <Dialog
            fullScreen={true}
            open={accountChecked && !isLoggedIn}
        >
            <Container maxWidth={'sm'} style={{height:"85vh", margin:"15vh auto 0px"}}>

                <Grid
                    container
                    direction="column"
                    justifyContent="center"
                    alignItems="center"
                    spacing={3}
                    className={classes.root}
                    
                >
                        <Grid item xs={11} sm={9} md={8} lg={8}>
                            <img src={gellert_logo_full} 
                                alt={'Gellert an Agristor Company'}
                                style={{width:"100%"}}
                                onClick={()=>window.open(`https://gellert.com/`)}
                            />
                        </Grid>

                        <Grid item xs={8} sm={6} md={6} lg={6} style={{minWidth:'248px'}}>

                            <TextFieldBase 
                                error={loginErrorMSG !== undefined}
                                label={intl.formatMessage(loginUsername)}
                                type="username"
                                fullWidth
                                onChange={(e)=>updateLoginForm("username", e.target.value)}
                                style={{marginBottom:"15px"}}
                            />

                            <TextFieldBase 
                                error={loginErrorMSG !== undefined}
                                label={intl.formatMessage(loginPassword)}
                                type="password"
                                onChange={(e)=>updateLoginForm("password", e.target.value)}
                                fullWidth
                                helperText={loginErrorMSG ? intl.formatMessage(getTranslationObjById(loginErrorMSG)) : ''}
                            />
                        </Grid>

                        {/* <Grid item >
                        </Grid> */}


                        <Grid item >
                            <Grid container 
                                direction="column"
                                justifyContent="center"
                                alignItems="center"
                                spacing={3}
                            >

                                <Grid item>
                                    <ButtonBasic 
                                        onClick={(e)=>attemptLogin(e, loginForm)}
                                        >
                                        <FormattedMessage 
                                            id="login.login-button"
                                            defaultMessage="Login"
                                            description="text for login button"
                                            />
                                    </ButtonBasic>
                                </Grid> 
                                    <Button  disabled
                                        fullWidth
                                        color={'default'}
                                        variant={'outlined'}
                                        style={{padding: "5px 10px", minWidth:'240px', marginTop:"50px"}}
                                        startIcon={
                                            <img src={google_logo} alt={'icon'} style={{width:'20px',height:'20px', marginRight:'15px'}}/>
                                        }
                                        size={'small'}
                                    >
                                        {/* Login in with Google */}
                                            {intl.formatMessage(loginWithGoogle)}
                                    </Button>
                                    <Button
                                        color={'default'}
                                        variant={'outlined'}
                                        style={{padding: "5px 10px", minWidth:'240px', marginTop:"10px"}}
                                        href="/saml/initiate-login/"
                                        startIcon={
                                            <img src={microsoft_logo} alt={'icon'} style={{width:'20px', height:'20px'}}/>
                                        }
                                        size={'small'}
                                    >
                                        {/* Login in with Microsoft */}
                                        {intl.formatMessage(loginWithMicrosoft)}
                                    </Button>
                            </Grid>
                        </Grid>
                </Grid>
            </Container>
        </Dialog>
    )
}

export default LoginPopup