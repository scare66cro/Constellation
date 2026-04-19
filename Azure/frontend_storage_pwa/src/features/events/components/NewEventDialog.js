import { Button, Fab } from "@material-ui/core"
import { EventAvailable } from "@material-ui/icons"
import { useEffect, useState } from "react"
import { useIntl } from "react-intl"
import { useDispatch, useSelector } from "react-redux"
import FormDialog from "../../../common/FormDialog"
import StatusPopup from "../../../common/StatusPopup"
import { buttonCancel, formSiteEventSaveButton, formSiteEventTitleNewSiteEvent } from "../../../utilities/translationObjects"
import { createSiteEvent, createSiteEventReset } from "../actions"
import { _createSiteEventStatusSelector } from "../selectors"
import FormSiteEvent from "./FormSiteEvent"

const NewEventDialog = (props) => {
    const intl = useIntl()
    const [newEventOpen, setNewEventOpen] = useState(false)

    const [draft, setDraft] = useState({
        // "category": "Chemical",
        // "title": "Sproutnip 110lbs",
        // "notes": "Applied 110lbs of sproutnip, expecting a second round in 4 weeks",
        // "started_at": '2021-05-05T09:58:00Z',
        // "ended_at": null,
        // "site": '1243666e-ef19-4829-8548-420cfc74d235'
    })

    const updateDraftHandler = (field, value) => {
        console.log(field, value)
        const newDraft = {...draft, [field]:value }
        setDraft(newDraft)
    }

    const dispatch = useDispatch()
    const createEventStatusSelector = useSelector(_createSiteEventStatusSelector)

    const saveEventHandler = (myDraft) => {
        dispatch(createSiteEvent(myDraft))
    }

    const cancelHandler = () => {
        setNewEventOpen(false)
        setDraft({})
    }

    useEffect(()=>{
        cancelHandler()
    // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [createEventStatusSelector === 'success'])

    return(
        <>

            <StatusPopup 
                status={createEventStatusSelector} 
                // is not open when idle or undefined
                open={createEventStatusSelector !== 'idle' && createEventStatusSelector !== undefined} 
                // on click will reset the status
                onClick={()=>dispatch(createSiteEventReset())}
                successTimeout={1500}
                // errorTimeout={750}
            />

            <Fab size='medium' color='primary' 
                style={{position: "fixed", bottom:'80px', right:'15px'}}
                onClick={()=>setNewEventOpen(true)}
            >
                <EventAvailable />
            </Fab>
            <FormDialog 
                open={newEventOpen} 
                onClose={()=>setNewEventOpen(false)}
                dialogtitle={intl.formatMessage(formSiteEventTitleNewSiteEvent)}
                dialogactions={
                    <>
                        <Button 
                            style={{width:'40%'}}
                            onClick={cancelHandler}
                            variant='contained' 
                            color='default'>
                                {intl.formatMessage(buttonCancel)}
                        </Button>
                        <Button
                            onClick={()=>saveEventHandler(draft)} 
                            style={{width:'60%'}}
                            variant='contained' 
                            color='primary'>
                                {intl.formatMessage(formSiteEventSaveButton)}
                        </Button>
                    </>
                }
            >
                <FormSiteEvent 
                    updateDraftHandler={updateDraftHandler}
                    draft={draft}    
                />
            </FormDialog>
        </>
    )
}

export default NewEventDialog