import { describe, expect, it } from "vitest";
import { networkChanges, networkRows } from "./wifiNetworks";

describe("saved Wi-Fi network credentials", () => {
  const original = [{ ssid: "Home", password_set: true }];

  it("does not resend or reveal an unchanged stored password", () => {
    const rows = networkRows(original);
    expect(rows[0].password).toBe("");
    expect(networkChanges(rows, original)).toEqual({ changed: false, value: [{ ssid: "Home" }] });
  });

  it("stages a destination without replacing the current credential", () => {
    const rows = networkRows(original);
    rows.push({ ssid: "Gift", password: "new-secret", openNetwork: false });
    expect(networkChanges(rows, original)).toEqual({
      changed: true,
      value: [{ ssid: "Home" }, { ssid: "Gift", password: "new-secret" }],
    });
  });

  it("explicitly clears a password only when choosing an open network", () => {
    const rows = networkRows(original);
    rows[0].openNetwork = true;
    expect(networkChanges(rows, original).value).toEqual([{ ssid: "Home", password: "" }]);
    expect(networkChanges(rows, original).changed).toBe(true);
  });

  it("requires a credential after changing a saved SSID", () => {
    const rows = networkRows(original);
    rows[0].ssid = "Other";
    expect(networkChanges(rows, original).error).toMatch(/Enter a password/);
  });

  it("retains credentials while reordering by network name", () => {
    const saved = [...original, { ssid: "Gift", password_set: true }];
    expect(networkChanges(networkRows(saved).reverse(), saved)).toEqual({
      changed: true,
      value: [{ ssid: "Gift" }, { ssid: "Home" }],
    });
  });

  it("rejects duplicate names, excessive networks and oversized UTF-8 SSIDs", () => {
    expect(networkChanges(networkRows([...original, ...original]), original).error).toBeTruthy();
    expect(networkChanges(Array(6).fill({}), []).error).toBeTruthy();
    expect(networkChanges([{ ssid: "鳥".repeat(11), openNetwork: true }], []).error).toBeTruthy();
  });

  it("tracks removal and rejects short passwords", () => {
    expect(networkChanges([], original)).toEqual({ changed: true, value: [] });
    expect(
      networkChanges([{ ssid: "Gift", password: "short", openNetwork: false }], []).error
    ).toBeTruthy();
  });
});
