#include <unity.h>

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
  std::vector<AppDescriptor> descriptors{{AppId::STOCK, "股票"}, {AppId::WEATHER, "天气"}};
  MenuApp menu{descriptors, renderer};
  FakeApp stock{AppId::STOCK, "股票"};
  FakeApp weather{AppId::WEATHER, "天气"};
  AppManager manager{menu, {&stock, &weather}};
};

void test_defaults_to_stock_and_enters_once() {
  ShellFixture f;
  TEST_ASSERT_TRUE(f.manager.begin(AppId::STOCK));
  TEST_ASSERT_EQUAL(AppId::STOCK, f.manager.activeAppId());
  TEST_ASSERT_EQUAL_INT(1, f.stock.enters);
  TEST_ASSERT_EQUAL_INT(0, f.stock.exits);
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
  TEST_ASSERT_EQUAL_UINT32(0, f.menu.selectedIndex());
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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_to_stock_and_enters_once);
  RUN_TEST(test_prev_long_returns_to_menu_and_does_not_reach_stock);
  RUN_TEST(test_menu_selection_wraps_and_next_long_enters_selected_app);
  RUN_TEST(test_reserved_next_long_is_swallowed_in_normal_app);
  RUN_TEST(test_short_events_reach_only_active_normal_app);
  RUN_TEST(test_tick_and_render_are_isolated_to_active_app);
  return UNITY_END();
}
