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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_builds_open_meteo_url_and_parses_snapshot);
  RUN_TEST(test_missing_or_malformed_payload_fails_closed);
  RUN_TEST(test_short_daily_arrays_fail_closed);
  RUN_TEST(test_transport_errors_map_to_weather_errors_and_diagnostics);
  RUN_TEST(test_builds_metno_url_useragent_and_parses_snapshot);
  RUN_TEST(test_metno_errors_fail_closed);
  return UNITY_END();
}
