import { TSettings } from "./type";

function prefixMocks(path) {
  if (process.env.NODE_ENV !== "production") return `mock-responses/${path}`;
  return path;
}

class Requests {
  
  static get(path, options = {}, json = true) {
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
}


export class HpRequests {
  static getSettings() : Promise<TSettings> {
      return Requests.get("settings");
  }

  static getCoData() {
      return Requests.get("hp");
  } 
}
