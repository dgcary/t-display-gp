#include <unity.h>

#include <string>

#include "WeatherProvider.h"

void setUp() {}
void tearDown() {}

class FakeTransport final : public IHttpTransport {
 public:
  HttpResponse get(const std::string& url, const HttpHeaders& headers = {}) override {
    requestedUrl = url;
    requestedHeaders = headers;
    return response;
  }
  HttpResponse response;
  std::string requestedUrl;
  HttpHeaders requestedHeaders;
};

const char* validWeatherJson() {
  return R"json({
    "current":{
      "time":1787108400,
      "temperature_2m":31.2,
      "relative_humidity_2m":68,
      "apparent_temperature":34.1,
      "weather_code":1,
      "wind_speed_10m":12.4
    },
    "daily":{
      "weather_code":[1,61,3],
      "temperature_2m_max":[33.1,32.0,30.4],
      "temperature_2m_min":[26.2,25.0,24.3],
      "precipitation_probability_max":[10,55,20]
    }
  })json";
}

LocationConfig kunshan() {
  LocationConfig location;
  location.displayName = "昆山";
  location.latitudeE6 = 31385000;
  location.longitudeE6 = 120980000;
  return location;
}

void test_builds_open_meteo_url_and_parses_snapshot() {
  FakeTransport transport;
  transport.response.error = HttpTransportError::NONE;
  transport.response.statusCode = 200;
  transport.response.body = validWeatherJson();
  transport.response.elapsedMs = 321;
  transport.response.receivedBytes = transport.response.body.size();
  OpenMeteoProvider provider(transport);

  WeatherSnapshot out;
  WeatherDiagnostics diagnostics;
  TEST_ASSERT_EQUAL(WeatherError::NONE, provider.fetch(kunshan(), out, &diagnostics));
  TEST_ASSERT_TRUE(transport.requestedUrl.find("latitude=31.385000") != std::string::npos);
  TEST_ASSERT_TRUE(transport.requestedUrl.find("longitude=120.980000") != std::string::npos);
  TEST_ASSERT_TRUE(transport.requestedUrl.find("forecast_days=3") != std::string::npos);
  TEST_ASSERT_TRUE(transport.requestedUrl.find("timezone=Asia%2FShanghai") != std::string::npos);
  TEST_ASSERT_TRUE(transport.requestedUrl.find("timeformat=unixtime") != std::string::npos);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 31.2f, out.currentTemp);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 34.1f, out.apparentTemp);
  TEST_ASSERT_EQUAL_INT(68, out.humidityPercent);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.4f, out.windSpeed);
  TEST_ASSERT_EQUAL_INT(10, out.precipitationProbabilityPercent);
  TEST_ASSERT_EQUAL_INT(1, out.weatherCode);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 33.1f, out.today.highTemp);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, out.tomorrow.lowTemp);
  TEST_ASSERT_EQUAL_INT(3, out.dayAfter.weatherCode);
  TEST_ASSERT_EQUAL_UINT64(1787108400ULL, out.updatedEpochSeconds);
  TEST_ASSERT_EQUAL_UINT32(321, diagnostics.elapsedMs);
}

void test_missing_or_malformed_payload_fails_closed() {
  FakeTransport transport;
  transport.response.error = HttpTransportError::NONE;
  transport.response.statusCode = 200;
  OpenMeteoProvider provider(transport);

  WeatherSnapshot out;
  out.currentTemp = 99.0f;
  transport.response.body = R"json({"current":{"temperature_2m":31}})json";
  TEST_ASSERT_EQUAL(WeatherError::MISSING_FIELD, provider.fetch(kunshan(), out));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 99.0f, out.currentTemp);

  transport.response.body = "{broken";
  TEST_ASSERT_EQUAL(WeatherError::PARSE, provider.fetch(kunshan(), out));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 99.0f, out.currentTemp);
}

void test_short_daily_arrays_fail_closed() {
  FakeTransport transport;
  transport.response.error = HttpTransportError::NONE;
  transport.response.statusCode = 200;
  transport.response.body = R"json({
    "current":{"time":1,"temperature_2m":1,"relative_humidity_2m":2,"apparent_temperature":3,"weather_code":1,"wind_speed_10m":4},
    "daily":{"weather_code":[1],"temperature_2m_max":[2],"temperature_2m_min":[1],"precipitation_probability_max":[5]}
  })json";
  OpenMeteoProvider provider(transport);
  WeatherSnapshot out;
  TEST_ASSERT_EQUAL(WeatherError::MISSING_FIELD, provider.fetch(kunshan(), out));
}

void test_transport_errors_map_to_weather_errors_and_diagnostics() {
  FakeTransport transport;
  OpenMeteoProvider provider(transport);
  WeatherSnapshot out;
  WeatherDiagnostics diagnostics;

  transport.response.error = HttpTransportError::NETWORK;
  transport.response.nativeError = -5;
  transport.response.tlsError = -9984;
  TEST_ASSERT_EQUAL(WeatherError::NETWORK, provider.fetch(kunshan(), out, &diagnostics));
  TEST_ASSERT_EQUAL_INT(-5, diagnostics.nativeError);
  TEST_ASSERT_EQUAL_INT(-9984, diagnostics.tlsError);

  transport.response = {};
  transport.response.error = HttpTransportError::HTTP_STATUS;
  transport.response.statusCode = 503;
  TEST_ASSERT_EQUAL(WeatherError::HTTP_STATUS, provider.fetch(kunshan(), out, &diagnostics));
  TEST_ASSERT_EQUAL_INT(503, diagnostics.httpStatus);

  transport.response = {};
  transport.response.error = HttpTransportError::BODY_TOO_LARGE;
  TEST_ASSERT_EQUAL(WeatherError::BODY_TOO_LARGE, provider.fetch(kunshan(), out, &diagnostics));

  transport.response = {};
  transport.response.error = HttpTransportError::TRUNCATED_BODY;
  TEST_ASSERT_EQUAL(WeatherError::NETWORK, provider.fetch(kunshan(), out, &diagnostics));
}

const char* validMetNoJson() {
  return R"json({
    "properties": {
      "timeseries": [
        {"time": "2026-11-13T05:00:00Z", "data": {"instant": {"details": {"air_temperature": 19.5, "relative_humidity": 85.6, "wind_speed": 2.1}}, "next_1_hours": {"summary": {"symbol_code": "lightrain"}, "details": {"precipitation_amount": 0.1}}}},
        {"time": "2026-11-13T10:00:00Z", "data": {"instant": {"details": {"air_temperature": 21.0, "relative_humidity": 70.0, "wind_speed": 3.0}}, "next_1_hours": {"summary": {"symbol_code": "partlycloudy_day"}, "details": {"precipitation_amount": 0.0}}}},
        {"time": "2026-11-13T15:00:00Z", "data": {"instant": {"details": {"air_temperature": 16.2, "relative_humidity": 90.0, "wind_speed": 4.5}}, "next_1_hours": {"summary": {"symbol_code": "rain"}, "details": {"precipitation_amount": 0.4}}}},
        {"time": "2026-11-14T04:00:00Z", "data": {"instant": {"details": {"air_temperature": 14.0, "relative_humidity": 95.0, "wind_speed": 2.0}}, "next_1_hours": {"summary": {"symbol_code": "cloudy"}, "details": {"precipitation_amount": 0.0}}}},
        {"time": "2026-11-14T07:00:00Z", "data": {"instant": {"details": {"air_temperature": 18.8, "relative_humidity": 80.0, "wind_speed": 3.4}}, "next_1_hours": {"summary": {"symbol_code": "clearsky_day"}, "details": {"precipitation_amount": 0.0}}}},
        {"time": "2026-11-15T06:00:00Z", "data": {"instant": {"details": {"air_temperature": 12.5, "relative_humidity": 88.0, "wind_speed": 5.0}}, "next_6_hours": {"summary": {"symbol_code": "snow"}, "details": {"precipitation_amount": 1.0}}}}
      ]
    }
  })json";
}

void test_builds_metno_url_useragent_and_parses_snapshot() {
  FakeTransport transport;
  transport.response.error = HttpTransportError::NONE;
  transport.response.statusCode = 200;
  transport.response.body = validMetNoJson();
  transport.response.elapsedMs = 512;
  transport.response.receivedBytes = transport.response.body.size();
  MetNoProvider provider(transport);

  WeatherSnapshot out;
  WeatherDiagnostics diagnostics;
  TEST_ASSERT_EQUAL(WeatherError::NONE, provider.fetch(kunshan(), out, &diagnostics));
  TEST_ASSERT_TRUE(transport.requestedUrl.find("https://api.met.no/weatherapi/locationforecast/2.0/compact?lat=31.385000&lon=120.980000") != std::string::npos);
  TEST_ASSERT_TRUE(!transport.requestedHeaders.empty());
  bool userAgentFound = false;
  for (const auto& header : transport.requestedHeaders) {
    if (header.name == "User-Agent" &&
        header.value.find("TDisplayGP/1.0 github.com/dgcary/t-display-gp") != std::string::npos) {
      userAgentFound = true;
    }
  }
  TEST_ASSERT_TRUE(userAgentFound);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 19.5f, out.currentTemp);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 19.5f, out.apparentTemp);
  TEST_ASSERT_EQUAL_INT(85, out.humidityPercent);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.1f, out.windSpeed);
  TEST_ASSERT_EQUAL_INT(0, out.precipitationProbabilityPercent);
  TEST_ASSERT_EQUAL_INT(51, out.weatherCode);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.0f, out.today.highTemp);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 16.2f, out.today.lowTemp);
  TEST_ASSERT_EQUAL_INT(51, out.today.weatherCode);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 18.8f, out.tomorrow.highTemp);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 14.0f, out.tomorrow.lowTemp);
  TEST_ASSERT_EQUAL_INT(3, out.tomorrow.weatherCode);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.5f, out.dayAfter.highTemp);
  TEST_ASSERT_EQUAL_INT(73, out.dayAfter.weatherCode);
  TEST_ASSERT_EQUAL_UINT64(1794546000ULL, out.updatedEpochSeconds);
  TEST_ASSERT_EQUAL_UINT32(512, diagnostics.elapsedMs);
}

void test_metno_errors_fail_closed() {
  FakeTransport transport;
  MetNoProvider provider(transport);
  WeatherSnapshot out;
  WeatherDiagnostics diagnostics;

  transport.response.error = HttpTransportError::NETWORK;
  transport.response.nativeError = -5;
  TEST_ASSERT_EQUAL(WeatherError::NETWORK, provider.fetch(kunshan(), out, &diagnostics));
  TEST_ASSERT_EQUAL_INT(-5, diagnostics.nativeError);

  transport.response = {};
  transport.response.error = HttpTransportError::HTTP_STATUS;
  transport.response.statusCode = 429;
  TEST_ASSERT_EQUAL(WeatherError::HTTP_STATUS, provider.fetch(kunshan(), out, &diagnostics));

  transport.response = {};
  transport.response.error = HttpTransportError::BODY_TOO_LARGE;
  TEST_ASSERT_EQUAL(WeatherError::BODY_TOO_LARGE, provider.fetch(kunshan(), out, &diagnostics));

  transport.response = {};
  transport.response.error = HttpTransportError::NONE;
  transport.response.statusCode = 200;
  transport.response.body = "{broken";
  TEST_ASSERT_EQUAL(WeatherError::PARSE, provider.fetch(kunshan(), out, &diagnostics));

  transport.response.body = R"json({"properties":{"timeseries":[]}})json";
  TEST_ASSERT_EQUAL(WeatherError::MISSING_FIELD, provider.fetch(kunshan(), out, &diagnostics));
}

class AmapFakeTransport final : public IHttpTransport {
 public:
  HttpResponse get(const std::string& url, const HttpHeaders& headers = {}) override {
    requestedUrls.push_back(url);
    HttpResponse response;
    response.error = error;
    response.statusCode = error == HttpTransportError::NONE ? 200 : 0;
    response.elapsedMs = 100;
    if (url.find("geocode/regeo") != std::string::npos) {
      response.body = regeoBody;
    } else if (url.find("extensions=base") != std::string::npos) {
      response.body = baseBody;
    } else if (url.find("extensions=all") != std::string::npos) {
      response.body = allBody;
    }
    return response;
  }
  HttpTransportError error = HttpTransportError::NONE;
  std::string regeoBody;
  std::string baseBody;
  std::string allBody;
  std::vector<std::string> requestedUrls;
};

const char* validAmapRegeoJson() {
  return R"json({"status":"1","info":"OK","infocode":"10000","regeocode":{"addressComponent":{"adcode":"310000"}}})json";
}

const char* validAmapBaseJson() {
  return R"json({"status":"1","count":"1","info":"OK","lives":[{"province":"上海","city":"昆山市","weather":"多云","temperature":"23","winddirection":"东南","windpower":"≤3","humidity":"55","reporttime":"2026-11-18 10:00:00"}]})json";
}

const char* validAmapAllJson() {
  return R"json({"status":"1","count":"1","info":"OK","forecasts":[{"city":"昆山市","adcode":"310000","reporttime":"2026-11-18 10:00:00","casts":[
    {"date":"2026-11-18","dayweather":"多云","nightweather":"阴","daytemp":"21","nighttemp":"14","daywind":"东","daypower":"≤3"},
    {"date":"2026-11-19","dayweather":"小雨","nightweather":"小雨","daytemp":"19","nighttemp":"13","daywind":"东北","daypower":"3-4"},
    {"date":"2026-11-20","dayweather":"晴","nightweather":"晴","daytemp":"18","nighttemp":"11","daywind":"北","daypower":"≤3"}]}]})json";
}

void test_builds_amap_urls_and_parses_snapshot() {
  AmapFakeTransport transport;
  transport.regeoBody = validAmapRegeoJson();
  transport.baseBody = validAmapBaseJson();
  transport.allBody = validAmapAllJson();
  AmapProvider provider(transport);
  provider.setKey("6f8a1b2c3d4e5f60718293a4b5c6d7e8");
  TEST_ASSERT_TRUE(provider.hasKey());

  WeatherSnapshot out;
  WeatherDiagnostics diagnostics;
  TEST_ASSERT_EQUAL(WeatherError::NONE, provider.fetch(kunshan(), out, &diagnostics));

  bool regeoUrlFound = false;
  bool baseUrlFound = false;
  bool allUrlFound = false;
  for (const auto& url : transport.requestedUrls) {
    if (url.find("https://restapi.amap.com/v3/geocode/regeo?key=6f8a1b2c3d4e5f60718293a4b5c6d7e8&location=120.980000,31.385000") != std::string::npos) {
      regeoUrlFound = true;
    }
    if (url.find("https://restapi.amap.com/v3/weather/weatherInfo?key=6f8a1b2c3d4e5f60718293a4b5c6d7e8&city=310000&extensions=base") != std::string::npos) {
      baseUrlFound = true;
    }
    if (url.find("https://restapi.amap.com/v3/weather/weatherInfo?key=6f8a1b2c3d4e5f60718293a4b5c6d7e8&city=310000&extensions=all") != std::string::npos) {
      allUrlFound = true;
    }
  }
  TEST_ASSERT_TRUE(regeoUrlFound);
  TEST_ASSERT_TRUE(baseUrlFound);
  TEST_ASSERT_TRUE(allUrlFound);

  TEST_ASSERT_FLOAT_WITHIN(0.01f, 23.0f, out.currentTemp);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 23.0f, out.apparentTemp);
  TEST_ASSERT_EQUAL_INT(55, out.humidityPercent);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.5f, out.windSpeed);
  TEST_ASSERT_EQUAL_INT(0, out.precipitationProbabilityPercent);
  TEST_ASSERT_EQUAL_INT(2, out.weatherCode);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.0f, out.today.highTemp);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 14.0f, out.today.lowTemp);
  TEST_ASSERT_EQUAL_INT(2, out.today.weatherCode);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 19.0f, out.tomorrow.highTemp);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 13.0f, out.tomorrow.lowTemp);
  TEST_ASSERT_EQUAL_INT(61, out.tomorrow.weatherCode);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 18.0f, out.dayAfter.highTemp);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 11.0f, out.dayAfter.lowTemp);
  TEST_ASSERT_EQUAL_INT(0, out.dayAfter.weatherCode);
}

void test_amap_requires_key_and_maps_api_errors() {
  AmapFakeTransport transport;
  AmapProvider provider(transport);
  WeatherSnapshot out;
  TEST_ASSERT_FALSE(provider.hasKey());
  TEST_ASSERT_EQUAL(WeatherError::MISSING_FIELD, provider.fetch(kunshan(), out));

  provider.setKey("6f8a1b2c3d4e5f60718293a4b5c6d7e8");
  transport.regeoBody = R"json({"status":"0","info":"INVALID_USER_KEY","infocode":"10001"})json";
  TEST_ASSERT_EQUAL(WeatherError::HTTP_STATUS, provider.fetch(kunshan(), out));

  transport.regeoBody = R"json({"status":"1","regeocode":{"addressComponent":{"adcode":"310000"}}})json";
  transport.baseBody = "{broken";
  TEST_ASSERT_EQUAL(WeatherError::PARSE, provider.fetch(kunshan(), out));
}

class WeatherCnFakeTransport final : public IHttpTransport {
 public:
  HttpResponse get(const std::string& url, const HttpHeaders& headers = {}) override {
    requestedUrl = url;
    HttpResponse response;
    response.error = error;
    response.statusCode = error == HttpTransportError::NONE ? 200 : 0;
    response.elapsedMs = 277;
    response.body = body;
    return response;
  }
  HttpTransportError error = HttpTransportError::NONE;
  std::string body;
  std::string requestedUrl;
};

const char* validWeatherCnJson() {
  return R"json({
    "time":"2026-11-18 10:00",
    "city":{"cityId":"101190408","city":"昆山"},
    "data":{
      "shidu":"71%",
      "wendu":"11.2",
      "quality":"良",
      "forecast":[
        {"date":"18","high":"高温 15℃","low":"低温 7℃","ymd":"2026-11-18","fx":"西北风","fl":"2级","type":"多云"},
        {"date":"19","high":"高温 12℃","low":"低温 3℃","ymd":"2026-11-19","fx":"西北风","fl":"2级","type":"晴"},
        {"date":"20","high":"高温 13℃","low":"低温 3℃","ymd":"2026-11-20","fx":"西风","fl":"2级","type":"晴"}
      ]
    }
  })json";
}

void test_builds_wxcn_url_and_parses_snapshot() {
  WeatherCnFakeTransport transport;
  transport.body = validWeatherCnJson();
  WeatherCnProvider provider(transport);
  provider.setCityId("101190408");
  TEST_ASSERT_TRUE(provider.hasCityId());

  WeatherSnapshot out;
  WeatherDiagnostics diagnostics;
  TEST_ASSERT_EQUAL(WeatherError::NONE, provider.fetch(kunshan(), out, &diagnostics));
  TEST_ASSERT_TRUE(transport.requestedUrl.find("http://t.weather.itboy.net/api/weather/city/101190408") != std::string::npos);

  TEST_ASSERT_FLOAT_WITHIN(0.01f, 11.2f, out.currentTemp);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 11.2f, out.apparentTemp);
  TEST_ASSERT_EQUAL_INT(71, out.humidityPercent);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 2.5f, out.windSpeed);
  TEST_ASSERT_EQUAL_INT(0, out.precipitationProbabilityPercent);
  TEST_ASSERT_EQUAL_INT(2, out.weatherCode);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 15.0f, out.today.highTemp);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.0f, out.today.lowTemp);
  TEST_ASSERT_EQUAL_INT(2, out.today.weatherCode);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.0f, out.tomorrow.highTemp);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.0f, out.tomorrow.lowTemp);
  TEST_ASSERT_EQUAL_INT(0, out.tomorrow.weatherCode);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 13.0f, out.dayAfter.highTemp);
  TEST_ASSERT_EQUAL_INT(0, out.dayAfter.weatherCode);
  TEST_ASSERT_EQUAL_UINT32(277, diagnostics.elapsedMs);
}

void test_wxcn_requires_city_id_and_fails_closed() {
  WeatherCnFakeTransport transport;
  WeatherCnProvider provider(transport);
  WeatherSnapshot out;
  TEST_ASSERT_FALSE(provider.hasCityId());
  TEST_ASSERT_EQUAL(WeatherError::MISSING_FIELD, provider.fetch(kunshan(), out));

  provider.setCityId("101190408");
  transport.body = "{broken";
  TEST_ASSERT_EQUAL(WeatherError::PARSE, provider.fetch(kunshan(), out));

  transport.body = R"json({"time":"x","city":{},"data":{"shidu":"71%","wendu":"11","forecast":[{"high":"高温 1℃","low":"低温 1℃","type":"晴"}]}})json";
  TEST_ASSERT_EQUAL(WeatherError::MISSING_FIELD, provider.fetch(kunshan(), out));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_builds_open_meteo_url_and_parses_snapshot);
  RUN_TEST(test_missing_or_malformed_payload_fails_closed);
  RUN_TEST(test_short_daily_arrays_fail_closed);
  RUN_TEST(test_transport_errors_map_to_weather_errors_and_diagnostics);
  RUN_TEST(test_builds_metno_url_useragent_and_parses_snapshot);
  RUN_TEST(test_metno_errors_fail_closed);
  RUN_TEST(test_builds_amap_urls_and_parses_snapshot);
  RUN_TEST(test_amap_requires_key_and_maps_api_errors);
  RUN_TEST(test_builds_wxcn_url_and_parses_snapshot);
  RUN_TEST(test_wxcn_requires_city_id_and_fails_closed);
  return UNITY_END();
}
