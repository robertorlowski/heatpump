import { TCO, TSaveCO, TSettings } from "./type";

function prefixMocks(path) {
  if (process.env.NODE_ENV !== "production") 
    return `mock-responses/${path}`;
  return  "api/".concat(path);
}

class Requests {
  
  static get(path, json = true) {
    return fetch(
      `/${prefixMocks(path)}`,
      Object.assign(
        {
          method: "GET",
          mode: "no-cors",
          cache: "no-cache",
          credentials: "same-origin",
          headers: {
            "Content-Type": "application/x-www-form-urlencoded"
          },
          redirect: "follow",
          referrer: "no-referrer"
        }
      )
    )
      .then(response => (json ? response.json() : response))
      .then(response => {
        console.log("GET", path, "resp", response);
        return response;
      })
      .catch(error => {
        console.warn(error);
      });
  }

  static post(path, data = {}, json = true) {
    return fetch(
      `/${prefixMocks(path)}`,
        Object.assign(
          {
            method: "POST",
            mode: "no-cors",
            cache: "no-cache",
            credentials: "same-origin",
            headers: {
              "Content-Type": "application/json; charset=utf-8"
            },
            redirect: "follow",
            referrer: "no-referrer",
            body: JSON.stringify(data)
          }
        )
      )
      .then(response => {
          return json ? response.json() : response
      })
      .then(response => {
        console.log("POST", path, data, "resp", response);
        return response;
      })
      .catch(error => {
        console.warn(error);
      });
  }

}


export class HpRequests {
  static getSettings() : Promise<TSettings> {
      return Requests.get("settings");
  }

  static getCoData() : Promise<TCO> {
      return Requests.get("hp");
  } 

  static setCoData(data: TSaveCO) {
      console.log(JSON.stringify(data));
      return Requests.post("operation", data, false);
  } 
}
