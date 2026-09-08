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
                                         {AppId::BAMBU, "Bambu Lab"},
                                         {AppId::HOME_ASSISTANT, "智能家居"},
                                         {AppId::DEVICE_INFO, "设备信息"}};
  MenuApp menu{descriptors, renderer};
  FakeApp stock{AppId::STOCK, "股票"};
  FakeApp weather{AppId::WEATHER, "天气"};
  FakeApp bambu{AppId::BAMBU, "Bambu Lab"};
  FakeApp homeAssistant{AppId::HOME_ASSISTANT, "智能家居"};
  FakeApp deviceInfo{AppId::DEVICE_INFO, "设备信息"};
  AppManager manager{menu, {&stock, &weather, &bambu, &homeAssistant, &deviceInfo}};
};

void test_defaults_to_stock_and_enters_once() {
  ShellFixture f;
  TEST_ASSERT_TRUE(f.manager.begin());
  TEST_ASSERT_EQUAL(AppId::STOCK, f.manager.activeAppId());
  TEST_ASSERT_EQUAL_INT(1, f.stock.enters);
  TEST_ASSERT_EQUAL_INT(0, f.weather.enters);
  TEST_ASSERT_EQUAL_INT(0, f.bambu.enters);
  TEST_ASSERT_EQUAL_INT(0, f.homeAssistant.enters);
}

void test_prev_long_returns_to_menu_and_does_not_reach_stock() {
  ShellFixture f;
  f.manager.begin(AppId::STOCK);
  f.manager.onInput(InputEvent::PREV_LONG);
  TEST_ASSERT_EQUAL(AppId::MENU, f.manager.activeAppId());
  TEST_ASSERT_EQUAL_INT(1, f.stock.exits);
  TEST_ASSERT_EQUAL_INT(0, f.stock.buttons);
}

void test_menu_selection_wraps_and_enters_bambu_in_final_order() {
  ShellFixture f;
  f.manager.begin(AppId::STOCK);
  f.manager.onInput(InputEvent::PREV_LONG);
  TEST_ASSERT_EQUAL_UINT32(0, f.menu.selectedIndex());

  f.manager.onInput(InputEvent::NEXT_SHORT);
  TEST_ASSERT_EQUAL_UINT32(1, f.menu.selectedIndex());
  f.manager.onInput(InputEvent::NEXT_SHORT);
  TEST_ASSERT_EQUAL_UINT32(2, f.menu.selectedIndex());
  TEST_ASSERT_EQUAL(AppId::BAMBU, f.menu.selectedAppId());

  f.manager.onInput(InputEvent::NEXT_LONG);
  TEST_ASSERT_EQUAL(AppId::BAMBU, f.manager.activeAppId());
  TEST_ASSERT_EQUAL_INT(1, f.bambu.enters);
  TEST_ASSERT_EQUAL_INT(1, f.stock.exits);
}

void test_menu_wraps_across_five_apps() {
  ShellFixture f;
  f.manager.begin(AppId::STOCK);
  f.manager.onInput(InputEvent::PREV_LONG);
  f.manager.onInput(InputEvent::PREV_SHORT);
  TEST_ASSERT_EQUAL_UINT32(4, f.menu.selectedIndex());
  TEST_ASSERT_EQUAL(AppId::DEVICE_INFO, f.menu.selectedAppId());
  f.manager.onInput(InputEvent::NEXT_SHORT);
  TEST_ASSERT_EQUAL_UINT32(0, f.menu.selectedIndex());
  TEST_ASSERT_EQUAL(AppId::STOCK, f.menu.selectedAppId());
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
  f.manager.begin(AppId::BAMBU);
  f.manager.onInput(InputEvent::PREV_SHORT);
  TEST_ASSERT_EQUAL_INT(1, f.bambu.buttons);
  TEST_ASSERT_EQUAL(InputEvent::PREV_SHORT, f.bambu.lastButton);
  TEST_ASSERT_EQUAL_INT(0, f.stock.buttons);
}

void test_tick_and_render_are_isolated_to_active_app() {
  ShellFixture f;
  f.manager.begin(AppId::BAMBU);
  f.manager.tick(1234);
  f.manager.render();
  TEST_ASSERT_EQUAL_INT(1, f.bambu.ticks);
  TEST_ASSERT_EQUAL_INT(1, f.bambu.renders);
  TEST_ASSERT_TRUE(f.bambu.lastRenderFull);
  TEST_ASSERT_EQUAL_INT(0, f.weather.ticks);
  TEST_ASSERT_EQUAL_INT(0, f.stock.ticks);

  f.manager.onInput(InputEvent::PREV_LONG);
  f.manager.tick(2000);
  f.manager.render();
  TEST_ASSERT_EQUAL_INT(1, f.renderer.renders);
  TEST_ASSERT_EQUAL_INT(1, f.bambu.ticks);
}

void test_weather_remains_active_after_long_inactivity() {
  ShellFixture f;
  f.manager.begin(AppId::WEATHER);
  f.manager.tick(1000);
  f.manager.tick(1000 + 24U * 60U * 60U * 1000U);
  TEST_ASSERT_EQUAL(AppId::WEATHER, f.manager.activeAppId());
  TEST_ASSERT_EQUAL_INT(0, f.weather.exits);
}

void test_bambu_remains_active_after_long_inactivity() {
  ShellFixture f;
  f.manager.begin(AppId::BAMBU);
  f.manager.tick(1000);
  f.manager.tick(1000 + 24U * 60U * 60U * 1000U);
  TEST_ASSERT_EQUAL(AppId::BAMBU, f.manager.activeAppId());
  TEST_ASSERT_EQUAL_INT(0, f.bambu.exits);
}

void test_menu_remains_active_after_long_inactivity() {
  ShellFixture f;
  f.manager.begin(AppId::WEATHER);
  f.manager.onInput(InputEvent::PREV_LONG);
  f.manager.tick(5000);
  f.manager.tick(5000 + 24U * 60U * 60U * 1000U);
  TEST_ASSERT_EQUAL(AppId::MENU, f.manager.activeAppId());
}

void test_device_info_remains_active_across_millis_wrap() {
  ShellFixture f;
  f.manager.begin(AppId::DEVICE_INFO);
  const uint32_t start = 0xFFFFFF00U;
  f.manager.tick(start);
  f.manager.tick(static_cast<uint32_t>(start + 60000U));
  TEST_ASSERT_EQUAL(AppId::DEVICE_INFO, f.manager.activeAppId());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_to_stock_and_enters_once);
  RUN_TEST(test_prev_long_returns_to_menu_and_does_not_reach_stock);
  RUN_TEST(test_menu_selection_wraps_and_enters_bambu_in_final_order);
  RUN_TEST(test_menu_wraps_across_five_apps);
  RUN_TEST(test_reserved_next_long_is_swallowed_in_normal_app);
  RUN_TEST(test_short_events_reach_only_active_normal_app);
  RUN_TEST(test_tick_and_render_are_isolated_to_active_app);
  RUN_TEST(test_weather_remains_active_after_long_inactivity);
  RUN_TEST(test_bambu_remains_active_after_long_inactivity);
  RUN_TEST(test_menu_remains_active_after_long_inactivity);
  RUN_TEST(test_device_info_remains_active_across_millis_wrap);
  return UNITY_END();
}
