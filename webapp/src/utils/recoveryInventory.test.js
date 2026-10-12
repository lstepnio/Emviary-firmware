import { readFileSync } from "node:fs";
import vm from "node:vm";
import { expect, it } from "vitest";

it("lists every saved profile without interpreting SSIDs as markup or exposing passwords", async () => {
  const elements = new Map();
  const element = () => ({
    children: [],
    style: {},
    dataset: {},
    textContent: "",
    value: "",
    append(...items) {
      this.children.push(...items);
    },
    replaceChildren() {
      this.children = [];
    },
  });
  const profiles = Array.from({ length: 5 }, (_, index) => ({
    ssid: index === 0 ? '<img src=x onerror="alert(1)">' : `Network ${index}`,
    password_set: index !== 4,
  }));
  const document = {
    getElementById(id) {
      if (!elements.has(id)) elements.set(id, element());
      return elements.get(id);
    },
    createElement: element,
    createTextNode: (textContent) => ({ textContent }),
  };
  const html = readFileSync(
    new URL("../../../main/recovery/recovery.html", import.meta.url),
    "utf8"
  );
  const script = html.match(/<script>([\s\S]*)<\/script>/)[1];
  const context = vm.createContext({
    document,
    setInterval() {},
    fetch: async (url) => ({
      ok: true,
      text: async () =>
        JSON.stringify(
          url.endsWith("config")
            ? {
                wifi_networks: profiles,
                wifi_connected_ssid: profiles[1].ssid,
              }
            : { device_name: "Emviary", version: "test", width: 800, height: 480 }
        ),
    }),
  });
  vm.runInContext(script, context);
  await vm.runInContext("load()", context);
  const inventory = document.getElementById("networkinventory").children;
  expect(inventory).toHaveLength(5);
  expect(inventory[0].textContent).toContain(profiles[0].ssid);
  expect(inventory[0].children).toHaveLength(0);
  expect(inventory[1].textContent).toContain("Connected");
  expect(inventory[4].textContent).toContain("Open network");
  expect(document.getElementById("networks").children).toHaveLength(5);
  expect(document.getElementById("networksummary").textContent).toContain("5 of 5");
});
