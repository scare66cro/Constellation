import { Accordion, AccordionDetails, AccordionSummary, Typography } from "@material-ui/core"
import { AccountBox, VpnKey } from "@material-ui/icons"
import { FormattedMessage, useIntl } from "react-intl"
import { useSelector } from "react-redux"
import PageView from "../../../common/PageView"
import { bottomNavMyProfile } from "../../../utilities/translationObjects"
import { selectUserProfile } from "../selectors"
import FormChangePassword from "./FormChangePassword"

const FieldLabel = (props) => {
    return(
        <Typography style={{fontWeight:'bolder'}}>
            {props.children}
            :&nbsp;&nbsp;
        </Typography>
    )
}

const ViewProfile = (props) => {
    // ------------HOOKS--------------
    const intl = useIntl()

    // -----------SELECTORS------------
    const userProfile = useSelector(state=>selectUserProfile(state))

    return(
        <PageView
            pageIcon={<AccountBox />}
            pageTitle={intl.formatMessage(bottomNavMyProfile)}
        >
            <Accordion square expanded={true}>
                <AccordionDetails style={{display:'block'}} >
                    <div style={{display:'flex', padding:'0px 5px 5px'}}>
                        <FieldLabel>
                            <FormattedMessage 
                                id='pageView-Profile-Organization'
                                defaultMessage='Organization'
                                description='title for Organization field'
                            />
                        </FieldLabel>
                        <Typography>
                            --
                            {/* {userProfile?.organization_id || 'None'} */}
                        </Typography>
                    </div>
                    <div style={{display:'flex', padding:'0px 5px 5px'}}>
                        <FieldLabel>
                            <FormattedMessage 
                                id='pageView-Profile-Username'
                                defaultMessage='Username'
                                description='title for username field'
                            />
                        </FieldLabel>
                        <Typography>
                            {userProfile?.username}
                        </Typography>
                    </div>
                    <div style={{display:'flex', padding:'5px'}}>
                        <FieldLabel>
                            <FormattedMessage 
                                id='pageView-Profile-Email'
                                defaultMessage='Email'
                                description='title for email field'
                            />
                        </FieldLabel>
                        <Typography>
                            {userProfile?.email}
                        </Typography>
                    </div>
                </AccordionDetails>
            </Accordion>
            <Accordion square >
                <AccordionSummary >
                    <VpnKey style={{marginRight:'30px'}} />
                    <Typography>
                        <FormattedMessage 
                            id='pageView-Profile-SectionTitle-ChangePassword'
                            defaultMessage='Change Password'
                            description='title for Change Password section'
                        />
                    </Typography>
                </AccordionSummary>
                <AccordionDetails style={{display:'flex', flexWrap:'wrap', justifyContent:'center'}} >
                    <FormChangePassword />
                </AccordionDetails>
            </Accordion>
        </PageView>
    )
}
export default ViewProfile