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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_builds_open_meteo_url_and_parses_snapshot);
  RUN_TEST(test_missing_or_malformed_payload_fails_closed);
  RUN_TEST(test_short_daily_arrays_fail_closed);
  RUN_TEST(test_transport_errors_map_to_weather_errors_and_diagnostics);
  return UNITY_END();
}
