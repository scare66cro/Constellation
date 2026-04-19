import { CircularProgress } from "@material-ui/core";

const SaveComponent = (props) => {
  return (
    props.saving
    ?
      <div style={{width:'100%', display:'flex', justifyContent:'center'}}>
        <CircularProgress />
      </div>
    :
      props.children
  );
}

export default SaveComponent;
