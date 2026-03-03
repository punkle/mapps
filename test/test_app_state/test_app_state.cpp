#include <mapps/BerryRuntime.h>
#include <mapps/AppStateBackend.h>
#include <unity.h>

#include "PosixAppStateBackend.h"
#include "PosixIO.h"

#include <cstdio>
#include <memory>
#include <string>

static const std::string STATE_DIR = "/tmp/mapps_test_state";

static void cleanup_state_dir()
{
    // Remove all .state files in the test state directory
    std::vector<std::string> entries;
    if (PosixIO::listDirectory(STATE_DIR, entries)) {
        for (const auto &e : entries) {
            std::remove(PosixIO::joinPath(STATE_DIR, e).c_str());
        }
    }
    std::remove(STATE_DIR.c_str());
}

static void test_set_get_int()
{
    cleanup_state_dir();
    auto backend = std::make_shared<PosixAppStateBackend>(STATE_DIR);

    BerryRuntime rt("test_app", {});
    rt.setAppStateBackend(backend);
    rt.setSource(
        "def do_set() app_state.set('k', 42) end\n"
        "def do_get() return app_state.get('k') end\n");
    TEST_ASSERT_TRUE(rt.start());

    rt.call("do_set");
    AppValue result = rt.call("do_get");
    TEST_ASSERT_EQUAL(AppValue::Int, result.type);
    TEST_ASSERT_EQUAL(42, result.intVal);

    rt.stop();
    cleanup_state_dir();
}

static void test_set_get_float()
{
    cleanup_state_dir();
    auto backend = std::make_shared<PosixAppStateBackend>(STATE_DIR);

    BerryRuntime rt("test_app", {});
    rt.setAppStateBackend(backend);
    rt.setSource(
        "def do_set() app_state.set('k', 3.14) end\n"
        "def do_get() return app_state.get('k') end\n");
    TEST_ASSERT_TRUE(rt.start());

    rt.call("do_set");
    AppValue result = rt.call("do_get");
    TEST_ASSERT_EQUAL(AppValue::Float, result.type);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.14f, result.floatVal);

    rt.stop();
    cleanup_state_dir();
}

static void test_set_get_string()
{
    cleanup_state_dir();
    auto backend = std::make_shared<PosixAppStateBackend>(STATE_DIR);

    BerryRuntime rt("test_app", {});
    rt.setAppStateBackend(backend);
    rt.setSource(
        "def do_set() app_state.set('k', 'hello') end\n"
        "def do_get() return app_state.get('k') end\n");
    TEST_ASSERT_TRUE(rt.start());

    rt.call("do_set");
    AppValue result = rt.call("do_get");
    TEST_ASSERT_EQUAL(AppValue::String, result.type);
    TEST_ASSERT_EQUAL_STRING("hello", result.strVal.c_str());

    rt.stop();
    cleanup_state_dir();
}

static void test_set_get_bool()
{
    cleanup_state_dir();
    auto backend = std::make_shared<PosixAppStateBackend>(STATE_DIR);

    BerryRuntime rt("test_app", {});
    rt.setAppStateBackend(backend);
    rt.setSource(
        "def do_set() app_state.set('k', true) end\n"
        "def do_get() return app_state.get('k') end\n");
    TEST_ASSERT_TRUE(rt.start());

    rt.call("do_set");
    AppValue result = rt.call("do_get");
    TEST_ASSERT_EQUAL(AppValue::Bool, result.type);
    TEST_ASSERT_TRUE(result.boolVal);

    rt.stop();
    cleanup_state_dir();
}

static void test_get_missing_key()
{
    cleanup_state_dir();
    auto backend = std::make_shared<PosixAppStateBackend>(STATE_DIR);

    BerryRuntime rt("test_app", {});
    rt.setAppStateBackend(backend);
    rt.setSource("def do_get() return app_state.get('nonexistent') end\n");
    TEST_ASSERT_TRUE(rt.start());

    AppValue result = rt.call("do_get");
    TEST_ASSERT_EQUAL(AppValue::Nil, result.type);

    rt.stop();
    cleanup_state_dir();
}

static void test_remove()
{
    cleanup_state_dir();
    auto backend = std::make_shared<PosixAppStateBackend>(STATE_DIR);

    BerryRuntime rt("test_app", {});
    rt.setAppStateBackend(backend);
    rt.setSource(
        "def do_set() app_state.set('k', 42) end\n"
        "def do_remove() app_state.remove('k') end\n"
        "def do_get() return app_state.get('k') end\n");
    TEST_ASSERT_TRUE(rt.start());

    rt.call("do_set");
    AppValue before = rt.call("do_get");
    TEST_ASSERT_EQUAL(AppValue::Int, before.type);

    rt.call("do_remove");
    AppValue after = rt.call("do_get");
    TEST_ASSERT_EQUAL(AppValue::Nil, after.type);

    rt.stop();
    cleanup_state_dir();
}

static void test_clear()
{
    cleanup_state_dir();
    auto backend = std::make_shared<PosixAppStateBackend>(STATE_DIR);

    BerryRuntime rt("test_app", {});
    rt.setAppStateBackend(backend);
    rt.setSource(
        "def do_set() app_state.set('a', 1) app_state.set('b', 2) end\n"
        "def do_clear() app_state.clear() end\n"
        "def get_a() return app_state.get('a') end\n"
        "def get_b() return app_state.get('b') end\n");
    TEST_ASSERT_TRUE(rt.start());

    rt.call("do_set");
    TEST_ASSERT_EQUAL(AppValue::Int, rt.call("get_a").type);
    TEST_ASSERT_EQUAL(AppValue::Int, rt.call("get_b").type);

    rt.call("do_clear");
    TEST_ASSERT_EQUAL(AppValue::Nil, rt.call("get_a").type);
    TEST_ASSERT_EQUAL(AppValue::Nil, rt.call("get_b").type);

    rt.stop();
    cleanup_state_dir();
}

static void test_persistence()
{
    cleanup_state_dir();
    auto backend = std::make_shared<PosixAppStateBackend>(STATE_DIR);

    // First runtime: set a value
    {
        BerryRuntime rt("test_app", {});
        rt.setAppStateBackend(backend);
        rt.setSource("def do_set() app_state.set('k', 42) end\n");
        TEST_ASSERT_TRUE(rt.start());
        rt.call("do_set");
        rt.stop();
    }

    // Second runtime: get the value (same slug and backend)
    {
        BerryRuntime rt("test_app", {});
        rt.setAppStateBackend(backend);
        rt.setSource("def do_get() return app_state.get('k') end\n");
        TEST_ASSERT_TRUE(rt.start());
        AppValue result = rt.call("do_get");
        TEST_ASSERT_EQUAL(AppValue::Int, result.type);
        TEST_ASSERT_EQUAL(42, result.intVal);
        rt.stop();
    }

    cleanup_state_dir();
}

static void test_app_isolation()
{
    cleanup_state_dir();
    auto backend = std::make_shared<PosixAppStateBackend>(STATE_DIR);

    // App A sets a value
    {
        BerryRuntime rt("app_a", {});
        rt.setAppStateBackend(backend);
        rt.setSource("def do_set() app_state.set('k', 42) end\n");
        TEST_ASSERT_TRUE(rt.start());
        rt.call("do_set");
        rt.stop();
    }

    // App B should not see App A's value
    {
        BerryRuntime rt("app_b", {});
        rt.setAppStateBackend(backend);
        rt.setSource("def do_get() return app_state.get('k') end\n");
        TEST_ASSERT_TRUE(rt.start());
        AppValue result = rt.call("do_get");
        TEST_ASSERT_EQUAL(AppValue::Nil, result.type);
        rt.stop();
    }

    cleanup_state_dir();
}

int main()
{
    UNITY_BEGIN();

    RUN_TEST(test_set_get_int);
    RUN_TEST(test_set_get_float);
    RUN_TEST(test_set_get_string);
    RUN_TEST(test_set_get_bool);
    RUN_TEST(test_get_missing_key);
    RUN_TEST(test_remove);
    RUN_TEST(test_clear);
    RUN_TEST(test_persistence);
    RUN_TEST(test_app_isolation);

    return UNITY_END();
}
