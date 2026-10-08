// Passwords are write-only. A blank input retains the credential for the same
// saved SSID, while choosing an open network explicitly clears its password.
export function networkRows(networks) {
  return networks.map((network) => ({
    ssid: network.ssid,
    password: "",
    passwordSet: network.password_set === true,
    openNetwork: network.password_set !== true,
  }));
}

export function networkChanges(rows, original) {
  if (rows.length > 5) return { error: "Save at most five Wi-Fi networks" };
  const names = new Set();
  const value = [];
  for (const row of rows) {
    const ssidBytes = new TextEncoder().encode(row.ssid).length;
    if (ssidBytes === 0 || ssidBytes > 32 || /[\x00-\x1f\x7f]/.test(row.ssid)) {
      return {
        error: "Each Wi-Fi network needs a name of 1 to 32 bytes without control characters",
      };
    }
    if (names.has(row.ssid))
      return { error: "Each saved Wi-Fi network must have a different name" };
    names.add(row.ssid);
    const saved = original.find((network) => network.ssid === row.ssid);
    const network = { ssid: row.ssid };
    if (row.openNetwork) {
      network.password = "";
    } else if (row.password) {
      if (!(/^[\x20-\x7e]{8,63}$/.test(row.password) || /^[a-fA-F0-9]{64}$/.test(row.password))) {
        return {
          error:
            "Wi-Fi passwords need 8 to 63 printable ASCII characters, or a 64-digit hexadecimal key",
        };
      }
      network.password = row.password;
    } else if (!saved?.password_set) {
      return { error: `Enter a password for ${row.ssid}, or select Open network` };
    }
    value.push(network);
  }
  const changed =
    value.length !== original.length ||
    value.some((network, index) => {
      const saved = original[index];
      return (
        network.ssid !== saved?.ssid ||
        (network.password !== undefined &&
          (network.password !== "" || saved?.password_set === true))
      );
    });
  return { changed, value };
}
