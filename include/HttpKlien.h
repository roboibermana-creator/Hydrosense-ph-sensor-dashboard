#pragma once
// Pengganti HTTPClient untuk Network.cpp: memakai WiFi bila tersambung, dan bila WiFi mati
// memakai data seluler (Gsm.h) langsung ke Supabase. Antarmukanya meniru bagian
// HTTPClient yang dipakai Network.cpp, jadi fungsi Supabase yang ada tidak perlu diubah.
#include "Gsm.h"
#include <Arduino.h>
#include <HTTPClient.h>

bool wifiTersambung();
extern const char *SUPABASE_URL;

class HttpKlien {
public:
  // false bila tidak ada jalur sama sekali (WiFi mati dan data seluler belum tersambung).
  bool begin(const String &url) {
    gsm_ = false;
    ok_ = false;
    resp_ = "";
    header_ = "";
    if (wifiTersambung()) {
      ok_ = h_.begin(url);
      return ok_;
    }
    String dasar = String(SUPABASE_URL);
    if (url.startsWith(dasar) && gsmSiap()) { // data seluler (SIM7600)
      path_ = url.substring(dasar.length());
      gsm_ = true;
      ok_ = true;
      return true;
    }
    return false;
  }

  void addHeader(const String &nama, const String &nilai) {
    if (gsm_)
      header_ += nama + ": " + nilai + "\r\n";
    else if (ok_)
      h_.addHeader(nama, nilai);
  }

  int GET() { return kirim(0, ""); }
  int POST(const String &badan) { return kirim(1, badan); }
  int PATCH(const String &badan) { return kirim(2, badan); }

  String getString() { return gsm_ ? resp_ : (ok_ ? h_.getString() : String()); }

  void end() {
    if (ok_ && !gsm_)
      h_.end();
    ok_ = false;
    gsm_ = false;
  }

  HTTPClient &raw() { return h_; }
  bool lewatGsm() const { return gsm_; }

private:
  HTTPClient h_;
  bool ok_ = false;
  bool gsm_ = false;
  String header_;
  String path_;
  String resp_;

  // jenis: 0 = GET, 1 = POST, 2 = PATCH
  int kirim(int jenis, const String &badan) {
    if (!ok_)
      return -1;
    if (gsm_) {
      static const char *nama[] = {"GET", "POST", "PATCH"};
      return gsmHttp(nama[jenis], path_, header_, badan, resp_);
    }
    if (jenis == 0)
      return h_.GET();
    if (jenis == 1)
      return h_.POST(badan);
    return h_.PATCH(badan);
  }
};
