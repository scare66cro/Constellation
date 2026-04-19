import { useState, useEffect, useRef } from "react";
import { differenceInMinutes, addMinutes } from "date-fns";
import { useSelector } from "react-redux";
import { extractAgristar2ListItemDataFromIoTClient, selectIoTClientById } from "../features/iotClients/selectors";

export function useValidateData(id, time) {
  const iotClient = useSelector((state) => selectIoTClientById(state, id));
  const agristar2_data = extractAgristar2ListItemDataFromIoTClient(iotClient)
  const [currentTime, setCurrentTime] = useState(Date.now());
  const [timeDiff, setTimeDiff] = useState(0);
  const [staleData, setStaleData] = useState(timeDiff >= time);
  const [networkError, setNetworkError] = useState(agristar2_data?.error);
  const [minute, setMinute] = useState(0);

  useInterval(()=> {
    setMinute(minute + 1);
  }, 60000);

  useEffect(() => {
    const time = differenceInMinutes(currentTime, new Date(agristar2_data?.time_stamp));
    setTimeDiff(time);
  }, [currentTime, agristar2_data.time_stamp])

  useEffect(() => {
    setCurrentTime(addMinutes(currentTime, 1));
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [minute])
  
  useEffect(() => {
    setStaleData(timeDiff >= time);
    setNetworkError(agristar2_data?.error);
  }, [agristar2_data.time_stamp, agristar2_data.error, time, timeDiff])

  return [staleData, networkError];
}

export function useInterval(callback, delay) {
    const savedCallback = useRef();
  
    // Remember the latest callback.
    useEffect(() => {
      savedCallback.current = callback;
    }, [callback]);
  
    // Set up the interval.
    useEffect(() => {
      function tick() {
        savedCallback.current();
      }
      if (delay !== null) {
        let id = setInterval(tick, delay);
        return () => clearInterval(id);
      }
    }, [delay]);
}
