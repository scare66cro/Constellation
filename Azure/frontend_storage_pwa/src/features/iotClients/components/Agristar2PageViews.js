import { useHistory } from "react-router"
import { Switch, Route, useRouteMatch, Redirect } from "react-router"
import Agristar2PageViewEquipmentStatus from "./Agristar2PageViewsEquipmentStatus"
import Agristar2PageViewSettings from "./Agristar2PageViewsGeneralSettings"
import Agristar2PageViewRunClock from "./Agristar2PageViewsRunClock"
import Agristar2PageViewClimacellClock from "./Agristar2PageViewsClimacellClock"
import Agristar2PageViewAdvancedSettings from "./Agristar2PageViewsAdvancedSettings"
import Agristar2PageViewUpgradeClient from "./Agristar2PageViewsUpgradeClient"
import Agristar2PagePileView from "./Agristar2PagePileView";

const Agristar2PageViews = (props) => {
    let {path, url} = useRouteMatch();
    const history = useHistory();

    return(
        <Switch >
            <Route exact path={`${path}/`}>
                <Redirect to={`${url}/settings`} />
            </Route>

            <Route path={`${path}/settings`} >
                <Agristar2PageViewSettings /> 
            </Route>

            <Route path={`${path}/pile`}>
                <Agristar2PagePileView />
            </Route>

            <Route path={`${path}/system-run-clock`} >
                <Agristar2PageViewRunClock />
            </Route>

            <Route path={`${path}/climacell-clock`} >
                <Agristar2PageViewClimacellClock />
            </Route>

            <Route path={`${path}/equipment-status`} >
                <Agristar2PageViewEquipmentStatus />
            </Route>

            <Route path={`${path}/advanced`} >
                <Agristar2PageViewAdvancedSettings />
            </Route>

            <Route path={`${path}/upgrade`}>
                <Agristar2PageViewUpgradeClient upgrade={history.location.state} />
            </Route>
        </Switch>
    )
}
export default Agristar2PageViews