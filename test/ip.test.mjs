// Jalankan: node test/ip.test.mjs
import assert from "node:assert/strict";
import { hitungIP } from "../js/ip.js";

const kasus = [
  [4.55, 47.9, 2.4687, 0.2395, 1.991, "Cemar ringan"],
  [7.0, 150, 0.3333, 0.75, 0.654, "Memenuhi baku mutu"],
  [7.5, 250, 0, 1.4846, 1.174, "Cemar ringan"],
];
const dekat = (a, b, tol) => assert.ok(Math.abs(a - b) <= tol, `${a} != ${b}`);
for (const [ph, tss, rPh, rTss, ip, st] of kasus) {
  const h = hitungIP(ph, tss);
  dekat(h.rPh, rPh, 0.0001);
  dekat(h.rTss, rTss, 0.0001);
  dekat(h.ip, ip, 0.001);
  assert.equal(h.status, st);
}
assert.equal(hitungIP(null, 100), null);
assert.equal(hitungIP(7, null), null);
assert.equal(hitungIP(undefined, undefined), null);
console.log("Semua uji IP lulus");
