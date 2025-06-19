export type TSlot = {
    slot_start_hour?: Number,
    slot_start_minute?: Number,
    slot_stop_hour?: Number,
    slot_stop_minute?: Number
};

export type TSettings  = {
  night_hour?: TSlot,
  settings?: TSlot[]
};

export type THP = {
    Tbe: number,
    Tae: number,
    Tco: number,
    Tho: number,
    Ttarget: number,
    Tsump: number,
    EEV_dt: number,
    Tcwu: number,
    Tmax: number,
    Tmin: number,
    Tcwu_max: number,
    Tcwu_min: number,
    Watts: number,
    EEV: number,
    EEV_pos: number,
    HCS: boolean,
    CCS: boolean,
    HPS: boolean,
    F: boolean,
    CWUS: boolean,
    CWU: boolean,
    CO: boolean
}

export type TPV = {
  total_power: number,
  total_prod: number,
  total_prod_today: number,
  temperature: number
}

export type TCO = {
  HP: THP,
  PV: TPV,
  time: String,
  co_pomp: boolean,
  cwu_pomp: Boolean,
  pv_power: boolean,
  schedule_on: boolean,
  work_mode: String
}

export type TSaveCO = {
  force?: String,
  work_mode?: String,
  temperature_co_max?: String,
  temperature_co_min?: String, 
  sump_heater?: String,
  cold_pomp?: String,
  hot_pomp?: String
}
