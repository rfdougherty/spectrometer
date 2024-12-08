bool startWifi() {
  WiFiManager wm;
  bool res = wm.autoConnect("Spectrometer"); //,"password"); // optional password
  if(!res) {
    Serial.println("Failed to connect");
    return false;
  }
  g_ipaddr = WiFi.localIP().toString();
  Serial << F("Connected to ") << WiFi.SSID() << F(" with IP address: ") << g_ipaddr << "\n";
  byte mac[6];
  WiFi.macAddress(mac);
  mac_to_str(mac, g_mac_str);
  Serial << F("MAC address: ") << g_mac_str << "\n";

  log_i("Connected to %s", WiFi.SSID().c_str());
  return true;
}

void setupRouting() {
  server.on("/wavelength", []() {
    char buf[30];
    updateTime(TIME_SYNC_TIMEOUT_MS);
    unsigned long ts = getEpochSecs();
    snprintf(g_buf, sizeof(g_buf), "{\"ts\":%d,\"nm\":[");
    for (int i = 0; i < SPEC_CHANNELS; i++) {
      if (i == SPEC_CHANNELS - 1)
        snprintf(buf, sizeof(buf), "%0.1f]}", g_nm_lut[i]);
      else
        snprintf(buf, sizeof(buf), "%0.1f,", g_nm_lut[i]);
      strlcat(g_buf, buf, sizeof(g_buf));
    }
    server.send(200, "application/json", g_buf);
  });

  server.on(UriBraces("/spectrum/{}"), []() {
    // takes required integration time (in microsec)
    uint32_t integration_time = constrain(server.pathArg(0).toInt(), spec.get_min_iteg_us(), 1000000);
    readSpecToGbuf(integration_time);
    server.send(200, "application/json", g_buf);
  });

  server.on(UriBraces("/pulserate/{}"), []() {
    // set the pulse rate, in hz
    uint32_t pulse_rate = constrain(server.pathArg(0).toInt(), 100000, 10000000);
    uint32_t ticks = spec.set_pulse_rate(pulse_rate);
    snprintf(g_buf, sizeof(g_buf), "{\"cpu_freq_mhz\":%d,\"pulse_ticks\":%d}", getCpuFrequencyMhz(), ticks);
    server.send(200, "application/json", g_buf);
  });

  server.on("/minimum_integration_time", []() {
    snprintf(g_buf, sizeof(g_buf), "{\"min_integ_ns\":%d}", spec.get_min_iteg_us());
    server.send(200, "application/json", g_buf);
  });

  server.on("/temperature", []() {
    updateTime(TIME_SYNC_TIMEOUT_MS);
    unsigned long ts = getEpochSecs();
    g_temperature = aht10.readTemperature(); // read 6-bytes via I2C, takes 80 milliseconds
    g_humidity = aht10.readHumidity(AHTXX_USE_READ_DATA);
    snprintf(g_buf, sizeof(g_buf), "{\"ts\":%d,\"temperature\":%0.1f,\"humidity\":%0.1f}", 
             ts, g_temperature, g_humidity);
    server.send(200, "application/json", g_buf);
  });

  server.on("/i2c", []() {
    i2c_scan(g_buf, sizeof(g_buf));
    server.send(200, "application/json", g_buf);
  });

  server.begin();
}

void mac_to_str(byte *mac, char *str) {
  for (int i = 0; i < 6; i++) {
    byte digit;
    digit = (*mac >> 8) & 0xF;
    *str++ = (digit < 10 ? '0' : 'A' - 10) + digit;
    digit = (*mac) & 0xF;
    *str++ = (digit < 10 ? '0' : 'A' - 10) + digit;
    *str++ = ':';
    mac++;
  }
  // replace the final colon with a nul terminator
  str[-1] = '\0';
}

void initTime(const char *timezone) {
  Serial.println("Setting up time");
  configTime(0, 0, g_ntpServer);  // First connect to NTP server, with 0 TZ offset
  updateTime(TIME_SYNC_TIMEOUT_MS);
  Serial.println("Received time from NTP");
  // Now we can set the real timezone
  // https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
  // PST/PDT is PST8PDT,M3.2.0,M11.1.0
  Serial << F("Setting Timezone to ") << timezone << "\n";
  setenv("TZ", timezone, 1);
  tzset();
}

bool updateTime(uint32_t timeout_ms) {
  if (!getLocalTime(&g_timeinfo, timeout_ms)) {
    Serial.println("Failed to update NTP time");
    return false;
  }
  return true;
}

unsigned long getEpochSecs() {
  time_t now;
  if (!getLocalTime(&g_timeinfo)) {
    return (0);
  }
  time(&now);
  return now;
}

static float getEpochFloat() {
  // Number of seconds since the epoch, but with ~microsecond precision
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (float)tv.tv_sec + tv.tv_usec/1e6;
}

static int64_t getEpochUs() {
  // Number of microseconds since the epoch
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (int64_t)tv.tv_usec + tv.tv_sec * 1000000ll;
}

void readSpecToGbuf(uint32_t integration_time) {
  /*
  Read spectrometer data into global g_buf as a json string. E.g.:
    {"ts":1234,"integration":100,"readtimes":[0,1,2,3],"spec":[10,20,30,...]}
  */
  char buf[30];
  spec.set_integration_time(integration_time);
  snprintf(g_buf, sizeof(g_buf), "{\"ts\":%f,\"integration\":%d,\"readtimes\":[", 
            getEpochFloat(), integration_time);
  spec.read_into(g_spec_data);
  // to read the timing data
  for(int i=0; i<4; i++){
    snprintf(buf, sizeof(buf), "%d,", spec.get_timing(i));
    strlcat(g_buf, buf, sizeof(g_buf));
  }
  snprintf(buf, sizeof(buf), "%d],\"spec\":[", spec.get_timing(4));
  strlcat(g_buf, buf, sizeof(g_buf));
  for (int i=0; i<SPEC_CHANNELS-1; i++) {
    snprintf(buf, sizeof(buf), "%d,", g_spec_data[i]);
    strlcat(g_buf, buf, sizeof(g_buf));
  }
  snprintf(buf, sizeof(buf), "%d]}", g_spec_data[SPEC_CHANNELS-1]);
  strlcat(g_buf, buf, sizeof(g_buf));
}