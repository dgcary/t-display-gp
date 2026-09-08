#include <unity.h>

#include "HomeAssistantProvider.h"

void setUp() {}
void tearDown() {}

class FakeHaTransport final : public IHomeAssistantTransport {
 public:
  HttpResponse get(const HomeAssistantConfig& config,
                   const HomeAssistantEntityConfig& entity) override {
    seenBaseUrl = config.baseUrl;
    seenEntity = entity.entityId;
    return response;
  }
  HttpResponse response;
  std::string seenBaseUrl;
  std::string seenEntity;
};

HomeAssistantConfig config() {
  HomeAssistantConfig c;
  c.enabled = true;
  c.baseUrl = "https://ha.example.test:8123";
  c.token = "token";
  c.caCert = "-----BEGIN CERTIFICATE-----\nX\n-----END CERTIFICATE-----\n";
  c.entityCount = 1;
  c.entities[0] = {"sensor.temperature", "温度"};
  return c;
}

void test_home_assistant_parses_state_and_attributes() {
  FakeHaTransport transport;
  transport.response.error = HttpTransportError::NONE;
  transport.response.statusCode = 200;
  transport.response.body = R"json({"entity_id":"sensor.temperature","state":"26.4","attributes":{"friendly_name":"客厅温度","unit_of_measurement":"°C"}})json";
  HomeAssistantProvider provider(transport);
  HomeAssistantEntitySnapshot out;
  HomeAssistantDiagnostics diagnostics;
  HomeAssistantConfig c = config();
  TEST_ASSERT_EQUAL(HomeAssistantError::NONE, provider.fetch(c, c.entities[0], out, &diagnostics));
  TEST_ASSERT_EQUAL_STRING("sensor.temperature", out.entityId.c_str());
  TEST_ASSERT_EQUAL_STRING("26.4", out.state.c_str());
  TEST_ASSERT_EQUAL_STRING("°C", out.unit.c_str());
  TEST_ASSERT_EQUAL_STRING(c.baseUrl.c_str(), transport.seenBaseUrl.c_str());
}

void test_home_assistant_rejects_entity_mismatch_without_mutating_cache() {
  FakeHaTransport transport;
  transport.response.error = HttpTransportError::NONE;
  transport.response.statusCode = 200;
  transport.response.body = R"json({"entity_id":"sensor.other","state":"99","attributes":{}})json";
  HomeAssistantProvider provider(transport);
  HomeAssistantConfig c = config();
  HomeAssistantEntitySnapshot out;
  out.state = "cached";
  TEST_ASSERT_EQUAL(HomeAssistantError::ENTITY_MISMATCH, provider.fetch(c, c.entities[0], out));
  TEST_ASSERT_EQUAL_STRING("cached", out.state.c_str());
}

void test_home_assistant_maps_401_to_unauthorized() {
  FakeHaTransport transport;
  transport.response.error = HttpTransportError::HTTP_STATUS;
  transport.response.statusCode = 401;
  HomeAssistantProvider provider(transport);
  HomeAssistantConfig c = config();
  HomeAssistantEntitySnapshot out;
  TEST_ASSERT_EQUAL(HomeAssistantError::UNAUTHORIZED, provider.fetch(c, c.entities[0], out));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_home_assistant_parses_state_and_attributes);
  RUN_TEST(test_home_assistant_rejects_entity_mismatch_without_mutating_cache);
  RUN_TEST(test_home_assistant_maps_401_to_unauthorized);
  return UNITY_END();
}
