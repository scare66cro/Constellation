import { loadIotData } from "$lib/business/util";
export async function load({fetch}) {
  const response = await loadIotData('/iot/humidifier', fetch);
  
  // Handle empty response during SSR
  if (!response || !response.control) {
    return { 
      boardType: '',
      humidStatus: '',
      control: [
        ['0'],
        ['1'], 
        ['2'],
      ],
    };
  }
  
  return { 
    boardType: response.boardType,
    humidStatus: response.humidStatus,
    control: [
      ['0', ...response.control.slice(0, 7)],
      ['1', ...response.control.slice(7, 14)],
      ['2', ...response.control.slice(14, 21)],
    ],
  };
}
