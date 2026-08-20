#include <unity.h>

#include <cstdint>
#include <vector>

#include "AppShell.h"

void setUp() {}
void tearDown() {}

class FakeMenuRenderer final : public IMenuRenderer {
 public:
  void render(const MenuViewModel& view, bool fullRedraw) override {
    ++renders;
    lastSelected = view.selectedIndex;
    lastFullRedraw = fullRedraw;
  }
  int renders = 0;
  size_t lastSelected = 0;
  bool lastFullRedraw = false;
};

class FakeApp final : public IApp {
 public:
  FakeApp(AppId id, const char* name) : id_(id), name_(name) {}
  AppId id() const override { return id_; }
  const char* name() const override { return name_; }
  void onEnter() override { ++enters; dirty = true; full = true; }
  void onExit() override { ++exits; }
  void onButton(InputEvent event) override { ++buttons; lastButton = event; dirty = true; }
  void tick(uint32_t nowMs) override { ++ticks; lastTick = nowMs; }
  bool takeDirtyFlag() override { const bool value = dirty; dirty = false; return value; }
  bool takeFullRedrawFlag() override { const bool value = full; full = false; return value; }
  void render(bool fullRedraw) override { ++renders; lastRenderFull = fullRedraw; }

  AppId id_;
  const char* name_;
  int enters = 0;
  int exits = 0;
  int buttons = 0;
  int ticks = 0;
  int renders = 0;
  uint32_t lastTick = 0;
  InputEvent lastButton = InputEvent::NONE;
  bool dirty = false;
  bool full = false;
  bool lastRenderFull = false;
};

struct ShellFixture {
  FakeMenuRenderer renderer;
  std::vector<AppDescriptor> descriptors{{AppId::STOCK, "股票"},
                                         {AppId::WEATHER, "天气"},
                                         {AppId::NIXIE_CLOCK, "辉光时钟"},
                                         {AppId::DEVICE_INFO, "设备信息"}};
  MenuApp menu{descriptors, renderer};
  FakeApp stock{AppId::STOCK, "股票"};
  FakeApp weather{AppId::WEATHER, "天气"};
  FakeApp nixie{AppId::NIXIE_CLOCK, "辉光时钟"};
  FakeApp deviceInfo{AppId::DEVICE_INFO, "设备信息"};
  AppManager manager{menu, {&stock, &weather, &nixie, &deviceInfo}};
};

void test_defaults_to_nixie_and_enters_once() {
  ShellFixture f;
  TEST_ASSERT_TRUE(f.manager.begin());
  TEST_ASSERT_EQUAL(AppId::NIXIE_CLOCK, f.manager.activeAppId());
  TEST_ASSERT_EQUAL_INT(1, f.nixie.enters);
  TEST_ASSERT_EQUAL_INT(0, f.stock.enters);
  TEST_ASSERT_EQUAL_INT(0, f.weather.enters);
}

void test_prev_long_returns_to_menu_and_does_not_reach_stock() {
  ShellFixture f;
  f.manager.begin(AppId::STOCK);
  f.manager.onInput(InputEvent::PREV_LONG);
  TEST_ASSERT_EQUAL(AppId::MENU, f.manager.activeAppId());
  TEST_ASSERT_EQUAL_INT(1, f.stock.exits);
  TEST_ASSERT_EQUAL_INT(0, f.stock.buttons);
}

void test_menu_selection_wraps_and_next_long_enters_selected_app() {
  ShellFixture f;
  f.manager.begin(AppId::STOCK);
  f.manager.onInput(InputEvent::PREV_LONG);
  TEST_ASSERT_EQUAL_UINT32(0, f.menu.selectedIndex());

  f.manager.onInput(InputEvent::NEXT_SHORT);
  TEST_ASSERT_EQUAL_UINT32(1, f.menu.selectedIndex());
  f.manager.onInput(InputEvent::NEXT_SHORT);
  TEST_ASSERT_EQUAL_UINT32(2, f.menu.selectedIndex());
  f.manager.onInput(InputEvent::PREV_SHORT);
  TEST_ASSERT_EQUAL_UINT32(1, f.menu.selectedIndex());

  f.manager.onInput(InputEvent::NEXT_LONG);
  TEST_ASSERT_EQUAL(AppId::WEATHER, f.manager.activeAppId());
  TEST_ASSERT_EQUAL_INT(1, f.weather.enters);
  TEST_ASSERT_EQUAL_INT(1, f.stock.exits);
}

void test_reserved_next_long_is_swallowed_in_normal_app() {
  ShellFixture f;
  f.manager.begin(AppId::STOCK);
  f.manager.onInput(InputEvent::NEXT_LONG);
  TEST_ASSERT_EQUAL(AppId::STOCK, f.manager.activeAppId());
  TEST_ASSERT_EQUAL_INT(0, f.stock.buttons);
}

void test_short_events_reach_only_active_normal_app() {
  ShellFixture f;
  f.manager.begin(AppId::STOCK);
  f.manager.onInput(InputEvent::PREV_SHORT);
  TEST_ASSERT_EQUAL_INT(1, f.stock.buttons);
  TEST_ASSERT_EQUAL(InputEvent::PREV_SHORT, f.stock.lastButton);
  TEST_ASSERT_EQUAL_INT(0, f.weather.buttons);
}

void test_tick_and_render_are_isolated_to_active_app() {
  ShellFixture f;
  f.manager.begin(AppId::STOCK);
  f.manager.tick(1234);
  f.manager.render();
  TEST_ASSERT_EQUAL_INT(1, f.stock.ticks);
  TEST_ASSERT_EQUAL_INT(1, f.stock.renders);
  TEST_ASSERT_TRUE(f.stock.lastRenderFull);
  TEST_ASSERT_EQUAL_INT(0, f.weather.ticks);
  TEST_ASSERT_EQUAL_INT(0, f.weather.renders);

  f.manager.onInput(InputEvent::PREV_LONG);
  f.manager.tick(2000);
  f.manager.render();
  TEST_ASSERT_EQUAL_INT(1, f.renderer.renders);
  TEST_ASSERT_EQUAL_INT(1, f.stock.ticks);
}

void test_weather_idles_to_nixie_at_exactly_thirty_seconds() {
  ShellFixture f;
  f.manager.begin(AppId::WEATHER);
  f.manager.tick(1000);
  f.manager.tick(30999);
  TEST_ASSERT_EQUAL(AppId::WEATHER, f.manager.activeAppId());
  f.manager.tick(31000);
  TEST_ASSERT_EQUAL(AppId::NIXIE_CLOCK, f.manager.activeAppId());
  TEST_ASSERT_EQUAL_INT(1, f.weather.exits);
  TEST_ASSERT_EQUAL_INT(1, f.nixie.enters);
}

void test_valid_button_activity_resets_idle_timer() {
  ShellFixture f;
  f.manager.begin(AppId::WEATHER);
  f.manager.tick(1000);
  f.manager.tick(20000);
  f.manager.onInput(InputEvent::NEXT_SHORT);
  f.manager.tick(20000);
  f.manager.tick(49999);
  TEST_ASSERT_EQUAL(AppId::WEATHER, f.manager.activeAppId());
  f.manager.tick(50000);
  TEST_ASSERT_EQUAL(AppId::NIXIE_CLOCK, f.manager.activeAppId());
}

void test_stock_is_exempt_from_idle_fallback() {
  ShellFixture f;
  f.manager.begin(AppId::STOCK);
  f.manager.tick(1000);
  f.manager.tick(1000 + 24U * 60U * 60U * 1000U);
  TEST_ASSERT_EQUAL(AppId::STOCK, f.manager.activeAppId());
  TEST_ASSERT_EQUAL_INT(0, f.nixie.enters);
}

void test_nixie_is_exempt_from_idle_fallback() {
  ShellFixture f;
  f.manager.begin(AppId::NIXIE_CLOCK);
  f.manager.tick(1000);
  f.manager.tick(1000 + 24U * 60U * 60U * 1000U);
  TEST_ASSERT_EQUAL(AppId::NIXIE_CLOCK, f.manager.activeAppId());
  TEST_ASSERT_EQUAL_INT(0, f.nixie.exits);
}

void test_menu_idles_to_nixie() {
  ShellFixture f;
  f.manager.begin(AppId::WEATHER);
  f.manager.onInput(InputEvent::PREV_LONG);
  f.manager.tick(5000);
  TEST_ASSERT_EQUAL(AppId::MENU, f.manager.activeAppId());
  f.manager.tick(35000);
  TEST_ASSERT_EQUAL(AppId::NIXIE_CLOCK, f.manager.activeAppId());
}

void test_idle_elapsed_is_wrap_safe() {
  ShellFixture f;
  f.manager.begin(AppId::DEVICE_INFO);
  const uint32_t start = 0xFFFFFF00U;
  f.manager.tick(start);
  f.manager.tick(static_cast<uint32_t>(start + 29999U));
  TEST_ASSERT_EQUAL(AppId::DEVICE_INFO, f.manager.activeAppId());
  f.manager.tick(static_cast<uint32_t>(start + 30000U));
  TEST_ASSERT_EQUAL(AppId::NIXIE_CLOCK, f.manager.activeAppId());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_to_nixie_and_enters_once);
  RUN_TEST(test_prev_long_returns_to_menu_and_does_not_reach_stock);
  RUN_TEST(test_menu_selection_wraps_and_next_long_enters_selected_app);
  RUN_TEST(test_reserved_next_long_is_swallowed_in_normal_app);
  RUN_TEST(test_short_events_reach_only_active_normal_app);
  RUN_TEST(test_tick_and_render_are_isolated_to_active_app);
  RUN_TEST(test_weather_idles_to_nixie_at_exactly_thirty_seconds);
  RUN_TEST(test_valid_button_activity_resets_idle_timer);
  RUN_TEST(test_stock_is_exempt_from_idle_fallback);
  RUN_TEST(test_nixie_is_exempt_from_idle_fallback);
  RUN_TEST(test_menu_idles_to_nixie);
  RUN_TEST(test_idle_elapsed_is_wrap_safe);
  return UNITY_END();
}
