import { useSelector } from "react-redux";
import { IconButton } from "@material-ui/core";
import { Save } from "@material-ui/icons";
import { selectDirtyBits } from "./selectors";

const IsDirtySave = (props) => {
  const isDirty = useSelector((state) => selectDirtyBits(state));

  return (
    <IconButton onClick={() => props.onClick()} disabled={!isDirty[props.id]}>
      <Save color={isDirty[props.id] ? 'primary' : 'disabled'} />
    </IconButton>
  );
}

export default IsDirtySave;