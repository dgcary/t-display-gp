#include <unity.h>

#include <cstdint>

#include "AppShell.h"

void setUp() {}
void tearDown() {}

class FakeApp final : public IApp {
 public:
  FakeApp(AppId id, const char* name, size_t pages = 1U) : id_(id), name_(name), pages_(pages) {}
  AppId id() const override { return id_; }
  const char* name() const override { return name_; }
  void onEnter() override { ++enters; dirty = true; full = true; }
  void onExit() override { ++exits; }
  void onButton(InputEvent event) override { ++buttons; lastButton = event; }
  void tick(uint32_t nowMs) override { ++ticks; lastTick = nowMs; }
  bool takeDirtyFlag() override { const bool value = dirty; dirty = false; return value; }
  bool takeFullRedrawFlag() override { const bool value = full; full = false; return value; }
  void render(bool fullRedraw) override { ++renders; lastRenderFull = fullRedraw; }
  size_t pageCount() const override { return pages_; }
  bool selectPage(size_t pageIndex) override {
    if (pageIndex >= pages_) return false;
    if (pageIndex != selected_) {
      selected_ = pageIndex;
      dirty = true;
      full = true;
    }
    ++selectCalls;
    return true;
  }
  size_t selectedPage() const override { return selected_; }

  AppId id_;
  const char* name_;
  size_t pages_ = 1U;
  size_t selected_ = 0U;
  int enters = 0;
  int exits = 0;
  int buttons = 0;
  int ticks = 0;
  int renders = 0;
  int selectCalls = 0;
  uint32_t lastTick = 0;
  InputEvent lastButton = InputEvent::NONE;
  bool dirty = false;
  bool full = false;
  bool lastRenderFull = false;
};

struct ShellFixture {
  FakeApp weather{AppId::WEATHER, "天气", 1U};
  FakeApp stock{AppId::STOCK, "股票", 4U};
  FakeApp bambu{AppId::BAMBU, "Bambu Lab", 2U};
  FakeApp homeAssistant{AppId::HOME_ASSISTANT, "智能家居", 1U};
  FakeApp deviceInfo{AppId::DEVICE_INFO, "设备信息", 1U};
  AppManager manager{&weather, &stock, &bambu, &homeAssistant, &deviceInfo};
};

void test_starts_on_weather() {
  ShellFixture f;
  TEST_ASSERT_TRUE(f.manager.begin());
  TEST_ASSERT_EQUAL(AppId::WEATHER, f.manager.activeAppId());
  TEST_ASSERT_EQUAL_UINT32(0, f.manager.activePageIndex());
  TEST_ASSERT_EQUAL_INT(1, f.weather.enters);
}

void test_next_walks_weather_stock4_bambu2_ha_device_and_wraps() {
  ShellFixture f;
  f.manager.begin();

  f.manager.onInput(InputEvent::NEXT_SHORT);
  TEST_ASSERT_EQUAL(AppId::STOCK, f.manager.activeAppId());
  TEST_ASSERT_EQUAL_UINT32(0, f.manager.activePageIndex());
  for (size_t expected = 1U; expected < 4U; ++expected) {
    f.manager.onInput(InputEvent::NEXT_SHORT);
    TEST_ASSERT_EQUAL(AppId::STOCK, f.manager.activeAppId());
    TEST_ASSERT_EQUAL_UINT32(expected, f.manager.activePageIndex());
  }

  f.manager.onInput(InputEvent::NEXT_SHORT);
  TEST_ASSERT_EQUAL(AppId::BAMBU, f.manager.activeAppId());
  TEST_ASSERT_EQUAL_UINT32(0, f.manager.activePageIndex());
  f.manager.onInput(InputEvent::NEXT_SHORT);
  TEST_ASSERT_EQUAL(AppId::BAMBU, f.manager.activeAppId());
  TEST_ASSERT_EQUAL_UINT32(1, f.manager.activePageIndex());
  f.manager.onInput(InputEvent::NEXT_SHORT);
  TEST_ASSERT_EQUAL(AppId::HOME_ASSISTANT, f.manager.activeAppId());
  f.manager.onInput(InputEvent::NEXT_SHORT);
  TEST_ASSERT_EQUAL(AppId::DEVICE_INFO, f.manager.activeAppId());
  f.manager.onInput(InputEvent::NEXT_SHORT);
  TEST_ASSERT_EQUAL(AppId::WEATHER, f.manager.activeAppId());
}

void test_prev_walks_reverse_and_wraps() {
  ShellFixture f;
  f.manager.begin();
  f.manager.onInput(InputEvent::PREV_SHORT);
  TEST_ASSERT_EQUAL(AppId::DEVICE_INFO, f.manager.activeAppId());
  f.manager.onInput(InputEvent::PREV_SHORT);
  TEST_ASSERT_EQUAL(AppId::HOME_ASSISTANT, f.manager.activeAppId());
  f.manager.onInput(InputEvent::PREV_SHORT);
  TEST_ASSERT_EQUAL(AppId::BAMBU, f.manager.activeAppId());
  TEST_ASSERT_EQUAL_UINT32(1, f.manager.activePageIndex());
}

void test_zero_page_apps_are_skipped() {
  FakeApp weather{AppId::WEATHER, "天气", 1U};
  FakeApp stock{AppId::STOCK, "股票", 0U};
  FakeApp bambu{AppId::BAMBU, "Bambu", 1U};
  FakeApp home{AppId::HOME_ASSISTANT, "HA", 1U};
  AppManager manager{&weather, &stock, &bambu, &home};
  TEST_ASSERT_TRUE(manager.begin());
  manager.onInput(InputEvent::NEXT_SHORT);
  TEST_ASSERT_EQUAL(AppId::BAMBU, manager.activeAppId());
  TEST_ASSERT_EQUAL_INT(0, stock.enters);
}

void test_same_app_page_switch_does_not_exit_or_reenter() {
  ShellFixture f;
  f.manager.begin();
  f.manager.onInput(InputEvent::NEXT_SHORT);
  TEST_ASSERT_EQUAL_INT(1, f.stock.enters);
  f.manager.onInput(InputEvent::NEXT_SHORT);
  TEST_ASSERT_EQUAL_UINT32(1, f.manager.activePageIndex());
  TEST_ASSERT_EQUAL_INT(1, f.stock.enters);
  TEST_ASSERT_EQUAL_INT(0, f.stock.exits);
}

void test_short_events_are_navigation_only_not_forwarded_to_apps() {
  ShellFixture f;
  f.manager.begin();
  f.manager.onInput(InputEvent::NEXT_SHORT);
  f.manager.onInput(InputEvent::PREV_SHORT);
  TEST_ASSERT_EQUAL_INT(0, f.weather.buttons);
  TEST_ASSERT_EQUAL_INT(0, f.stock.buttons);
}

void test_long_events_are_noop() {
  ShellFixture f;
  f.manager.begin();
  f.manager.onInput(InputEvent::PREV_LONG);
  TEST_ASSERT_EQUAL(AppId::WEATHER, f.manager.activeAppId());
  f.manager.onInput(InputEvent::NEXT_LONG);
  TEST_ASSERT_EQUAL(AppId::WEATHER, f.manager.activeAppId());
  TEST_ASSERT_EQUAL_INT(0, f.weather.buttons);
}

void test_tick_and_render_are_isolated_to_active_app() {
  ShellFixture f;
  f.manager.begin();
  f.manager.tick(1234U);
  f.manager.render();
  TEST_ASSERT_EQUAL_INT(1, f.weather.ticks);
  TEST_ASSERT_EQUAL_INT(1, f.weather.renders);
  TEST_ASSERT_TRUE(f.weather.lastRenderFull);
  TEST_ASSERT_EQUAL_INT(0, f.stock.ticks);

  f.manager.onInput(InputEvent::NEXT_SHORT);
  f.manager.tick(2000U);
  f.manager.render();
  TEST_ASSERT_EQUAL_INT(1, f.stock.ticks);
  TEST_ASSERT_EQUAL_INT(1, f.stock.renders);
}

void test_active_app_remains_after_long_inactivity() {
  ShellFixture f;
  f.manager.begin();
  f.manager.onInput(InputEvent::NEXT_SHORT);
  f.manager.onInput(InputEvent::NEXT_SHORT);
  f.manager.tick(1000U);
  f.manager.tick(1000U + 24U * 60U * 60U * 1000U);
  TEST_ASSERT_EQUAL(AppId::STOCK, f.manager.activeAppId());
  TEST_ASSERT_EQUAL_UINT32(1, f.manager.activePageIndex());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_starts_on_weather);
  RUN_TEST(test_next_walks_weather_stock4_bambu2_ha_device_and_wraps);
  RUN_TEST(test_prev_walks_reverse_and_wraps);
  RUN_TEST(test_zero_page_apps_are_skipped);
  RUN_TEST(test_same_app_page_switch_does_not_exit_or_reenter);
  RUN_TEST(test_short_events_are_navigation_only_not_forwarded_to_apps);
  RUN_TEST(test_long_events_are_noop);
  RUN_TEST(test_tick_and_render_are_isolated_to_active_app);
  RUN_TEST(test_active_app_remains_after_long_inactivity);
  return UNITY_END();
}
