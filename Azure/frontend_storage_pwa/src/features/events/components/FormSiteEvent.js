import { Grid } from "@material-ui/core"
import Autocomplete from '@material-ui/lab/Autocomplete';
import TextFieldBase from "../../../common/TextFieldBase";
import DateTimeInput from "../../../common/DateTimeInput";
import { useSelector } from "react-redux";
import { sitesListSelector } from "../../sites/selectors";
import { eventCategoryListSelector } from "../../events/selectors";
import { useIntl } from "react-intl";
import { formSiteEventSiteField, formSiteEventCategoryField, formSiteEventTitleField, formSiteEventStartedAtField, formSiteEventFinishedAtField } from "../../../utilities/translationObjects";

const FormSiteEvent = (props) => {
    const intl = useIntl()

    const updateDraftHandler = props.updateDraftHandler
    const draft = props.draft

    const sites = useSelector(sitesListSelector) 
    const categoryList = useSelector(eventCategoryListSelector)

    return(
        <Grid container spacing={1}>

            <Grid item xs={12} sm={12}>
                {/* SITE select site from list */}
                <Autocomplete 
                    options={sites}
                    value={sites.filter(s => s.id === draft.site)[0] || null}
                    getOptionLabel={site => site.name }
                    getOptionSelected={(option) => ( 'site' in draft ? option.id === draft.site : sites[0] )}
                    onChange={(e, value)=>updateDraftHandler('site', value?.id || null)}
                    renderInput={(params) => 
                        <TextFieldBase 
                            {...params} 
                            label={intl.formatMessage(formSiteEventSiteField)} 
                            variant="outlined" 
                            size='small'
                        />
                    }
                />
            </Grid>  
            
            <Grid item xs={12} sm={12}>
                {/* CATEGORY */}
                <Autocomplete
                    freeSolo         
                    options={categoryList}
                    value={draft.category || null}
                    getOptionLabel={category => category }
                    getOptionSelected={(option) => ( 'category' in draft ? option.id === draft.category : categoryList[0] )}
                    onChange={(e, value)=>updateDraftHandler('category', value)}
                    onInputChange={(e, value)=>updateDraftHandler('category', value)}
                    renderInput={(params) => 
                        <TextFieldBase 
                            {...params} 
                            label={intl.formatMessage(formSiteEventCategoryField)} 
                            variant="outlined" 
                            size='small'
                        />
                    }
                />
            </Grid>


            <Grid item xs={12} sm={12}>
                {/* TITLE */}
                <TextFieldBase 
                    fullWidth
                    value={draft.title || ''}
                    label={intl.formatMessage(formSiteEventTitleField)} 
                    onChange={(e)=>updateDraftHandler('title', e.target.value)}
                />
            </Grid>

            <Grid item xs={12} sm={12}>
                {/* START_AT */}
                <DateTimeInput
                    label={intl.formatMessage(formSiteEventStartedAtField)} 
                    value={draft.started_at || null }
                    onChange={(newDate) => updateDraftHandler('started_at', newDate)}
                />
            </Grid>

            <Grid item xs={12} sm={12}>
                {/* NOTES */}
                <TextFieldBase
                    fullWidth 
                    value={draft.notes || ''}
                    label={intl.formatMessage(formSiteEventCategoryField)} 
                    onChange={(e)=>updateDraftHandler('notes', e.target.value)}
                    multiline
                    rows={'5'}
                />
            </Grid>        
            
            <Grid item xs={12} sm={12}>
                {/* ENDED_AT */}
                <DateTimeInput 
                    label={intl.formatMessage(formSiteEventFinishedAtField)} 
                    value={draft.ended_at || null }
                    onChange={(newDate) => updateDraftHandler('ended_at', newDate)}
                />
            </Grid>
        </Grid>
    )
}
export default FormSiteEvent