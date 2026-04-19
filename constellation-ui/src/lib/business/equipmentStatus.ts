import { get } from "svelte/store";
import { equipmentOptionsStore } from "$lib/store";
import { t } from "svelte-i18n";

export type Equipment = { eqStatus: string[],  pwmConfig: string[], outputConfig: string[], ioNames: string[], auxSwitches: string[], miscData: string[], systemMode: string };

export function getOutputColor(value: string): string {
  return value === '1' ? 'text-green-700 font-bold' : 'text-red-500 font-bold';
}

export function exists(ioName: string[], outConfig: string[]) {
  return (ioName && outConfig[parseInt(ioName[4], 10)] !== '-1')
}

export function renamedAs(ioName: string[], defaultName: string): string {
  return (ioName[3] === '1' && ioName[0] !== defaultName) ? ioName[0] : defaultName;
}

function buildDoorDiagEquipment(
  equipment: Equipment, eq: string,
  name: string, btn: string, obj: string[], diagStatus: string, switchStatus: string,
  outputStatus: string, inputStatus: string, edit: boolean, target: string,
) {
  const equipmentStore = get(equipmentOptionsStore);

  let panel: { status: string | undefined, color: string | undefined } = { status: undefined, color: undefined };
  panel = equipmentStore.getSwitchStatus(switchStatus);

  let input = equipmentStore.getDoorDiagStatus(inputStatus, outputStatus, diagStatus);
  return {
    exists: exists(obj, equipment.outputConfig),
    name: eq,
    equipmentName: renamedAs(obj, name),
    equipmentStatus: input.status,
    panelSwitchStatus: panel.status,
    equipOn: true,
    diagOn: diagStatus === '2',
    remSwitchName: btn,
    outputColor: getOutputColor(outputStatus),
    statusColor: input.color,
    panelSwitchColor: panel.color,
    edit,
    target,
  };
}

function buildEquipment(
  equipment: Equipment,
  eq: string,
  name: string, btn: string, obj: string[],
  switchStatus: string, remoteStatus: string,
  outputStatus: string, inputStatus: string, edit: boolean, switch1Use: string[], switch2Use: string[],
  auxiliary = -1,
) {
  const equipmentStore = get(equipmentOptionsStore);

  let panel: { status: string | undefined, color: string | undefined } = { status: undefined, color: undefined };
  if (auxiliary === -1) {
      panel = equipmentStore.getSwitchStatus(switchStatus);
  } else {
      panel = equipmentStore.getAuxSwitch(edit, switchStatus, switch1Use, switch2Use, auxiliary);
  }

  let input = equipmentStore.getStatus(equipment, eq, remoteStatus, inputStatus, outputStatus);
  return {
    exists: exists(obj, equipment.outputConfig),
    name: eq,
    equipmentName: renamedAs(obj, name),
    equipmentStatus: input.status,
    panelSwitchStatus: panel.status,
    equipOn: remoteStatus !== '1',
    remoteStatus,
    remSwitchName: btn,
    outputColor: getOutputColor(outputStatus),
    statusColor: input.color,
    panelSwitchColor: panel.color,
    edit : edit,
  };
}

function buildRefrigEquipment(
  equipment: Equipment, eq: string,
  name: string, btn: string, obj: string[], diagStatus: string, remoteStatus: string,
  outputStatus: string, inputStatus: string, edit: boolean,
) {
  const equipmentStore = get(equipmentOptionsStore);

  let panel: { status: string | undefined, color: string | undefined } = { status: undefined, color: undefined };
  panel = equipmentStore.getSwitchStatus(equipment.eqStatus[15]);
  let input = equipmentStore.getRefrigStatus(remoteStatus, inputStatus, outputStatus, diagStatus);
  return {
    exists: exists(obj, equipment.outputConfig),
    name: eq,
    equipmentName: renamedAs(obj, name),
    equipmentStatus: input.status,
    panelSwitchStatus: panel.status,
    equipOn: remoteStatus !== '1',
    diagOn: diagStatus === '2',
    remoteStatus,
    remSwitchName: btn,
    outputColor: getOutputColor(outputStatus),
    statusColor: input.color,
    panelSwitchColor: panel.color,
    edit : edit,
  };
}

function getDoorStatus(main: string[]): string {
  if (isNaN(parseInt(main[15]))) {
    return main[15];
  } else {
    return main[15] + '%';
  }
}

export function getEquipment(equipment: Equipment, eq: string, edit: boolean, main: string[]) {
  const $t = get(t);
  const switch1Use = equipment.auxSwitches[0].split(':');
  const switch2Use = equipment.auxSwitches[1].split(':');

  switch (eq) {
    case 'fan':
      return buildEquipment(equipment, eq, $t('level2.failures1.fan'), 'fanBtn',
        [$t('equipment.fan-green-light'), '', '1'],
        equipment.eqStatus[0],
        equipment.eqStatus[37],
        equipment.eqStatus[2],
        equipment.eqStatus[1],
        edit,
        switch1Use, switch2Use,
      );
    case 'climacell':
      return buildEquipment(equipment, eq, $t('equipment.climacell'), 'climacellBtn', 
        equipment.ioNames[3].split(':'),
        equipment.eqStatus[3],
        equipment.eqStatus[38],
        equipment.eqStatus[5],
        equipment.eqStatus[4],
        edit,
        switch1Use, switch2Use,
      );
    case 'heat':
      return buildEquipment(equipment, eq, $t('equipment.heat'), 'heatBtn', 
        equipment.ioNames[4].split(':'),
        equipment.eqStatus[48],
        equipment.eqStatus[48],
        equipment.eqStatus[28],
        equipment.eqStatus[27],
        edit,
        switch1Use, switch2Use,
        -2
      );
    case 'cavity':
      return buildEquipment(equipment, eq, $t('equipment.cavity-heat'), 'cavHeatBtn', 
        equipment.ioNames[5].split(':'),
        equipment.eqStatus[36],
        equipment.eqStatus[49], 
        equipment.eqStatus[32], 
        equipment.eqStatus[31], 
        edit,
        switch1Use, switch2Use,
      );
    case 'pile':
      return buildEquipment(equipment, eq, $t('equipment.cavity-heat'), 'cavHeatBtn', 
        [$t('equipment.pile-fan'), '', '', '1'],
        equipment.eqStatus[36],
        equipment.eqStatus[49], 
        equipment.eqStatus[32], 
        equipment.eqStatus[31], 
        edit,
        switch1Use, switch2Use,
      );
    case 'aux1Switch':
      return buildEquipment(equipment, eq, $t('equipment.auxiliary-1'), 'aux1Btn', 
        equipment.ioNames[25].split(':'),
        equipment.eqStatus[47],
        equipment.eqStatus[47], 
        equipment.eqStatus[61], 
        equipment.eqStatus[60],
        edit,
        switch1Use, switch2Use,
        1, 
      );
    case 'aux2Switch':
      return buildEquipment(equipment, eq, $t('equipment.auxiliary-2'), 'aux2Btn', 
        equipment.ioNames[26].split(':'),
        equipment.eqStatus[53],
        equipment.eqStatus[53], 
        equipment.eqStatus[63], 
        equipment.eqStatus[62],
        edit,
        switch1Use, switch2Use,
        2, 
      );
    case 'aux3Switch':
      return buildEquipment(equipment, eq, $t('equipment.auxiliary-3'), 'aux3Btn', 
        equipment.ioNames[27].split(':'),
        equipment.eqStatus[94],
        equipment.eqStatus[94], 
        equipment.eqStatus[65], 
        equipment.eqStatus[64],
        edit,
        switch1Use, switch2Use,
        3, 
      );
    case 'aux4Switch':
      return buildEquipment(equipment, eq, $t('equipment.auxiliary-4'), 'aux4Btn', 
        equipment.ioNames[28].split(':'),
        equipment.eqStatus[95],
        equipment.eqStatus[95], 
        equipment.eqStatus[67], 
        equipment.eqStatus[66],
        edit,
        switch1Use, switch2Use,
        4, 
      );
    case 'aux5Switch':
      return buildEquipment(equipment, eq, $t('equipment.auxiliary-5'), 'aux5Btn', 
        equipment.ioNames[29].split(':'),
        equipment.eqStatus[96],
        equipment.eqStatus[96], 
        equipment.eqStatus[69], 
        equipment.eqStatus[68],
        edit,
        switch1Use, switch2Use,
        5, 
      );
    case 'aux6Switch':
      return buildEquipment(equipment, eq, $t('equipment.auxiliary-6'), 'aux6Btn', 
        equipment.ioNames[30].split(':'),
        equipment.eqStatus[97],
        equipment.eqStatus[97], 
        equipment.eqStatus[71], 
        equipment.eqStatus[70],
        edit,
        switch1Use, switch2Use,
        6, 
      );
    case 'aux7Switch':
      return buildEquipment(equipment, eq, $t('equipment.auxiliary-7'), 'aux7Btn', 
        equipment.ioNames[31].split(':'),
        equipment.eqStatus[98],
        equipment.eqStatus[98], 
        equipment.eqStatus[73], 
        equipment.eqStatus[72],
        edit,
        switch1Use, switch2Use,
        7, 
      );
    case 'aux8Switch':
      return buildEquipment(equipment, eq, $t('equipment.auxiliary-8'), 'aux8Btn', 
        equipment.ioNames[32].split(':'),
        equipment.eqStatus[99],
        equipment.eqStatus[99],
        equipment.eqStatus[75],
        equipment.eqStatus[74],
        edit,
        switch1Use, switch2Use,
        8, 
      );
    case 'refrig':
      let stRefrig = 0;
      for (let i = 0; i < 10; i += 1) {
        if (equipment.outputConfig[i + 13] !== '-1' && equipment.eqStatus[i + 17] === '1') {
          stRefrig = 1;
          if (equipment.eqStatus[i < 6 ? i + 41 : (i - 6) + 89] === '2') {
            stRefrig = 2;
          }
        }
      }
      const refrigMainRow = buildRefrigEquipment(equipment, eq, $t('equipment.refrigeration'), 'refrigBtn',
        ['', '', '0'],
        stRefrig.toString(),
        equipment.eqStatus[50],
        (equipment.eqStatus[16] === '0' || stRefrig === 1) ? '1' : '0',
        equipment.eqStatus[79],
        edit,
      );
      if (stRefrig === 0) {
        refrigMainRow.outputColor = 'text-red-500 font-bold';
      }
      return refrigMainRow;
    case 'doordiag':
      return buildDoorDiagEquipment(equipment, eq, $t('level2.pid.fresh-air-doors'), 'doorDiag', 
      equipment.ioNames[0].split(':'),
      equipment.eqStatus[100],
      equipment.eqStatus[29],
      equipment.eqStatus[15],
      getDoorStatus(main),
      edit,
      equipment.eqStatus[101],
    );

    case 'refrig1':
      return buildRefrigEquipment(equipment, eq, $t('equipment.refrigeration-stage-1'), 'refr1Btn',
        equipment.ioNames[13].split(':'),
        equipment.eqStatus[41],
        equipment.eqStatus[50],
        equipment.eqStatus[17],
        equipment.eqStatus[79],
        edit,
      );
    case 'refrig2':
      return buildRefrigEquipment(equipment, eq, $t('equipment.refrigeration-stage-2'), 'refr2Btn',
        equipment.ioNames[14].split(':'),
        equipment.eqStatus[42],
        equipment.eqStatus[50],
        equipment.eqStatus[18],
        equipment.eqStatus[80],
        edit,
      );
    case 'refrig3':
      return buildRefrigEquipment(equipment, eq, $t('equipment.refrigeration-stage-3'), 'refr3Btn',
        equipment.ioNames[15].split(':'),
        equipment.eqStatus[43],
        equipment.eqStatus[50],
        equipment.eqStatus[19],
        equipment.eqStatus[81],
        edit,
      );
    case 'refrig4':
      return buildRefrigEquipment(equipment, eq, $t('equipment.refrigeration-stage-4'), 'refr4Btn',
        equipment.ioNames[16].split(':'),
        equipment.eqStatus[44],
        equipment.eqStatus[50],
        equipment.eqStatus[20],
        equipment.eqStatus[82],
        edit,
      );
    case 'refrig5':
      return buildRefrigEquipment(equipment, eq, $t('equipment.refrigeration-stage-5'), 'refr5Btn',
        equipment.ioNames[17].split(':'),
        equipment.eqStatus[45],
        equipment.eqStatus[50],
        equipment.eqStatus[21],
        equipment.eqStatus[83],
        edit,
      );
    case 'refrig6':
      return buildRefrigEquipment(equipment, eq, $t('equipment.refrigeration-stage-6'), 'refr6Btn',
        equipment.ioNames[18].split(':'),
        equipment.eqStatus[46],
        equipment.eqStatus[50],
        equipment.eqStatus[22],
        equipment.eqStatus[84],
        edit,
      );
    case 'refrig7':
      return buildRefrigEquipment(equipment, eq, $t('equipment.refrigeration-stage-7'), 'refr7Btn',
        equipment.ioNames[19].split(':'),
        equipment.eqStatus[89],
        equipment.eqStatus[50],
        equipment.eqStatus[23],
        equipment.eqStatus[85],
        edit,
      );
    case 'refrig8':
      return buildRefrigEquipment(equipment, eq, $t('equipment.refrigeration-stage-8'), 'refr8Btn',
        equipment.ioNames[20].split(':'),
        equipment.eqStatus[90],
        equipment.eqStatus[50],
        equipment.eqStatus[24],
        equipment.eqStatus[86],
        edit,
      );
    case 'defrost1':
      return buildRefrigEquipment(equipment, eq, $t('equipment.defrost-1'), 'defrost1Btn',
        equipment.ioNames[21].split(':'),
        equipment.eqStatus[91],
        equipment.eqStatus[50],
        equipment.eqStatus[25],
        equipment.eqStatus[87],
        edit,
      );
    case 'defrost2':
      return buildRefrigEquipment(equipment, eq, $t('equipment.defrost-2'), 'defrost2Btn',
        equipment.ioNames[22].split(':'),
        equipment.eqStatus[92],
        equipment.eqStatus[50],
        equipment.eqStatus[26],
        equipment.eqStatus[88],
        edit,
      );
    case 'humid1':
      return buildEquipment(equipment, eq, $t('equipment.humidifier-1-head'), 'humid1PumpBtn', 
        equipment.ioNames[7].split(':'),
        equipment.eqStatus[8],
        equipment.eqStatus[39], 
        equipment.eqStatus[10], 
        equipment.eqStatus[9],
        edit,
        switch1Use, switch2Use,
      );
    case 'pump1':
      return buildEquipment(equipment, eq, $t('equipment.humidifier-1-pump'), 'humid1PumpBtn', 
        equipment.ioNames[8].split(':'),
        equipment.eqStatus[8],
        equipment.eqStatus[39], 
        equipment.eqStatus[11], 
        equipment.eqStatus[9],
        edit,
        switch1Use, switch2Use,
      );
    case 'humid2':
      return buildEquipment(equipment, eq, $t('equipment.humidifier-2-head'), 'humid2PumpBtn',
        equipment.ioNames[7].split(':'),
        equipment.eqStatus[8],
        equipment.eqStatus[40], 
        equipment.eqStatus[13], 
        equipment.eqStatus[12],
        edit,
        switch1Use, switch2Use,
      );
    case 'pump2':
      return buildEquipment(equipment, eq, $t('equipment.humidifier-2-pump'), 'humid2PumpBtn',
        equipment.ioNames[7].split(':'),
        equipment.eqStatus[8],
        equipment.eqStatus[40], 
        equipment.eqStatus[14], 
        equipment.eqStatus[12],
        edit,
        switch1Use, switch2Use,
      );
    case 'humid3':
      return buildEquipment(equipment, eq, $t('equipment.humidifier-3-head'), 'humid3PumpBtn',
        equipment.ioNames[7].split(':'),
        equipment.eqStatus[8],
        equipment.eqStatus[93], 
        equipment.eqStatus[77], 
        equipment.eqStatus[76],
        edit,
        switch1Use, switch2Use,
      );
    case 'pump3':
      return buildEquipment(equipment, eq, $t('equipment.humidifier-3-pump'), 'humid3PumpBtn',
        equipment.ioNames[7].split(':'),
        equipment.eqStatus[8],
        equipment.eqStatus[93], 
        equipment.eqStatus[78], 
        equipment.eqStatus[76],
        edit,
        switch1Use, switch2Use,
      );
    case 'door':
      return buildEquipment(equipment, eq, $t('level2.pid.fresh-air-doors'), 'doorBtn', 
        equipment.ioNames[0].split(':'),
        equipment.eqStatus[29],
        '', 
        equipment.eqStatus[15],
        getDoorStatus(main),
        edit,
        switch1Use, switch2Use,
      );
    case 'cure':
      return buildEquipment(equipment, eq, $t('global.cure'), 'cureBtn',
        [$t('global.cure'), '', '1'],
        equipment.eqStatus[8], equipment.eqStatus[52],
        equipment.eqStatus[8], 
        equipment.eqStatus[8] === '1' && equipment.eqStatus[2] === '1' && equipment.eqStatus[52] === '0' ? '0' : '1',
        edit,
        switch1Use, switch2Use,
      );
    case 'burner':
      return buildEquipment(equipment, eq, $t('equipment.burner'), 'burnerBtn',
        equipment.ioNames[6].split(':'),
        equipment.eqStatus[3],
        equipment.eqStatus[51],
        equipment.eqStatus[7],
        equipment.eqStatus[6],
        edit,
        switch1Use, switch2Use,
      );
    case 'lights1':
      return buildEquipment(equipment, eq, $t('equipment.bay-lights-1'), 'lights1Btn',
        equipment.ioNames[23].split(':'),
        equipment.eqStatus[54],
        equipment.eqStatus[54],
        equipment.eqStatus[57],
        equipment.eqStatus[56],
        edit,
        switch1Use, switch2Use,
      );
    case 'lights2':
      return buildEquipment(equipment, eq, $t('equipment.bay-lights-2'), 'lights2Btn',
        equipment.ioNames[24].split(':'),
        equipment.eqStatus[55],
        equipment.eqStatus[55],
        equipment.eqStatus[59],
        equipment.eqStatus[58],
        edit,
        switch1Use, switch2Use,
      );
  }
}

